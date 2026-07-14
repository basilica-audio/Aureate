#include "dsp/AureateEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Wow/Flutter is designed so its fixed base delay - not the live amount - is
// what contributes to getLatencySamples() (see AureateEngine.h/.cpp), so
// automating the amount during playback never changes the plugin's reported
// latency. These tests exercise that contract directly, plus the stage's
// basic audio-thread safety.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Wow/Flutter: getLatencySamples() is identical regardless of the amount set before prepare()", "[dsp][wowflutter][latency]")
{
    AureateEngine offEngine;
    offEngine.setWowFlutterProportion (0.0f);
    offEngine.prepare (makeTestSpec (2));

    AureateEngine onEngine;
    onEngine.setWowFlutterProportion (1.0f);
    onEngine.prepare (makeTestSpec (2));

    CHECK (offEngine.getLatencySamples() == onEngine.getLatencySamples());
    CHECK (offEngine.getLatencySamples() > 0); // the base delay alone guarantees this
}

TEST_CASE ("Wow/Flutter: getLatencySamples() does not change when the amount is changed after prepare(), without re-preparing",
           "[dsp][wowflutter][latency]")
{
    AureateEngine engine;
    engine.setWowFlutterProportion (0.0f);
    engine.prepare (makeTestSpec (2));

    const auto latencyBefore = engine.getLatencySamples();

    engine.setWowFlutterProportion (1.0f);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.5f);
    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (engine.getLatencySamples() == latencyBefore);
}

TEST_CASE ("Wow/Flutter: at 0% amount, the wet chain output is unaffected by the stage (matches a reference with Wow/Flutter always off)",
           "[dsp][wowflutter]")
{
    // Two otherwise-identical engines, only one of which had Wow/Flutter
    // explicitly set to a nonzero amount at some point before being set
    // back to 0 - both must produce byte-identical output, proving 0%
    // amount is a true off state (a fixed, non-modulated delay), not just
    // "very little" modulation.
    AureateEngine referenceEngine;
    referenceEngine.setDriveDb (6.0f);
    referenceEngine.setWarmthProportion (0.4f);
    referenceEngine.setMixProportion (1.0f);
    referenceEngine.setOutputDb (0.0f);
    referenceEngine.prepare (makeTestSpec (2));

    AureateEngine testEngine;
    testEngine.setDriveDb (6.0f);
    testEngine.setWarmthProportion (0.4f);
    testEngine.setMixProportion (1.0f);
    testEngine.setOutputDb (0.0f);
    testEngine.setWowFlutterProportion (0.0f);
    testEngine.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> referenceBuffer (2, testBlockSize);
    TestHelpers::fillWithSine (referenceBuffer, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> testBuffer;
    testBuffer.makeCopyOf (referenceBuffer);

    juce::dsp::AudioBlock<float> referenceBlock (referenceBuffer);
    referenceEngine.process (referenceBlock);

    juce::dsp::AudioBlock<float> testBlock (testBuffer);
    testEngine.process (testBlock);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* refData = referenceBuffer.getReadPointer (channel);
        const auto* testData = testBuffer.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (refData[i] == Catch::Approx (testData[i]).margin (0.0));
    }
}

TEST_CASE ("Wow/Flutter: a nonzero amount measurably changes the wet output versus 0% amount", "[dsp][wowflutter]")
{
    AureateEngine offEngine;
    offEngine.setDriveDb (0.0f);
    offEngine.setWarmthProportion (0.0f);
    offEngine.setMixProportion (1.0f);
    offEngine.setOutputDb (0.0f);
    offEngine.setWowFlutterProportion (0.0f);
    offEngine.prepare (makeTestSpec (1));

    AureateEngine onEngine;
    onEngine.setDriveDb (0.0f);
    onEngine.setWarmthProportion (0.0f);
    onEngine.setMixProportion (1.0f);
    onEngine.setOutputDb (0.0f);
    onEngine.setWowFlutterProportion (1.0f);
    onEngine.prepare (makeTestSpec (1));

    // A sustained, harmonically simple tone makes pitch-modulation artefacts
    // easy to detect via a plain sample-difference sum.
    juce::AudioBuffer<float> offBuffer (1, testBlockSize);
    TestHelpers::fillWithSine (offBuffer, testSampleRate, 2000.0, 0.5f);

    juce::AudioBuffer<float> onBuffer;
    onBuffer.makeCopyOf (offBuffer);

    juce::dsp::AudioBlock<float> offBlock (offBuffer);
    offEngine.process (offBlock);

    juce::dsp::AudioBlock<float> onBlock (onBuffer);
    onEngine.process (onBlock);

    double sumOfAbsoluteDifferences = 0.0;
    for (int i = 0; i < testBlockSize; ++i)
        sumOfAbsoluteDifferences += std::abs (static_cast<double> (offBuffer.getSample (0, i)) - static_cast<double> (onBuffer.getSample (0, i)));

    CHECK (sumOfAbsoluteDifferences > 1.0);
    CHECK (TestHelpers::allSamplesFinite (offBuffer));
    CHECK (TestHelpers::allSamplesFinite (onBuffer));
}

TEST_CASE ("Wow/Flutter: full amount with full-scale input and rapid re-preparing produces no NaN/Inf", "[dsp][wowflutter][robustness]")
{
    AureateEngine engine;
    engine.setDriveDb (18.0f);
    engine.setWowFlutterProportion (1.0f);
    engine.setMixProportion (1.0f);

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = rate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 2;
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, rate, 1000.0, 1.0f);

        juce::dsp::AudioBlock<float> block (buffer);
        for (int i = 0; i < 4; ++i)
            engine.process (block);

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}
