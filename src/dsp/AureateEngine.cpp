#include "AureateEngine.h"

namespace
{
    // Keeps a requested filter frequency safely below Nyquist regardless of
    // the (possibly oversampled) rate it runs at, so
    // juce::dsp::IIR::Coefficients::makeLowPass/makeLowShelf/makeHighShelf
    // never receive an out-of-range value (which would produce invalid/NaN
    // coefficients).
    float clampBelowNyquist (float frequencyHz, double rateHz) noexcept
    {
        const auto nyquist = static_cast<float> (rateHz) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }

    // Warmth (0-1) -> HF-rolloff cutoff, log-interpolated between
    // warmthMinLowPassHz (0%, effectively transparent) and
    // warmthMaxLowPassHz (100%, gentle darkening).
    float mapWarmthToLowPassHz (float proportion01, float minHz, float maxHz) noexcept
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, proportion01);
        return minHz * std::pow (maxHz / minHz, clamped);
    }

    // Warmth (0-1) -> saturator bias, linear from 0 (symmetric) to
    // maxBias (maximally asymmetric).
    float mapWarmthToBias (float proportion01, float maxBias) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, proportion01) * maxBias;
    }

    // Bias (-1 to 1) -> additional saturator bias contribution, linear.
    float mapExplicitBiasToBias (float proportionMinus1To1, float maxExplicitBias) noexcept
    {
        return juce::jlimit (-1.0f, 1.0f, proportionMinus1To1) * maxExplicitBias;
    }

    // Tone tilt (-1 to 1) -> shelf gain in dB, linear.
    float mapTiltToDb (float proportionMinus1To1, float maxTiltDb) noexcept
    {
        return juce::jlimit (-1.0f, 1.0f, proportionMinus1To1) * maxTiltDb;
    }
}

AureateEngine::AureateEngine() = default;

void AureateEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    preparedMaximumBlockSize = static_cast<size_t> (spec.maximumBlockSize);
    preparedNumChannels = static_cast<size_t> (spec.numChannels);

    //--------------------------------------------------------------------
    // Wow/Flutter: modulated delay line at the host sample rate, ahead of
    // Drive/oversampling. The base delay (and therefore its contribution to
    // getLatencySamples() below) depends only on sampleRate, never on the
    // live Wow/Flutter amount - see the class comment in AureateEngine.h for
    // why that matters. Rounded to an exact integer sample count (at every
    // sample rate, not just ones where the millisecond value happens to
    // divide evenly) so that at amount == 0 the delay line's instantaneous
    // delay is exactly an integer - i.e. a genuinely sample-accurate,
    // LTI pure delay with no sub-sample interpolation smoothing - which is
    // what keeps the dry/wet null test exact regardless of sample rate.
    wowFlutterBaseDelaySamples = std::round (sampleRate * static_cast<double> (wowFlutterBaseDelayMs) / 1000.0);
    wowFlutterMaxWowDepthSamples = sampleRate * static_cast<double> (wowFlutterMaxWowDepthMs) / 1000.0;
    wowFlutterMaxFlutterDepthSamples = sampleRate * static_cast<double> (wowFlutterMaxFlutterDepthMs) / 1000.0;
    wowFlutterWowPhaseIncrement = juce::MathConstants<double>::twoPi * static_cast<double> (wowFlutterWowRateHz) / sampleRate;
    wowFlutterFlutterPhaseIncrement = juce::MathConstants<double>::twoPi * static_cast<double> (wowFlutterFlutterRateHz) / sampleRate;

    // Generous guard margin above base+depth for the interpolator's own
    // sample footprint and floating-point rounding.
    const auto wowFlutterMaxDelaySamples = wowFlutterBaseDelaySamples + wowFlutterMaxWowDepthSamples
                                            + wowFlutterMaxFlutterDepthSamples + 8.0;
    wowFlutterDelayLine.setMaximumDelayInSamples (static_cast<int> (std::ceil (wowFlutterMaxDelaySamples)));
    wowFlutterDelayLine.prepare (spec);
    wowFlutterDelayLine.setDelay (static_cast<float> (wowFlutterBaseDelaySamples));

    driveGain.setRampDurationSeconds (smoothingTimeSeconds);
    driveGain.prepare (spec);

    // 4x oversampling (2^2), half-band polyphase IIR: much lower latency
    // than the equiripple FIR alternative for a given stopband quality,
    // which matters here because that latency is exactly what has to be
    // compensated on the dry path below. useIntegerLatency=true so the
    // reported latency (and therefore setLatencySamples()) is an exact
    // integer sample count rather than something we'd have to round.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        oversamplingFactorPow2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    oversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    // Total reported latency = the oversampler's latency + Wow/Flutter's
    // fixed base delay (rounded to the nearest integer sample, as
    // setLatencySamples() requires an integer). Both are prepare()-time
    // constants only, so this never changes due to parameter automation.
    latencySamples = static_cast<int> (std::round (oversampler->getLatencyInSamples()))
                      + static_cast<int> (std::round (wowFlutterBaseDelaySamples));

    // Warmth's HF-rolloff filter, the Tone tilt shelves, and the HF/LF Trim
    // shelves all run inside the oversampled block (see AureateEngine.h), so
    // they must be prepared at the oversampled rate/block size, not the
    // host's.
    const auto oversamplingMultiplier = 1u << static_cast<unsigned> (oversamplingFactorPow2);
    oversampledRate = sampleRate * static_cast<double> (oversamplingMultiplier);

    juce::dsp::ProcessSpec oversampledSpec;
    oversampledSpec.sampleRate = oversampledRate;
    oversampledSpec.maximumBlockSize = spec.maximumBlockSize * oversamplingMultiplier;
    oversampledSpec.numChannels = spec.numChannels;

    warmthLowPass.prepare (oversampledSpec);
    tiltLowShelf.prepare (oversampledSpec);
    tiltHighShelf.prepare (oversampledSpec);
    hfTrimShelf.prepare (oversampledSpec);
    lfTrimShelf.prepare (oversampledSpec);

    outputGain.setRampDurationSeconds (smoothingTimeSeconds);
    outputGain.prepare (spec);

    dryWetMixer.prepare (spec);
    dryWetMixer.setWetLatency (static_cast<float> (latencySamples));

    // juce::dsp::DryWetMixer defaults its internal mix to fully wet (1.0)
    // until told otherwise, and its own reset() (called from our reset()
    // below) snaps its internal dry/wet gain smoothers' *current* value to
    // whatever their *target* happens to be at that moment - it does not
    // know about lastMixProportion. Priming the real target here, before
    // reset() runs, means the mixer is already sitting at the correct dry/
    // wet balance for the very first process() call instead of ramping up
    // from "fully wet" over its internal 50ms default ramp.
    dryWetMixer.setWetMixProportion (lastMixProportion);

    // Re-seed the smoothers at the new rate, but pin current == target to
    // whatever was last requested (defaulting to the ParameterLayout
    // defaults on first prepare) - otherwise the ramp would sweep up from a
    // default-constructed 0 on the very first block.
    const auto warmthLowPassHz = mapWarmthToLowPassHz (lastWarmthProportion01, warmthMinLowPassHz, warmthMaxLowPassHz);
    const auto warmthBias = mapWarmthToBias (lastWarmthProportion01, warmthMaxBias);
    const auto explicitBias = mapExplicitBiasToBias (lastBiasProportion, maxExplicitBias);
    const auto tiltDb = mapTiltToDb (lastTiltProportion, maxTiltDb);

    // Reset at the host sampleRate, NOT oversampledRate: process() below
    // calls warmthLowPassHzSmoothed.skip(numSamples) once per block with
    // numSamples taken from the host-rate block (before
    // oversampler->processSamplesUp()), identically to every other smoother
    // here - so stepsToTarget must be computed in that same host-rate
    // domain, or the countdown (set from oversampledRate = sampleRate * 4)
    // takes 4x as many host-rate skip() calls to exhaust, stretching this
    // smoother's ramp to ~4x smoothingTimeSeconds in real time versus its
    // siblings (see issue #12; verified against JUCE 8.0.14's
    // SmoothedValue::reset()/skip(), which never reference sampleRate again
    // after reset()). The resulting Hz *value* is still applied to a filter
    // that runs at oversampledRate (see clampBelowNyquist() below) - only
    // the smoother's own timebase needs to match the domain skip() is
    // called from.
    warmthLowPassHzSmoothed.reset (sampleRate, smoothingTimeSeconds);
    warmthLowPassHzSmoothed.setCurrentAndTargetValue (warmthLowPassHz);
    warmthBiasSmoothed.reset (sampleRate, smoothingTimeSeconds);
    warmthBiasSmoothed.setCurrentAndTargetValue (warmthBias);
    explicitBiasSmoothed.reset (sampleRate, smoothingTimeSeconds);
    explicitBiasSmoothed.setCurrentAndTargetValue (explicitBias);
    tiltDbSmoothed.reset (sampleRate, smoothingTimeSeconds);
    tiltDbSmoothed.setCurrentAndTargetValue (tiltDb);
    mixSmoothed.reset (sampleRate, smoothingTimeSeconds);
    mixSmoothed.setCurrentAndTargetValue (lastMixProportion);
    wowFlutterAmountSmoothed.reset (sampleRate, smoothingTimeSeconds);
    wowFlutterAmountSmoothed.setCurrentAndTargetValue (lastWowFlutterProportion01);
    hissAmountSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hissAmountSmoothed.setCurrentAndTargetValue (lastHissProportion01);
    hfTrimDbSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hfTrimDbSmoothed.setCurrentAndTargetValue (lastHfTrimDb);
    lfTrimDbSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lfTrimDbSmoothed.setCurrentAndTargetValue (lastLfTrimDb);

    reset();

    // Prime the filter coefficients immediately so the very first
    // process() call runs with correct, non-default coefficients rather
    // than an identity/uninitialised state.
    *warmthLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
        oversampledRate, clampBelowNyquist (warmthLowPassHz, oversampledRate), filterQ);
    *tiltLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        oversampledRate, clampBelowNyquist (tiltLowShelfHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (-tiltDb));
    *tiltHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        oversampledRate, clampBelowNyquist (tiltHighShelfHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (tiltDb));
    *hfTrimShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        oversampledRate, clampBelowNyquist (hfTrimHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (lastHfTrimDb));
    *lfTrimShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        oversampledRate, clampBelowNyquist (lfTrimHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (lastLfTrimDb));
}

void AureateEngine::reset()
{
    wowFlutterDelayLine.reset();
    wowFlutterWowPhase = 0.0;
    wowFlutterFlutterPhase = 0.0;

    driveGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    warmthLowPass.reset();
    tiltLowShelf.reset();
    tiltHighShelf.reset();
    hfTrimShelf.reset();
    lfTrimShelf.reset();
    outputGain.reset();
    dryWetMixer.reset();

    // Deterministic re-seed: keeps Hiss's contribution bit-reproducible
    // across reset()/prepare() cycles (playback stop/start, sample-rate
    // change, etc.), which is what makes its own tests non-flaky.
    hissRandom.setSeed (1);
}

void AureateEngine::setDriveDb (float newDriveDb)
{
    driveGain.setGainDecibels (newDriveDb);
}

void AureateEngine::setWarmthProportion (float newProportion01)
{
    lastWarmthProportion01 = newProportion01;
    warmthLowPassHzSmoothed.setTargetValue (mapWarmthToLowPassHz (newProportion01, warmthMinLowPassHz, warmthMaxLowPassHz));
    warmthBiasSmoothed.setTargetValue (mapWarmthToBias (newProportion01, warmthMaxBias));
}

void AureateEngine::setToneProportion (float newProportionMinus1To1)
{
    lastTiltProportion = newProportionMinus1To1;
    tiltDbSmoothed.setTargetValue (mapTiltToDb (newProportionMinus1To1, maxTiltDb));
}

void AureateEngine::setMixProportion (float newProportion01)
{
    lastMixProportion = newProportion01;
    mixSmoothed.setTargetValue (newProportion01);
}

void AureateEngine::setOutputDb (float newOutputDb)
{
    outputGain.setGainDecibels (newOutputDb);
}

void AureateEngine::setBiasProportion (float newProportionMinus1To1)
{
    lastBiasProportion = newProportionMinus1To1;
    explicitBiasSmoothed.setTargetValue (mapExplicitBiasToBias (newProportionMinus1To1, maxExplicitBias));
}

void AureateEngine::setWowFlutterProportion (float newProportion01)
{
    lastWowFlutterProportion01 = newProportion01;
    wowFlutterAmountSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, newProportion01));
}

void AureateEngine::setHissProportion (float newProportion01)
{
    lastHissProportion01 = newProportion01;
    hissAmountSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, newProportion01));
}

void AureateEngine::setCharacter (TapeSaturator::Model newModel)
{
    character = newModel;
}

void AureateEngine::setHfTrimDb (float newTrimDb)
{
    lastHfTrimDb = newTrimDb;
    hfTrimDbSmoothed.setTargetValue (newTrimDb);
}

void AureateEngine::setLfTrimDb (float newTrimDb)
{
    lastLfTrimDb = newTrimDb;
    lfTrimDbSmoothed.setTargetValue (newTrimDb);
}

void AureateEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // Defensive: clamp to the sample/channel counts declared to prepare().
    // juce::dsp::Oversampling::initProcessing() (called from prepare()
    // above) sizes each internal stage's buffer to exactly
    // spec.maximumBlockSize * factor and does NOT grow it for a later,
    // bigger block (JUCE 8.0.14, juce_dsp/processors/juce_Oversampling.cpp)
    // - each stage's own processSamplesUp()/processSamplesDown() guards
    // that only with a jassert, which is compiled out entirely in Release
    // and - even in this Debug build - is a silent no-op unless a debugger
    // is actually attached (see JUCE_BREAK_IN_DEBUGGER in
    // juce_PlatformDefs.h), so an oversized block falls straight through to
    // a real out-of-bounds heap write either way. Trimming the working
    // block to what prepare() actually sized things for - rather than
    // processing (and writing) past that capacity - is the same defensive
    // pattern as sibling plugin lancet's LancetEngine::process() (see
    // issue #13).
    const auto numSamples = juce::jmin (requestedSamples, preparedMaximumBlockSize);
    const auto numChannels = juce::jmin (block.getNumChannels(), preparedNumChannels);

    if (numSamples == 0 || numChannels == 0)
        return;

    auto workingBlock = block.getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    // Coefficient recomputation involves trig calls (tan/cos), so the
    // Warmth low-pass, Tone tilt shelves, and HF/LF Trim shelves are
    // smoothed and re-derived once per block rather than per sample - a
    // standard real-time-safe compromise for IIR filters, whose
    // coefficients aren't cheap to interpolate directly. Drive/Output still
    // ramp sample-accurately via juce::dsp::Gain's internal SmoothedValue;
    // the saturator's bias terms, Wow/Flutter amount, Hiss amount, and Mix
    // are re-applied every block below (Wow/Flutter's own per-sample
    // modulation shape is recomputed every sample regardless, see below).
    const auto warmthLowPassHz = clampBelowNyquist (
        warmthLowPassHzSmoothed.skip (static_cast<int> (numSamples)), oversampledRate);
    const auto warmthBias = warmthBiasSmoothed.skip (static_cast<int> (numSamples));
    const auto explicitBias = explicitBiasSmoothed.skip (static_cast<int> (numSamples));
    const auto tiltDb = tiltDbSmoothed.skip (static_cast<int> (numSamples));
    const auto wetMix = mixSmoothed.skip (static_cast<int> (numSamples));
    const auto wowFlutterAmount = wowFlutterAmountSmoothed.skip (static_cast<int> (numSamples));
    const auto hissAmount = hissAmountSmoothed.skip (static_cast<int> (numSamples));
    const auto hfTrimDb = hfTrimDbSmoothed.skip (static_cast<int> (numSamples));
    const auto lfTrimDb = lfTrimDbSmoothed.skip (static_cast<int> (numSamples));

    const auto combinedBias = juce::jlimit (-maxCombinedBias, maxCombinedBias, warmthBias + explicitBias);
    const auto hissGain = hissAmount * maxHissLinearGain;

    *warmthLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (oversampledRate, warmthLowPassHz, filterQ);
    *tiltLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        oversampledRate, clampBelowNyquist (tiltLowShelfHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (-tiltDb));
    *tiltHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        oversampledRate, clampBelowNyquist (tiltHighShelfHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (tiltDb));
    *hfTrimShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        oversampledRate, clampBelowNyquist (hfTrimHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (hfTrimDb));
    *lfTrimShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        oversampledRate, clampBelowNyquist (lfTrimHz, oversampledRate), filterQ, juce::Decibels::decibelsToGain (lfTrimDb));
    dryWetMixer.setWetMixProportion (wetMix);

    juce::dsp::ProcessContextReplacing<float> context (workingBlock);

    // Capture the pre-processing signal as "dry" before any wet-path
    // processing touches `workingBlock`. DryWetMixer internally delays this
    // by getLatencySamples() (set via setWetLatency in prepare()) so it
    // stays time-aligned with the oversampled wet path below.
    dryWetMixer.pushDrySamples (workingBlock);

    // Wow/Flutter: modulated delay line at the host sample rate, ahead of
    // Drive/oversampling (see class comment). The depth (not the base
    // delay) scales with the smoothed amount, so amount == 0 degenerates to
    // a plain fixed delay of wowFlutterBaseDelaySamples - exactly what
    // getLatencySamples() already accounts for, keeping the null test valid
    // at the default (0%) amount without any special-casing here.
    {
        const auto wowDepthSamples = static_cast<double> (wowFlutterAmount) * wowFlutterMaxWowDepthSamples;
        const auto flutterDepthSamples = static_cast<double> (wowFlutterAmount) * wowFlutterMaxFlutterDepthSamples;

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            wowFlutterWowPhase += wowFlutterWowPhaseIncrement;
            if (wowFlutterWowPhase >= juce::MathConstants<double>::twoPi)
                wowFlutterWowPhase -= juce::MathConstants<double>::twoPi;

            wowFlutterFlutterPhase += wowFlutterFlutterPhaseIncrement;
            if (wowFlutterFlutterPhase >= juce::MathConstants<double>::twoPi)
                wowFlutterFlutterPhase -= juce::MathConstants<double>::twoPi;

            const auto modulationSamples = wowDepthSamples * std::sin (wowFlutterWowPhase)
                                            + flutterDepthSamples * std::sin (wowFlutterFlutterPhase);
            const auto delaySamples = wowFlutterBaseDelaySamples + modulationSamples;

            wowFlutterDelayLine.setDelay (static_cast<float> (delaySamples));

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = workingBlock.getChannelPointer (channel);
                const auto channelIndex = static_cast<int> (channel);

                wowFlutterDelayLine.pushSample (channelIndex, channelData[sample]);
                channelData[sample] = wowFlutterDelayLine.popSample (channelIndex);
            }
        }
    }

    driveGain.process (context);

    auto oversampledBlock = oversampler->processSamplesUp (workingBlock);
    juce::dsp::ProcessContextReplacing<float> oversampledContext (oversampledBlock);

    // Tape-style saturation stage: gentle pre-clip HF rolloff (models tape
    // bias/self-erasure dulling highs before the nonlinearity), then the
    // asymmetric saturator itself (Character-selected model, Warmth+Bias-
    // driven asymmetry).
    warmthLowPass.process (oversampledContext);

    for (size_t channel = 0; channel < oversampledBlock.getNumChannels(); ++channel)
    {
        auto* channelData = oversampledBlock.getChannelPointer (channel);

        for (size_t sample = 0; sample < oversampledBlock.getNumSamples(); ++sample)
            channelData[sample] = TapeSaturator::processSample (channelData[sample], combinedBias, character);
    }

    // Console-style tilt Tone, also run inside the oversampled domain
    // (immediately after the saturator, before downsampling).
    tiltLowShelf.process (oversampledContext);
    tiltHighShelf.process (oversampledContext);

    // HF/LF Trim: independent fixed-frequency shelves, after Tone so a
    // session can use Tone for the broad balance and Trim for a final touch
    // at the extremes.
    hfTrimShelf.process (oversampledContext);
    lfTrimShelf.process (oversampledContext);

    // Hiss: shaped noise mixed in last, inside the oversampled domain, so it
    // inherits the downsampler's anti-aliasing filter below rather than
    // needing its own explicit band-limiting filter here. Independent
    // per-channel noise (unlike Wow/Flutter's channel-linked modulation),
    // matching how tape hiss is typically decorrelated between the two
    // channels of a stereo recording.
    if (hissGain > 0.0f)
    {
        for (size_t channel = 0; channel < oversampledBlock.getNumChannels(); ++channel)
        {
            auto* channelData = oversampledBlock.getChannelPointer (channel);

            for (size_t sample = 0; sample < oversampledBlock.getNumSamples(); ++sample)
                channelData[sample] += (hissRandom.nextFloat() * 2.0f - 1.0f) * hissGain;
        }
    }

    oversampler->processSamplesDown (workingBlock);

    dryWetMixer.mixWetSamples (workingBlock);

    // Output is a final master trim applied after the dry/wet mix, so it
    // scales the combined (not just wet) signal.
    outputGain.process (context);
}
