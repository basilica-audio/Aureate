#include "PluginProcessor.h"
#include "dsp/AureateEngine.h"
#include "params/ParameterIds.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("getLatencySamples() reports the oversampling latency after prepareToPlay", "[latency]")
{
    AureateAudioProcessor processor;

    // Before prepareToPlay, no engine has been prepared yet - JUCE's default
    // AudioProcessor latency is 0.
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    // Cross-check against a standalone engine prepared identically: the
    // processor must report exactly what the engine (i.e. the oversampler)
    // computes, not an approximation of it.
    AureateEngine referenceEngine;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    referenceEngine.prepare (spec);

    CHECK (processor.getLatencySamples() == referenceEngine.getLatencySamples());
    CHECK (processor.getLatencySamples() > 0); // 4x oversampling always has some latency
}

TEST_CASE ("Latency is stable across repeated prepareToPlay calls at the same sample rate", "[latency]")
{
    AureateAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    const auto firstLatency = processor.getLatencySamples();

    processor.prepareToPlay (44100.0, 256);
    const auto secondLatency = processor.getLatencySamples();

    CHECK (firstLatency == secondLatency);
}

TEST_CASE ("Latency updates correctly when the sample rate changes", "[latency]")
{
    AureateAudioProcessor processor;

    processor.prepareToPlay (44100.0, 512);
    const auto latencyAt44k = processor.getLatencySamples();

    processor.prepareToPlay (96000.0, 512);
    const auto latencyAt96k = processor.getLatencySamples();

    CHECK (latencyAt44k > 0);
    CHECK (latencyAt96k > 0);
    // Not asserting a specific ratio (that depends on JUCE's internal
    // half-band filter design), just that both are well-defined positive
    // latencies reported consistently.
}

//==============================================================================
// 6.14 - the v0.3.0 stages add no latency, at any setting or sample rate
//==============================================================================
TEST_CASE ("6.14 Reported latency is identical at every combination of the new parameters and every sample rate",
           "[latency][neutrality]")
{
    // This is the reason the Glue section runs at the host rate and the Iron
    // stage lives inside the EXISTING oversampled region: neither is allowed
    // to move getLatencySamples(), because that number is also what the
    // DryWetMixer compensates the dry path by. A stage that added latency
    // would silently break the Mix-at-0% null for every existing session.
    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        AureateAudioProcessor reference;
        reference.prepareToPlay (sampleRate, 512);
        const auto baseline = reference.getLatencySamples();

        REQUIRE (baseline > 0);

        auto setParam = [] (AureateAudioProcessor& processor, const char* id, float realValue)
        {
            auto* parameter = processor.apvts.getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
        };

        struct Combination
        {
            const char* description;
            float compEnable, compModel, iron, quality, autoGain;
        };

        const Combination combinations[] = {
            { "Glue on, VCA law",        1.0f, 0.0f, 0.0f,   0.0f, 0.0f },
            { "Glue on, Vari-Mu law",    1.0f, 1.0f, 0.0f,   0.0f, 0.0f },
            { "Iron at 100%",            0.0f, 0.0f, 100.0f, 0.0f, 0.0f },
            { "HQ quality",              0.0f, 0.0f, 0.0f,   1.0f, 0.0f },
            { "Auto Gain on",            0.0f, 0.0f, 0.0f,   0.0f, 1.0f },
            { "everything at once",      1.0f, 1.0f, 100.0f, 1.0f, 1.0f },
        };

        for (const auto& combination : combinations)
        {
            AureateAudioProcessor processor;

            setParam (processor, ParamIDs::compEnable, combination.compEnable);
            setParam (processor, ParamIDs::compModel, combination.compModel);
            setParam (processor, ParamIDs::iron, combination.iron);
            setParam (processor, ParamIDs::quality, combination.quality);
            setParam (processor, ParamIDs::autoGain, combination.autoGain);

            processor.prepareToPlay (sampleRate, 512);

            INFO ("at " << sampleRate << " Hz with " << combination.description);
            CHECK (processor.getLatencySamples() == baseline);
        }
    }
}
