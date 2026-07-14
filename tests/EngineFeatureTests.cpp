#include "dsp/AureateEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Engine-level tests for the M1 DSP additions beyond the v0.1 core: Bias,
// Character, Hiss, and HF/LF Trim. TapeSaturatorTests.cpp covers the
// Character curves themselves in isolation; these tests exercise them
// wired up through the full oversampled AureateEngine signal path.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;
    constexpr double testFrequencyHz = 1000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    float peakPositive (const juce::AudioBuffer<float>& buffer, int channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        float peak = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::max (peak, data[i]);
        return peak;
    }

    float peakNegative (const juce::AudioBuffer<float>& buffer, int channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        float peak = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::min (peak, data[i]);
        return peak;
    }
}

//==============================================================================
// Bias

TEST_CASE ("Bias: at Warmth 0% (symmetric saturator), a nonzero Bias still produces asymmetric ceilings", "[dsp][engine][bias]")
{
    AureateEngine engine;
    engine.setDriveDb (18.0f);
    engine.setWarmthProportion (0.0f); // no bias contribution from Warmth
    engine.setBiasProportion (1.0f); // fully positive Bias
    engine.setMixProportion (1.0f);
    engine.setOutputDb (0.0f);
    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto positive = peakPositive (buffer, 0);
    const auto negative = std::abs (peakNegative (buffer, 0));

    CHECK (positive != Catch::Approx (negative).margin (0.02));
}

TEST_CASE ("Bias: at Warmth 0% and Bias 0%, the saturator stays symmetric", "[dsp][engine][bias]")
{
    AureateEngine engine;
    engine.setDriveDb (18.0f);
    engine.setWarmthProportion (0.0f);
    engine.setBiasProportion (0.0f);
    engine.setMixProportion (1.0f);
    engine.setOutputDb (0.0f);
    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto positive = peakPositive (buffer, 0);
    const auto negative = std::abs (peakNegative (buffer, 0));

    CHECK (positive == Catch::Approx (negative).margin (0.02));
}

TEST_CASE ("Bias: positive and negative Bias settings produce mirrored (opposite-sign) asymmetry", "[dsp][engine][bias]")
{
    auto runWithBias = [] (float biasProportion)
    {
        AureateEngine engine;
        engine.setDriveDb (18.0f);
        engine.setWarmthProportion (0.0f);
        engine.setBiasProportion (biasProportion);
        engine.setMixProportion (1.0f);
        engine.setOutputDb (0.0f);
        engine.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return peakPositive (buffer, 0) - std::abs (peakNegative (buffer, 0));
    };

    const auto positiveBiasSkew = runWithBias (1.0f);
    const auto negativeBiasSkew = runWithBias (-1.0f);

    // Opposite Bias settings must skew the asymmetry in opposite directions.
    CHECK ((positiveBiasSkew > 0.0f) != (negativeBiasSkew > 0.0f));
}

//==============================================================================
// Character

TEST_CASE ("Character: Tape, Console, and Valve produce different wet output for identical parameters", "[dsp][engine][character]")
{
    auto runWithModel = [] (TapeSaturator::Model model)
    {
        AureateEngine engine;
        engine.setDriveDb (18.0f);
        engine.setWarmthProportion (0.5f);
        engine.setMixProportion (1.0f);
        engine.setOutputDb (0.0f);
        engine.setCharacter (model);
        engine.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.8f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        return TestHelpers::rms (buffer);
    };

    const auto tapeRms = runWithModel (TapeSaturator::Model::tape);
    const auto consoleRms = runWithModel (TapeSaturator::Model::console);
    const auto valveRms = runWithModel (TapeSaturator::Model::valve);

    CHECK (tapeRms != Catch::Approx (consoleRms).margin (1e-4));
    CHECK (tapeRms != Catch::Approx (valveRms).margin (1e-4));
    CHECK (consoleRms != Catch::Approx (valveRms).margin (1e-4));
}

TEST_CASE ("Character: changing model at runtime (no re-prepare) does not crash and stays finite", "[dsp][engine][character]")
{
    AureateEngine engine;
    engine.setDriveDb (18.0f);
    engine.setWarmthProportion (0.5f);
    engine.setMixProportion (1.0f);
    engine.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::dsp::AudioBlock<float> block (buffer);

    for (auto model : { TapeSaturator::Model::tape, TapeSaturator::Model::console,
                         TapeSaturator::Model::valve, TapeSaturator::Model::tape })
    {
        engine.setCharacter (model);
        TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.8f);
        CHECK_NOTHROW (engine.process (block));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

//==============================================================================
// Hiss

TEST_CASE ("Hiss: 0% amount keeps silence silent", "[dsp][engine][hiss]")
{
    AureateEngine engine;
    engine.setHissProportion (0.0f);
    engine.setMixProportion (1.0f);
    engine.setOutputDb (0.0f);
    engine.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    buffer.clear();

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::rms (buffer) == Catch::Approx (0.0).margin (1e-9));
}

TEST_CASE ("Hiss: 100% amount adds an audible, finite, bounded noise floor to silence", "[dsp][engine][hiss]")
{
    AureateEngine engine;
    engine.setHissProportion (1.0f);
    engine.setMixProportion (1.0f);
    engine.setOutputDb (0.0f);
    engine.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    buffer.clear();

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto rms = TestHelpers::rms (buffer);
    CHECK (rms > 1.0e-5);
    CHECK (TestHelpers::peakAbsolute (buffer) < 0.1f); // a noise floor, not a loud signal
}

TEST_CASE ("Hiss: higher amount produces a higher (or equal) noise floor than a lower amount", "[dsp][engine][hiss]")
{
    auto measureHissRms = [] (float amount)
    {
        AureateEngine engine;
        engine.setHissProportion (amount);
        engine.setMixProportion (1.0f);
        engine.setOutputDb (0.0f);
        engine.prepare (makeTestSpec (2));

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        buffer.clear();

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return TestHelpers::rms (buffer);
    };

    CHECK (measureHissRms (1.0f) > measureHissRms (0.25f));
    CHECK (measureHissRms (0.25f) > measureHissRms (0.0f));
}

//==============================================================================
// HF/LF Trim

TEST_CASE ("HF Trim: a positive setting raises a high-frequency tone's level versus 0 dB", "[dsp][engine][trim]")
{
    auto measureRmsAtHfTrim = [] (float trimDb)
    {
        AureateEngine engine;
        engine.setDriveDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setHfTrimDb (trimDb);
        engine.setOutputDb (0.0f);
        engine.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 10000.0, 0.2f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        return TestHelpers::rms (buffer);
    };

    CHECK (measureRmsAtHfTrim (6.0f) > measureRmsAtHfTrim (0.0f));
    CHECK (measureRmsAtHfTrim (0.0f) > measureRmsAtHfTrim (-6.0f));
}

TEST_CASE ("LF Trim: a positive setting raises a low-frequency tone's level versus 0 dB", "[dsp][engine][trim]")
{
    auto measureRmsAtLfTrim = [] (float trimDb)
    {
        AureateEngine engine;
        engine.setDriveDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setLfTrimDb (trimDb);
        engine.setOutputDb (0.0f);
        engine.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 80.0, 0.2f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        return TestHelpers::rms (buffer);
    };

    CHECK (measureRmsAtLfTrim (6.0f) > measureRmsAtLfTrim (0.0f));
    CHECK (measureRmsAtLfTrim (0.0f) > measureRmsAtLfTrim (-6.0f));
}
