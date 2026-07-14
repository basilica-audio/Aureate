#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

// Long-run NaN/Inf stability coverage per the M1 "broaden test coverage"
// issue: many seconds of continuous processing (not just a handful of
// blocks, as in tests/RobustnessTests.cpp) with slowly sweeping parameters
// covering every M1 DSP addition (Bias, Wow/Flutter, Hiss, Character,
// HF/LF Trim) alongside the v0.1 core controls, checking that no IIR filter
// state, the oversampler, the modulated delay line, or the noise generator
// ever accumulates into a NaN/Inf or a runaway (unbounded) output over a
// sustained run. Kept to ~4 seconds of audio at a moderate block size so it
// stays comfortably fast under a Debug build, including on Windows CI.
namespace
{
    void setParam (AureateAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Long-run stability: several seconds of continuous processing with slowly sweeping parameters stays finite and bounded",
           "[robustness][longrun]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 400; // ~4.27 s of audio at 48 kHz/512-sample blocks

    AureateAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    juce::MidiBuffer midi;
    juce::int64 sampleIndex = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        // Slowly sweep every control across its full range over the run,
        // each on its own (deliberately mutually prime-ish) period so their
        // combined phase relationship keeps changing rather than falling
        // into a repeating short cycle.
        const auto t = static_cast<float> (block) / static_cast<float> (numBlocks);

        setParam (processor, ParamIDs::drive, 12.0f + 12.0f * std::sin (t * 6.0f));
        setParam (processor, ParamIDs::warmth, 50.0f + 50.0f * std::sin (t * 4.3f));
        setParam (processor, ParamIDs::tone, 100.0f * std::sin (t * 3.1f));
        setParam (processor, ParamIDs::bias, 100.0f * std::sin (t * 2.2f));
        setParam (processor, ParamIDs::wowFlutter, 50.0f + 50.0f * std::sin (t * 1.7f));
        setParam (processor, ParamIDs::hiss, 50.0f + 50.0f * std::sin (t * 5.9f));
        setParam (processor, ParamIDs::hfTrim, 6.0f * std::sin (t * 3.7f));
        setParam (processor, ParamIDs::lfTrim, 6.0f * std::sin (t * 2.9f));
        setParam (processor, ParamIDs::mix, 50.0f + 50.0f * std::sin (t * 4.9f));
        setParam (processor, ParamIDs::output, 6.0f * std::sin (t * 1.3f));

        // Character switches between models partway through the run - the
        // sharpest possible discontinuity this parameter can produce,
        // exercising a mid-stream model change under otherwise-live signal.
        setParam (processor, ParamIDs::character, static_cast<float> (block % 3));

        juce::AudioBuffer<float> buffer (2, blockSize);
        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.7f, sampleIndex);
        sampleIndex += blockSize;

        CHECK_NOTHROW (processor.processBlock (buffer, midi));

        INFO ("block " << block);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }
}

TEST_CASE ("Long-run stability: silence held for several seconds at maximum Hiss/Warmth/Bias stays finite and bounded",
           "[robustness][longrun]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    constexpr int numBlocks = 600; // ~3.48 s of audio at 44.1 kHz/256-sample blocks

    AureateAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    setParam (processor, ParamIDs::drive, 24.0f);
    setParam (processor, ParamIDs::warmth, 100.0f);
    setParam (processor, ParamIDs::bias, 100.0f);
    setParam (processor, ParamIDs::hiss, 100.0f);
    setParam (processor, ParamIDs::wowFlutter, 100.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();

        CHECK_NOTHROW (processor.processBlock (buffer, midi));

        INFO ("block " << block);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        // Silence plus a modest Hiss noise floor must never run away.
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 1.0f);
    }
}

TEST_CASE ("Long-run stability: rapid Character switching every block for several seconds does not destabilise filter state",
           "[robustness][longrun][character]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 300; // ~3.2 s of audio at 48 kHz/512-sample blocks

    AureateAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    setParam (processor, ParamIDs::drive, 20.0f);
    setParam (processor, ParamIDs::warmth, 80.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::MidiBuffer midi;
    juce::int64 sampleIndex = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        setParam (processor, ParamIDs::character, static_cast<float> (block % 3));

        juce::AudioBuffer<float> buffer (2, blockSize);
        TestHelpers::fillWithSine (buffer, sampleRate, 440.0, 0.6f, sampleIndex);
        sampleIndex += blockSize;

        CHECK_NOTHROW (processor.processBlock (buffer, midi));

        INFO ("block " << block);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }
}
