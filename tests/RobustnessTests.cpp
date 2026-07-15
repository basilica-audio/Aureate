#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>

namespace
{
    void setParam (AureateAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 24.0f);
    setParam (processor, ParamIDs::warmth, 100.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Full-scale input at maximum drive produces no NaN/Inf", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 24.0f);
    setParam (processor, ParamIDs::warmth, 100.0f);
    setParam (processor, ParamIDs::tone, 100.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    setParam (processor, ParamIDs::output, 24.0f);
    setParam (processor, ParamIDs::bias, 100.0f);
    setParam (processor, ParamIDs::wowFlutter, 100.0f);
    setParam (processor, ParamIDs::hiss, 100.0f);
    setParam (processor, ParamIDs::character, 2.0f); // Valve
    setParam (processor, ParamIDs::hfTrim, 6.0f);
    setParam (processor, ParamIDs::lfTrim, 6.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 100.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 20.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("processBlock() with a block larger than samplesPerBlock does not process past the prepared capacity", "[robustness]")
{
    AureateAudioProcessor processor;

    constexpr int preparedBlockSize = 64;
    processor.prepareToPlay (48000.0, preparedBlockSize);

    setParam (processor, ParamIDs::drive, 12.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    // Deliberately larger than what prepareToPlay() declared - a host
    // contract violation, but juce::dsp::Oversampling's internal per-stage
    // buffers (sized from samplesPerBlock at prepareToPlay() time, see
    // AureateEngine::prepare()) do NOT grow for a later, bigger block, so
    // processing it without a guard is a real out-of-bounds heap write in a
    // Release build (issue #13).
    constexpr int oversizedBlockSize = preparedBlockSize * 4;

    juce::AudioBuffer<float> buffer (2, oversizedBlockSize);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    juce::AudioBuffer<float> beforeProcessing;
    beforeProcessing.makeCopyOf (buffer);

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));

    // Everything from preparedBlockSize onward must be left exactly as it
    // was: the engine must only touch the sample range it was actually
    // prepared for, not silently process (and potentially corrupt) the full
    // oversized block. Before the fix, the whole (oversized) block flows
    // through Drive/Wow-Flutter/the oversampler/Dry-Wet-Mix/Output, so this
    // tail region gets overwritten too.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* before = beforeProcessing.getReadPointer (channel);
        const auto* after = buffer.getReadPointer (channel);

        for (int sample = preparedBlockSize; sample < oversizedBlockSize; ++sample)
            CHECK (juce::exactlyEqual (after[sample], before[sample]));
    }
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::drive, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::warmth, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::tone, useMinimum ? -100.0f : 100.0f);
        setParam (processor, ParamIDs::mix, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::output, useMinimum ? -24.0f : 24.0f);
        setParam (processor, ParamIDs::bias, useMinimum ? -100.0f : 100.0f);
        setParam (processor, ParamIDs::wowFlutter, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::hiss, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::character, useMinimum ? 0.0f : 2.0f);
        setParam (processor, ParamIDs::hfTrim, useMinimum ? -6.0f : 6.0f);
        setParam (processor, ParamIDs::lfTrim, useMinimum ? -6.0f : 6.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::drive, unit (rng) * 24.0f);
        setParam (processor, ParamIDs::warmth, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::tone, -100.0f + unit (rng) * 200.0f);
        setParam (processor, ParamIDs::mix, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::output, -24.0f + unit (rng) * 48.0f);
        setParam (processor, ParamIDs::bias, -100.0f + unit (rng) * 200.0f);
        setParam (processor, ParamIDs::wowFlutter, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::hiss, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::character, std::floor (unit (rng) * 3.0f));
        setParam (processor, ParamIDs::hfTrim, -6.0f + unit (rng) * 12.0f);
        setParam (processor, ParamIDs::lfTrim, -6.0f + unit (rng) * 12.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 18.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}
