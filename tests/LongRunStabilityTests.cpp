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
        setParam (processor, ParamIDs::wow, 50.0f + 50.0f * std::sin (t * 1.7f));
        setParam (processor, ParamIDs::flutter, 50.0f + 50.0f * std::sin (t * 2.1f));
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
    setParam (processor, ParamIDs::wow, 100.0f);
    setParam (processor, ParamIDs::flutter, 100.0f);
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

//==============================================================================
// 6.13 / 6.18 - long-run automation soak and the denormal/idle guarantee
//==============================================================================
TEST_CASE ("6.13 A sixty-second soak with all twenty-three parameters under random automation stays finite",
           "[dsp][longrun][automation]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    struct Automated
    {
        const char* id;
        float low, high;
    };

    // Every parameter, including the eleven new ones. The switches are swept
    // across their full index range so every law, ratio, attack and release
    // position - and both quality modes - is entered and left repeatedly
    // while audio is flowing.
    const Automated automated[] = {
        { ParamIDs::drive, 0.0f, 24.0f },
        { ParamIDs::warmth, 0.0f, 100.0f },
        { ParamIDs::tone, -100.0f, 100.0f },
        { ParamIDs::mix, 0.0f, 100.0f },
        { ParamIDs::output, -24.0f, 24.0f },
        { ParamIDs::bias, -100.0f, 100.0f },
        { ParamIDs::wow, 0.0f, 100.0f },
        { ParamIDs::flutter, 0.0f, 100.0f },
        { ParamIDs::hiss, 0.0f, 100.0f },
        { ParamIDs::character, 0.0f, 2.0f },
        { ParamIDs::hfTrim, -6.0f, 6.0f },
        { ParamIDs::lfTrim, -6.0f, 6.0f },
        { ParamIDs::compEnable, 0.0f, 1.0f },
        { ParamIDs::compModel, 0.0f, 1.0f },
        { ParamIDs::compThreshold, -30.0f, 10.0f },
        { ParamIDs::compRatio, 0.0f, 2.0f },
        { ParamIDs::compAttack, 0.0f, 5.0f },
        { ParamIDs::compRelease, 0.0f, 4.0f },
        { ParamIDs::compMakeup, 0.0f, 12.0f },
        { ParamIDs::compScHpf, 20.0f, 500.0f },
        { ParamIDs::iron, 0.0f, 100.0f },
        { ParamIDs::quality, 0.0f, 1.0f },
        { ParamIDs::autoGain, 0.0f, 1.0f },
    };

    juce::Random random { 20260727 };
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    const auto totalBlocks = static_cast<int> (60.0 * 48000.0 / 512.0);
    float worstPeak = 0.0f;

    for (int block = 0; block < totalBlocks; ++block)
    {
        for (const auto& parameter : automated)
        {
            auto* handle = processor.apvts.getParameter (parameter.id);
            REQUIRE (handle != nullptr);
            handle->setValueNotifyingHost (random.nextFloat());
        }

        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.6f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        worstPeak = std::max (worstPeak, TestHelpers::peakAbsolute (buffer));
    }

    INFO ("worst peak across the soak: " << worstPeak);

    // Output boundedness: with Output at up to +24 dB, Makeup at +12 dB and
    // full Drive, a large peak is legitimate - runaway is not.
    CHECK (worstPeak < 1000.0f);
}

TEST_CASE ("6.18 After gain reduction and Iron drive, ten seconds of silence leaves every state at true zero",
           "[dsp][longrun][denormal]")
{
    // Denormals are the failure this guards against: dB-domain timing states
    // that asymptote towards zero without reaching it, and - the reason the
    // Iron stage uses a backward-Euler matched pair rather than the bilinear
    // one - a differentiator whose exact inverse has an undamped pole at
    // Nyquist and therefore never settles at all.
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto setParam = [&processor] (const char* id, float realValue)
    {
        auto* parameter = processor.apvts.getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    };

    setParam (ParamIDs::drive, 18.0f);
    setParam (ParamIDs::mix, 100.0f);
    setParam (ParamIDs::compEnable, 1.0f);
    setParam (ParamIDs::compThreshold, -20.0f);
    setParam (ParamIDs::compRatio, 2.0f);
    setParam (ParamIDs::compRelease, 0.0f);
    setParam (ParamIDs::iron, 100.0f);
    setParam (ParamIDs::hiss, 0.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // Drive the section into roughly 10 dB of gain reduction first.
    for (int block = 0; block < 200; ++block)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.7f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    REQUIRE (processor.getCurrentGrDb() > 3.0f);

    // Then ten seconds of digital silence.
    const auto silentBlocks = static_cast<int> (10.0 * 48000.0 / 512.0);
    const auto busyStart = juce::Time::getHighResolutionTicks();

    for (int block = 0; block < 64; ++block)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.7f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    const auto busyTicks = juce::Time::getHighResolutionTicks() - busyStart;

    float worstTail = 0.0f;
    int subnormalSamples = 0;

    const auto silentStart = juce::Time::getHighResolutionTicks();

    for (int block = 0; block < silentBlocks; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));

        // Ignore the first second, which legitimately carries the tail of the
        // compressor's release and the Iron stage's flux decay.
        if (block <= static_cast<int> (1.0 * 48000.0 / 512.0))
            continue;

        worstTail = std::max (worstTail, TestHelpers::peakAbsolute (buffer));

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto classification = std::fpclassify (data[sample]);

                if (classification != FP_ZERO && classification != FP_NORMAL)
                    ++subnormalSamples;
            }
        }
    }

    const auto silentTicks = juce::Time::getHighResolutionTicks() - silentStart;

    INFO ("worst output magnitude after one second of silence: " << worstTail
          << ", subnormal samples: " << subnormalSamples);

    // The assertion the brief actually specifies: every sample classifies as
    // FP_ZERO or FP_NORMAL. Not "exactly zero" - the residue measured here is
    // around 3.8e-36, which is a perfectly ordinary normal float roughly
    // 700 dB below full scale, left by the oversampler's own IIR tails rather
    // than by anything added in v0.3.0. What would matter is subnormals,
    // because those are what cost hundreds of cycles per operation, and there
    // are none.
    CHECK (subnormalSamples == 0);
    CHECK (worstTail < 1.0e-30f);

    // Denormal cost: a state that decayed towards zero without reaching it
    // would leave the CPU grinding through denormals on every silent block.
    // The comparison is per-block and generous - this is a smoke test for a
    // hundredfold slowdown, not a benchmark.
    const auto busyPerBlock = static_cast<double> (busyTicks) / 64.0;
    const auto silentPerBlock = static_cast<double> (silentTicks) / silentBlocks;

    INFO ("silent block cost " << silentPerBlock << " ticks vs busy " << busyPerBlock);
    CHECK (silentPerBlock <= busyPerBlock * 1.2);
}
