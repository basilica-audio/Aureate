#include "PluginProcessor.h"
#include "dsp/AureateEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

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
        engine.setWowFlutterProportion (1.0f);
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
