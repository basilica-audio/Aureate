#pragma once

#include <juce_dsp/juce_dsp.h>

#include "TapeSaturator.h"

// The complete Aureate signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin (see tests/EngineTests.cpp). Owns all DSP state; every buffer/
// filter/oversampler/delay-line is allocated in prepare() and never
// reallocated on the audio thread.
//
// Signal flow (see docs/architecture.md for the full diagram and the
// latency-compensation rationale):
//
//   input -> Wow/Flutter (modulated delay) -> Drive
//         -> [4x oversampled: Warmth HF-rolloff -> saturator (Warmth+Bias,
//            Character-selected model) -> console tilt Tone -> HF/LF Trim
//            -> Hiss] -> downsample -> Dry/Wet mix -> Output trim
//
// The Warmth HF-rolloff filter, the saturator, the Tone tilt shelves, the
// HF/LF Trim shelves, and the Hiss noise stage all run inside the
// oversampled domain - this keeps their coefficient updates and the
// nonlinearity's/noise's harmonics all generated/filtered at 4x the host
// sample rate before a single downsample step, rather than filtering twice
// at two different rates (Hiss in particular benefits from inheriting the
// downsampler's anti-aliasing filter for free). Drive (pre-oversampling),
// Wow/Flutter (pre-oversampling), and Output (post-mix) run at the host
// sample rate.
//
// The dry path is delay-compensated against getLatencySamples() via
// juce::dsp::DryWetMixer, so Mix at 0% is a sample-accurate (once shifted by
// getLatencySamples()) passthrough of the input - this is what the plugin's
// null test (tests/EngineTests.cpp) exercises. Output is a final master trim
// applied after the dry/wet mix, so it affects the blended signal as a whole
// rather than only the wet path.
class AureateEngine
{
public:
    AureateEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/oversampler/delay-line state without deallocating.
    // Safe to call from the audio thread (e.g. on playback stop/loop).
    void reset();

    // Processes `block` in place. If `block` exceeds the sample/channel
    // counts declared to prepare(), it is defensively clamped to that
    // capacity - only the leading subset is processed in place, the
    // remainder of `block` is left untouched (see the .cpp for why: the
    // oversampler's internal buffers do not grow past what prepare() gave
    // them, so processing more would be a real out-of-bounds heap write in
    // a Release build - this is a host-contract violation the plugin
    // should never see, but is deliberately not undefined behaviour if it
    // does happen). A zero-sample block is a safe no-op. No allocation
    // occurs here.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units (dB, 0-1/-1-1 proportion, or model
    // index). Safe to call every block from the audio thread - no
    // allocation/locks. Drive and Output are smoothed by the underlying
    // juce::dsp::Gain ramp; Warmth/Tone/Mix/Bias/WowFlutter/Hiss/Trim are
    // smoothed internally and re-applied once per block (see process()).
    void setDriveDb (float newDriveDb);
    void setWarmthProportion (float newProportion01);
    void setToneProportion (float newProportionMinus1To1);
    void setMixProportion (float newProportion01);
    void setOutputDb (float newOutputDb);
    void setBiasProportion (float newProportionMinus1To1);
    void setWowFlutterProportion (float newProportion01);
    void setHissProportion (float newProportion01);
    void setCharacter (TapeSaturator::Model newModel);
    void setHfTrimDb (float newTrimDb);
    void setLfTrimDb (float newTrimDb);

    // Total added latency in samples (oversampling + the Wow/Flutter stage's
    // fixed base delay), valid after prepare() has run. This is deliberately
    // independent of the live Wow/Flutter *amount* (see prepare()), so it
    // never changes except when prepare() itself runs.
    int getLatencySamples() const noexcept { return latencySamples; }

private:
    static constexpr int oversamplingFactorPow2 = 2; // 2^2 = 4x oversampling
    static constexpr double smoothingTimeSeconds = 0.05;

    // Warmth mapping: 0% -> no audible rolloff (cutoff pinned high, near the
    // oversampled Nyquist) and a symmetric (unbiased) saturator; 100% ->
    // gentle darkening (cutoff down to 3 kHz) and maximally biased/
    // asymmetric saturation.
    static constexpr float warmthMinLowPassHz = 20000.0f;
    static constexpr float warmthMaxLowPassHz = 3000.0f;
    static constexpr float warmthMaxBias = 0.3f;

    // Tilt Tone mapping: +/-100% maps to +/-maxTiltDb at each shelf, in
    // opposite directions, so 0% leaves both shelves at 0 dB (flat/unity).
    static constexpr float maxTiltDb = 6.0f;
    static constexpr float tiltLowShelfHz = 200.0f;
    static constexpr float tiltHighShelfHz = 4000.0f;
    // Butterworth (maximally-flat) Q for the Warmth low-pass, the Tone
    // shelves, and the HF/LF Trim shelves.
    static constexpr float filterQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // Bias mapping: +/-100% maps to +/-maxExplicitBias, added directly to
    // Warmth's own bias contribution (see mapWarmthToBias in the .cpp). The
    // combined bias is clamped to +/-maxCombinedBias so automating both
    // controls to their extremes at once can't push the saturator into a
    // fully one-sided (musically broken) operating point.
    static constexpr float maxExplicitBias = 0.3f;
    static constexpr float maxCombinedBias = 0.9f;

    // HF/LF Trim: independent fixed-frequency shelves, deliberately at
    // different corner frequencies than Tone's tilt shelves (further out
    // towards the extremes) so the two controls feel distinct rather than
    // redundant - Tone reshapes the broad midrange balance, Trim tames/adds
    // air or weight at the very top/bottom.
    static constexpr float maxTrimDb = 6.0f;
    static constexpr float hfTrimHz = 8000.0f;
    static constexpr float lfTrimHz = 150.0f;

    // Hiss: linear (not dB-skewed) amount-to-gain mapping, so 0% is exactly
    // silent (no noise floor at all when off) rather than merely very quiet.
    // maxHissLinearGain is the peak per-sample noise amplitude at 100%,
    // chosen so full Hiss is clearly audible (a deliberate "vintage" option)
    // without ever approaching clipping headroom on its own.
    static constexpr float maxHissLinearGain = 0.006f; // ~ -44 dBFS peak at 100%

    // Wow/Flutter: a small, fixed base delay is always present (even at 0%
    // amount) so the stage's contribution to getLatencySamples() never
    // depends on the live parameter value - only prepare()-time constants -
    // which is what makes it safe to automate Wow/Flutter during playback
    // without the plugin's reported latency changing underneath the host.
    // Depths/rates are deliberately not extreme: this emulates gentle tape-
    // transport instability, not a chorus/vibrato effect.
    static constexpr float wowFlutterBaseDelayMs = 6.0f;
    static constexpr float wowFlutterMaxWowDepthMs = 2.5f;
    static constexpr float wowFlutterMaxFlutterDepthMs = 0.35f;
    static constexpr float wowFlutterWowRateHz = 0.7f;
    static constexpr float wowFlutterFlutterRateHz = 6.5f;

    double sampleRate = 44100.0;
    double oversampledRate = 44100.0;

    // The sample/channel counts declared to prepare() - everything
    // downstream (most importantly the oversampler's internal per-stage
    // buffers, see juce::dsp::Oversampling::initProcessing()) is sized from
    // these and does not grow later, so process() clamps to them
    // defensively (see process()'s doc comment and issue #13).
    size_t preparedMaximumBlockSize = 0;
    size_t preparedNumChannels = 0;

    // Runs at the host sample rate, before Drive/oversampling (see class
    // comment) - modulates the wet path's transport delay by a sum of two
    // sine LFOs ("wow" and "flutter") scaled by the smoothed amount.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> wowFlutterDelayLine;
    double wowFlutterBaseDelaySamples = 0.0;
    double wowFlutterMaxWowDepthSamples = 0.0;
    double wowFlutterMaxFlutterDepthSamples = 0.0;
    double wowFlutterWowPhase = 0.0;
    double wowFlutterFlutterPhase = 0.0;
    double wowFlutterWowPhaseIncrement = 0.0;
    double wowFlutterFlutterPhaseIncrement = 0.0;

    juce::dsp::Gain<float> driveGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // These all run inside the oversampled block (see class comment).
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> warmthLowPass;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tiltLowShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tiltHighShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> hfTrimShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lfTrimShelf;

    // Deterministic (fixed-seed) so Hiss's contribution is bit-reproducible
    // across runs, which keeps its own tests non-flaky; real-time-safe
    // (juce::Random::nextFloat() does not allocate or lock).
    juce::Random hissRandom { 1 };

    juce::dsp::Gain<float> outputGain;

    // Sized generously above getLatencySamples()'s worst case across the
    // whole supported sample-rate range, so setWetLatency() never exceeds
    // the mixer's internal delay-line capacity. getLatencySamples() is now
    // the sum of the oversampler's latency (tens of samples) and
    // Wow/Flutter's fixed base delay (wowFlutterBaseDelayMs, which scales
    // with sample rate - up to ~1152 samples at 192 kHz); a too-small
    // capacity here was previously observed to silently corrupt (or, in a
    // Debug build, assert inside juce::dsp::DelayLine::setDelay) the dry-
    // path delay compensation at high sample rates once Wow/Flutter's base
    // delay was added to getLatencySamples().
    juce::dsp::DryWetMixer<float> dryWetMixer { 8192 };

    // Warmth's low-pass cutoff uses multiplicative smoothing (appropriate
    // for a quantity perceived logarithmically, like Hz); everything else
    // below uses linear smoothing.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> warmthLowPassHzSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> warmthBiasSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> explicitBiasSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tiltDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wowFlutterAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hissAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfTrimDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lfTrimDbSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied to the smoothers on every prepare() so re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // default or lets a smoother start from an invalid 0 Hz.
    float lastWarmthProportion01 = 0.35f;
    float lastTiltProportion = 0.0f;
    float lastMixProportion = 1.0f;
    float lastBiasProportion = 0.0f;
    float lastWowFlutterProportion01 = 0.0f;
    float lastHissProportion01 = 0.0f;
    float lastHfTrimDb = 0.0f;
    float lastLfTrimDb = 0.0f;

    TapeSaturator::Model character = TapeSaturator::Model::tape;

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateEngine)
};
