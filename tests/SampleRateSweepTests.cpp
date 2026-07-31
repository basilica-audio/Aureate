#include "PluginProcessor.h"
#include "dsp/AureateEngine.h"
#include "dsp/GlueCompressor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Broadens coverage across the sample-rate range hosts commonly offer
// (44.1-192 kHz), per the M1 "broaden test coverage" issue. The existing
// 48 kHz-only null test (tests/EngineTests.cpp) stays as-is; this file
// re-runs the same "0% mix nulls against input, once shifted by latency"
// property at every rate in the sweep, plus basic latency/finiteness sanity
// checks, so a sample-rate-dependent regression (e.g. in the Wow/Flutter
// base-delay rounding, or the oversampler's rate-dependent behaviour) can't
// slip through unnoticed just because it only manifests away from 48 kHz.
namespace
{
    constexpr double testFrequencyHz = 1000.0;

    // 8192 samples is comfortably larger than any latency in this sweep
    // (oversampling latency plus the few-millisecond Wow/Flutter base delay)
    // even at the highest rate (192 kHz), while still running well under a
    // second of audio per rate in a Debug build.
    constexpr int testBlockSize = 8192;

    juce::dsp::ProcessSpec makeSpec (double sampleRate, int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    constexpr double sweepRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    void setParam (AureateAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Sample-rate sweep: engine null test (0% mix) holds at every rate from 44.1 to 192 kHz", "[dsp][engine][null][samplerate]")
{
    for (const auto rate : sweepRates)
    {
        CAPTURE (rate);

        AureateEngine engine;
        engine.setMixProportion (0.0f);
        engine.setDriveDb (18.0f);
        engine.setWarmthProportion (0.8f);
        engine.setToneProportion (0.6f);
        engine.setOutputDb (0.0f); // post-mix master trim must stay at unity for a true null test

        const auto spec = makeSpec (rate, 2);
        engine.prepare (spec);

        const auto latency = engine.getLatencySamples();
        REQUIRE (latency >= 0);
        REQUIRE (latency < testBlockSize / 2);

        juce::AudioBuffer<float> reference (2, testBlockSize);
        TestHelpers::fillWithSine (reference, rate, testFrequencyHz, 0.5f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        const auto overlapLength = testBlockSize - latency;
        REQUIRE (overlapLength > testBlockSize / 2);

        constexpr float tolerance = 3.1623e-5f; // < -90 dBFS, in linear amplitude

        for (int channel = 0; channel < reference.getNumChannels(); ++channel)
        {
            const auto* refData = reference.getReadPointer (channel);
            const auto* outData = processed.getReadPointer (channel);

            float maxResidual = 0.0f;
            for (int i = 0; i < overlapLength; ++i)
                maxResidual = std::max (maxResidual, std::abs (outData[latency + i] - refData[i]));

            CHECK (maxResidual < tolerance);
        }
    }
}

TEST_CASE ("Sample-rate sweep: latency is a well-defined positive integer at every rate", "[dsp][engine][latency][samplerate]")
{
    for (const auto rate : sweepRates)
    {
        CAPTURE (rate);

        AureateEngine engine;
        engine.prepare (makeSpec (rate, 2));

        CHECK (engine.getLatencySamples() > 0);
        CHECK (engine.getLatencySamples() < testBlockSize / 2);
    }
}

TEST_CASE ("Sample-rate sweep: full-chain processing at maximum drive/warmth/bias/hiss/wow-flutter stays finite at every rate",
           "[dsp][engine][robustness][samplerate]")
{
    for (const auto rate : sweepRates)
    {
        CAPTURE (rate);

        AureateEngine engine;
        engine.setDriveDb (24.0f);
        engine.setWarmthProportion (1.0f);
        engine.setToneProportion (1.0f);
        engine.setBiasProportion (1.0f);
        engine.setWowProportion (1.0f);
        engine.setFlutterProportion (1.0f);
        engine.setHissProportion (1.0f);
        engine.setHfTrimDb (6.0f);
        engine.setLfTrimDb (6.0f);
        engine.setMixProportion (1.0f);
        engine.setOutputDb (24.0f);
        engine.prepare (makeSpec (rate, 2));

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        TestHelpers::fillWithSine (buffer, rate, testFrequencyHz, 1.0f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        CHECK (TestHelpers::allSamplesFinite (buffer));
        CHECK (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }
}

TEST_CASE ("Sample-rate sweep: AureateAudioProcessor::prepareToPlay reports consistent, positive latency at every rate",
           "[processor][latency][samplerate]")
{
    AureateAudioProcessor processor;

    for (const auto rate : sweepRates)
    {
        CAPTURE (rate);

        processor.prepareToPlay (rate, 512);
        CHECK (processor.getLatencySamples() > 0);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, rate, testFrequencyHz, 0.5f);
        juce::MidiBuffer midi;

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

//==============================================================================
// 6.7 - the Vari-Mu timing network is sample-rate independent
//==============================================================================
TEST_CASE ("6.7 The Glue section's gain-reduction trace is the same in absolute time at every sample rate",
           "[dsp][glue][samplerate][varimu]")
{
    // The three-capacitor network is discretised trapezoidally with its
    // coefficients rebuilt per sample rate at prepare(), so a release that
    // takes two seconds must take two seconds at 44.1 kHz and at 192 kHz
    // alike. A network discretised naively (or one whose rates were baked in
    // as per-sample coefficients) would drift by a factor of four across this
    // sweep.
    auto measureRecoverySeconds = [] (double sampleRate, GlueCompressor::Law law, int releaseIndex)
    {
        GlueCompressor compressor;
        compressor.setEnabled (true);
        compressor.setLaw (law);
        compressor.setThresholdDb (0.0f);
        compressor.setRatioIndex (1);
        compressor.setAttackIndex (2);
        compressor.setReleaseIndex (releaseIndex);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 1;
        spec.numChannels = 2;
        compressor.prepare (spec);

        const auto amplitude = static_cast<float> (std::pow (10.0, (-18.0 + 15.0) / 20.0)
                                                    * juce::MathConstants<double>::sqrt2);

        const auto sustainSamples = static_cast<int> (2.0 * sampleRate);
        const auto decaySamples = static_cast<int> (15.0 * sampleRate);

        juce::AudioBuffer<float> buffer (2, 1);
        float peak = 0.0f;

        for (int sample = 0; sample < sustainSamples; ++sample)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 1000.0
                                * static_cast<double> (sample) / sampleRate;
            const auto value = amplitude * static_cast<float> (std::sin (phase));

            buffer.setSample (0, 0, value);
            buffer.setSample (1, 0, value);

            juce::dsp::AudioBlock<float> block (buffer);
            compressor.process (block);
        }

        peak = compressor.getCurrentGrDb();

        for (int sample = 0; sample < decaySamples; ++sample)
        {
            buffer.setSample (0, 0, 0.0f);
            buffer.setSample (1, 0, 0.0f);

            juce::dsp::AudioBlock<float> block (buffer);
            compressor.process (block);

            if (compressor.getCurrentGrDb() <= peak * 0.36788f)
                return sample / sampleRate;
        }

        return -1.0;
    };

    for (const auto law : { GlueCompressor::Law::vca, GlueCompressor::Law::variMu })
    {
        for (const auto releaseIndex : { 1, 3 })
        {
            const auto reference = measureRecoverySeconds (48000.0, law, releaseIndex);
            REQUIRE (reference > 0.0);

            for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
            {
                const auto measured = measureRecoverySeconds (sampleRate, law, releaseIndex);
                REQUIRE (measured > 0.0);

                INFO ("law " << static_cast<int> (law) << ", release position " << (releaseIndex + 1)
                      << ": " << reference << " s at 48 kHz vs " << measured << " s at " << sampleRate << " Hz");
                CHECK (measured == Catch::Approx (reference).epsilon (0.02));
            }
        }
    }
}

//==============================================================================
// Suite-wide hardening wave: sample-rate matrix reprepare.
//
// Broader than "prepareToPlay reports consistent, positive latency at every
// rate" above: that test only sweeps sample rate at a single fixed block
// size and bus layout. This one drives one processor instance through a
// full 44.1k -> 96k -> 192k reprepare matrix, crossing small AND large
// block sizes and mono/stereo bus layouts along the way, with
// automation-like parameter churn between reprepares. Every Aureate
// parameter used here is fully automatable and latency-independent
// (AureateEngine::getLatencySamples() depends only on sampleRate - see
// AureateEngine.cpp/.h), so unlike sibling plugins tenebrae/seraph this
// test does not need to hold any control back. Deterministic and block
// counts kept small so this stays well under 30s even on Debug/CI.
TEST_CASE ("Sample-rate matrix reprepare: 44.1k -> 96k -> 192k across block sizes and bus "
           "layouts survives parameter automation and reports correct latency every time",
           "[processor][robustness][samplerate][reprepare]")
{
    AureateAudioProcessor processor;
    juce::MidiBuffer midi;

    setParam (processor, ParamIDs::drive, 15.0f);
    setParam (processor, ParamIDs::warmth, 55.0f);
    setParam (processor, ParamIDs::tone, -30.0f);
    setParam (processor, ParamIDs::bias, 20.0f);
    setParam (processor, ParamIDs::output, -3.0f);
    setParam (processor, ParamIDs::mix, 85.0f);

    auto* driveParam = processor.apvts.getParameter (ParamIDs::drive);
    REQUIRE (driveParam != nullptr);

    // Tracks what Drive's value ought to be at the start of each iteration -
    // seeded from the setParam() above, then updated to the last value the
    // automation loop below left it at, so each reprepare's "did the value
    // survive" check is against ground truth rather than a stale constant.
    auto expectedDriveValue = driveParam->convertFrom0to1 (driveParam->getValue());

    struct Step
    {
        double sampleRate;
        int blockSize;
        int numChannels;
    };

    // Small AND large blocks at both 96k and 192k, plus a mono layout
    // change thrown in at 192k (Aureate supports mono -
    // isBusesLayoutSupported() accepts mono or stereo in == out) to make
    // sure a channel-count change riding along with a sample-rate reprepare
    // doesn't trip anything up.
    static constexpr Step steps[] = {
        { 44100.0,  32,   2 },
        { 96000.0,  32,   2 },
        { 96000.0,  2048, 2 },
        { 192000.0, 32,   1 },
        { 192000.0, 2048, 2 },
    };

    for (const auto& step : steps)
    {
        if (step.numChannels == 1)
        {
            juce::AudioProcessor::BusesLayout monoLayout;
            monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
            monoLayout.outputBuses.add (juce::AudioChannelSet::mono());
            REQUIRE (processor.setBusesLayout (monoLayout));
        }
        else
        {
            juce::AudioProcessor::BusesLayout stereoLayout;
            stereoLayout.inputBuses.add (juce::AudioChannelSet::stereo());
            stereoLayout.outputBuses.add (juce::AudioChannelSet::stereo());
            REQUIRE (processor.setBusesLayout (stereoLayout));
        }

        processor.prepareToPlay (step.sampleRate, step.blockSize);

        // Latency must be reported (and positive - the oversampler plus
        // Wow/Flutter's base delay always add some) after every single
        // reprepare in the matrix, not just the first one.
        CHECK (processor.getLatencySamples() > 0);

        // State survival: prepareToPlay() must never reset APVTS parameter
        // values, at any sample rate/block-size/layout combination.
        CHECK (driveParam->convertFrom0to1 (driveParam->getValue())
               == Catch::Approx (expectedDriveValue).margin (0.01f));

        juce::AudioBuffer<float> buffer (step.numChannels, step.blockSize);

        for (int block = 0; block < 4; ++block)
        {
            // Automation-like parameter churn while processing, mimicking a
            // host sweeping controls mid-stream between reprepares.
            const auto sweep = static_cast<float> (block) / 4.0f;
            expectedDriveValue = 3.0f + sweep * 18.0f;
            setParam (processor, ParamIDs::drive, expectedDriveValue);
            setParam (processor, ParamIDs::warmth, sweep * 100.0f);
            setParam (processor, ParamIDs::tone, -80.0f + sweep * 160.0f);
            setParam (processor, ParamIDs::wow, sweep * 100.0f);
            setParam (processor, ParamIDs::flutter, sweep * 100.0f);

            TestHelpers::fillWithSine (buffer, step.sampleRate, testFrequencyHz, 0.6f,
                                       static_cast<juce::int64> (block) * step.blockSize);

            CHECK_NOTHROW (processor.processBlock (buffer, midi));
            CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }
}
