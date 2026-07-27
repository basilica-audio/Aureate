#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* driveParam = processor.apvts.getParameter (ParamIDs::drive);
    auto* warmthParam = processor.apvts.getParameter (ParamIDs::warmth);
    auto* toneParam = processor.apvts.getParameter (ParamIDs::tone);
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* outputParam = processor.apvts.getParameter (ParamIDs::output);
    auto* biasParam = processor.apvts.getParameter (ParamIDs::bias);
    auto* wowParam = processor.apvts.getParameter (ParamIDs::wow);
    auto* flutterParam = processor.apvts.getParameter (ParamIDs::flutter);
    auto* hissParam = processor.apvts.getParameter (ParamIDs::hiss);
    auto* characterParam = processor.apvts.getParameter (ParamIDs::character);
    auto* hfTrimParam = processor.apvts.getParameter (ParamIDs::hfTrim);
    auto* lfTrimParam = processor.apvts.getParameter (ParamIDs::lfTrim);

    REQUIRE (driveParam != nullptr);
    REQUIRE (warmthParam != nullptr);
    REQUIRE (toneParam != nullptr);
    REQUIRE (mixParam != nullptr);
    REQUIRE (outputParam != nullptr);
    REQUIRE (biasParam != nullptr);
    REQUIRE (wowParam != nullptr);
    REQUIRE (flutterParam != nullptr);
    REQUIRE (hissParam != nullptr);
    REQUIRE (characterParam != nullptr);
    REQUIRE (hfTrimParam != nullptr);
    REQUIRE (lfTrimParam != nullptr);

    driveParam->setValueNotifyingHost (driveParam->convertTo0to1 (17.0f));
    warmthParam->setValueNotifyingHost (warmthParam->convertTo0to1 (72.0f));
    toneParam->setValueNotifyingHost (toneParam->convertTo0to1 (-40.0f));
    mixParam->setValueNotifyingHost (mixParam->convertTo0to1 (63.0f));
    outputParam->setValueNotifyingHost (outputParam->convertTo0to1 (-4.5f));
    biasParam->setValueNotifyingHost (biasParam->convertTo0to1 (-25.0f));
    wowParam->setValueNotifyingHost (wowParam->convertTo0to1 (40.0f));
    flutterParam->setValueNotifyingHost (flutterParam->convertTo0to1 (65.0f));
    hissParam->setValueNotifyingHost (hissParam->convertTo0to1 (15.0f));
    characterParam->setValueNotifyingHost (characterParam->convertTo0to1 (2.0f)); // Valve
    hfTrimParam->setValueNotifyingHost (hfTrimParam->convertTo0to1 (3.0f));
    lfTrimParam->setValueNotifyingHost (lfTrimParam->convertTo0to1 (-2.0f));

    const auto savedDrive = driveParam->getValue();
    const auto savedWarmth = warmthParam->getValue();
    const auto savedTone = toneParam->getValue();
    const auto savedMix = mixParam->getValue();
    const auto savedOutput = outputParam->getValue();
    const auto savedBias = biasParam->getValue();
    const auto savedWow = wowParam->getValue();
    const auto savedFlutter = flutterParam->getValue();
    const auto savedHiss = hissParam->getValue();
    const auto savedCharacter = characterParam->getValue();
    const auto savedHfTrim = hfTrimParam->getValue();
    const auto savedLfTrim = lfTrimParam->getValue();

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    driveParam->setValueNotifyingHost (driveParam->getDefaultValue());
    warmthParam->setValueNotifyingHost (warmthParam->getDefaultValue());
    toneParam->setValueNotifyingHost (toneParam->getDefaultValue());
    mixParam->setValueNotifyingHost (mixParam->getDefaultValue());
    outputParam->setValueNotifyingHost (outputParam->getDefaultValue());
    biasParam->setValueNotifyingHost (biasParam->getDefaultValue());
    wowParam->setValueNotifyingHost (wowParam->getDefaultValue());
    flutterParam->setValueNotifyingHost (flutterParam->getDefaultValue());
    hissParam->setValueNotifyingHost (hissParam->getDefaultValue());
    characterParam->setValueNotifyingHost (characterParam->getDefaultValue());
    hfTrimParam->setValueNotifyingHost (hfTrimParam->getDefaultValue());
    lfTrimParam->setValueNotifyingHost (lfTrimParam->getDefaultValue());

    REQUIRE (driveParam->getValue() != Catch::Approx (savedDrive));
    REQUIRE (warmthParam->getValue() != Catch::Approx (savedWarmth));
    REQUIRE (toneParam->getValue() != Catch::Approx (savedTone));
    REQUIRE (mixParam->getValue() != Catch::Approx (savedMix));
    REQUIRE (outputParam->getValue() != Catch::Approx (savedOutput));
    REQUIRE (biasParam->getValue() != Catch::Approx (savedBias));
    REQUIRE (wowParam->getValue() != Catch::Approx (savedWow));
    REQUIRE (flutterParam->getValue() != Catch::Approx (savedFlutter));
    REQUIRE (hissParam->getValue() != Catch::Approx (savedHiss));
    REQUIRE (characterParam->getValue() != Catch::Approx (savedCharacter));
    REQUIRE (hfTrimParam->getValue() != Catch::Approx (savedHfTrim));
    REQUIRE (lfTrimParam->getValue() != Catch::Approx (savedLfTrim));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (driveParam->getValue() == Catch::Approx (savedDrive).margin (1e-6));
    CHECK (warmthParam->getValue() == Catch::Approx (savedWarmth).margin (1e-6));
    CHECK (toneParam->getValue() == Catch::Approx (savedTone).margin (1e-6));
    CHECK (mixParam->getValue() == Catch::Approx (savedMix).margin (1e-6));
    CHECK (outputParam->getValue() == Catch::Approx (savedOutput).margin (1e-6));
    CHECK (biasParam->getValue() == Catch::Approx (savedBias).margin (1e-6));
    CHECK (wowParam->getValue() == Catch::Approx (savedWow).margin (1e-6));
    CHECK (flutterParam->getValue() == Catch::Approx (savedFlutter).margin (1e-6));
    CHECK (hissParam->getValue() == Catch::Approx (savedHiss).margin (1e-6));
    CHECK (characterParam->getValue() == Catch::Approx (savedCharacter).margin (1e-6));
    CHECK (hfTrimParam->getValue() == Catch::Approx (savedHfTrim).margin (1e-6));
    CHECK (lfTrimParam->getValue() == Catch::Approx (savedLfTrim).margin (1e-6));
}

//==============================================================================
// docs/design-brief.md §7 / §5 guarantee 8: a v0.1.0-shaped state (single
// "wow_flutter" PARAM entry, no "wow"/"flutter" entries) must still load
// without erroring, with both new parameters landing at the old single
// value rather than silently resetting to 0%.
TEST_CASE ("State migration: a v0.1.0-shaped state (single wow_flutter PARAM) loads cleanly and maps onto wow/flutter",
           "[state][migration]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // A synthetic v0.1.0-shaped APVTS state blob: every v0.1.0 parameter ID
    // present (including the since-retired "wow_flutter"), but neither
    // "wow" nor "flutter" (which did not exist in v0.1.0's layout).
    constexpr const char* legacyStateXml =
        "<PARAMETERS>"
        "<PARAM id=\"drive\" value=\"6.0\"/>"
        "<PARAM id=\"warmth\" value=\"35.0\"/>"
        "<PARAM id=\"tone\" value=\"0.0\"/>"
        "<PARAM id=\"mix\" value=\"100.0\"/>"
        "<PARAM id=\"output\" value=\"0.0\"/>"
        "<PARAM id=\"bias\" value=\"0.0\"/>"
        "<PARAM id=\"wow_flutter\" value=\"57.0\"/>"
        "<PARAM id=\"hiss\" value=\"0.0\"/>"
        "<PARAM id=\"character\" value=\"0.0\"/>"
        "<PARAM id=\"hf_trim\" value=\"0.0\"/>"
        "<PARAM id=\"lf_trim\" value=\"0.0\"/>"
        "</PARAMETERS>";

    const std::unique_ptr<juce::XmlElement> legacyXml (juce::XmlDocument::parse (juce::String (legacyStateXml)));
    REQUIRE (legacyXml != nullptr);

    juce::MemoryBlock legacyStateBlock;
    juce::AudioProcessor::copyXmlToBinary (*legacyXml, legacyStateBlock);

    // Perturb Wow/Flutter first so a no-op/failed load couldn't accidentally
    // pass the assertions below.
    auto* wowParam = processor.apvts.getParameter (ParamIDs::wow);
    auto* flutterParam = processor.apvts.getParameter (ParamIDs::flutter);
    REQUIRE (wowParam != nullptr);
    REQUIRE (flutterParam != nullptr);
    wowParam->setValueNotifyingHost (wowParam->convertTo0to1 (0.0f));
    flutterParam->setValueNotifyingHost (flutterParam->convertTo0to1 (0.0f));

    CHECK_NOTHROW (processor.setStateInformation (legacyStateBlock.getData(), static_cast<int> (legacyStateBlock.getSize())));

    // Both new parameters land at the old single value - a "recognisable (if
    // not identical) character" per the brief, not a silent reset to 0%.
    CHECK (wowParam->convertFrom0to1 (wowParam->getValue()) == Catch::Approx (57.0f).margin (1.0e-3));
    CHECK (flutterParam->convertFrom0to1 (flutterParam->getValue()) == Catch::Approx (57.0f).margin (1.0e-3));

    // Every other v0.1.0 parameter still loads normally too.
    auto* driveParam = processor.apvts.getParameter (ParamIDs::drive);
    REQUIRE (driveParam != nullptr);
    CHECK (driveParam->convertFrom0to1 (driveParam->getValue()) == Catch::Approx (6.0f).margin (1.0e-3));
}

TEST_CASE ("State migration: a v0.2.0-shaped state (already carrying wow/flutter) is left untouched by the migration",
           "[state][migration]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* wowParam = processor.apvts.getParameter (ParamIDs::wow);
    auto* flutterParam = processor.apvts.getParameter (ParamIDs::flutter);
    REQUIRE (wowParam != nullptr);
    REQUIRE (flutterParam != nullptr);

    wowParam->setValueNotifyingHost (wowParam->convertTo0to1 (12.0f));
    flutterParam->setValueNotifyingHost (flutterParam->convertTo0to1 (88.0f));

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);

    wowParam->setValueNotifyingHost (wowParam->getDefaultValue());
    flutterParam->setValueNotifyingHost (flutterParam->getDefaultValue());

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    // The migration must not overwrite genuinely different, already-present
    // wow/flutter values with each other or with anything derived from a
    // (nonexistent, in a v0.2.0 state) legacy entry.
    CHECK (wowParam->convertFrom0to1 (wowParam->getValue()) == Catch::Approx (12.0f).margin (1.0e-3));
    CHECK (flutterParam->convertFrom0to1 (flutterParam->getValue()) == Catch::Approx (88.0f).margin (1.0e-3));
}

//==============================================================================
// v0.3.0 neutrality / migration render-null (brief section 6, test 6.1).
//
// 6.1(a) lives here as a pure state test; 6.1(b) - the in-process bit-exact
// A/B - lives in EngineTests.cpp where the engine's test-only bypass flag is
// exercised directly; 6.1(c) is the cross-version tolerance null below.
//
// Why 6.1(c) is a tolerance null and not a bit-exact golden: std::tanh
// (TapeSaturator), std::exp/std::sin (the wow/flutter LFOs and this file's
// own fixture generator) are not bit-identical between Apple libm and the
// MSVC UCRT, and FP contraction differs per compiler, so a pinned
// max-abs-diff == 0 golden would be green on at most one leg of the
// {macos-latest, windows-latest} CI matrix and would leave protected main
// permanently red. Bit-identity is only ever asserted *within one binary*
// (test 6.1b). See the brief's revision note 1.
//==============================================================================

#include "TestHelpers.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr double referenceRenderSampleRate = 48000.0;
    constexpr int referenceRenderBlockSize = 512;
    constexpr int referenceRenderNumSamples = 96000; // 2 s at 48 kHz
    constexpr int referenceRenderNumChannels = 2;

    juce::File referenceRenderFile()
    {
        return juce::File (AUREATE_TEST_DATA_DIR).getChildFile ("aureate-v0.2.1-default-render.wav");
    }

    // Renders the shared neutrality fixture through a default-state
    // processor. `configure` gets a chance to touch parameters/engine flags
    // before the render starts.
    template <typename ConfigureFn>
    juce::AudioBuffer<float> renderNeutralityFixture (ConfigureFn&& configure)
    {
        AureateAudioProcessor processor;
        configure (processor);
        processor.prepareToPlay (referenceRenderSampleRate, referenceRenderBlockSize);

        juce::AudioBuffer<float> source (referenceRenderNumChannels, referenceRenderNumSamples);
        TestHelpers::fillNeutralityFixture (source, referenceRenderSampleRate);

        juce::AudioBuffer<float> rendered (referenceRenderNumChannels, referenceRenderNumSamples);
        juce::MidiBuffer midi;

        for (int start = 0; start < referenceRenderNumSamples; start += referenceRenderBlockSize)
        {
            const auto length = juce::jmin (referenceRenderBlockSize, referenceRenderNumSamples - start);

            juce::AudioBuffer<float> block (referenceRenderNumChannels, length);
            for (int channel = 0; channel < referenceRenderNumChannels; ++channel)
                block.copyFrom (channel, 0, source, channel, start, length);

            processor.processBlock (block, midi);

            for (int channel = 0; channel < referenceRenderNumChannels; ++channel)
                rendered.copyFrom (channel, start, block, channel, 0, length);
        }

        return rendered;
    }

    juce::AudioBuffer<float> renderDefaultState()
    {
        return renderNeutralityFixture ([] (AureateAudioProcessor&) {});
    }
}

// Hidden by the leading '.' in its tag: this is a fixture *generator*, not an
// assertion, and it writes into the source tree. It was run exactly once,
// against the pristine v0.2.1 tree (CMakeLists project(Aureate VERSION
// 0.2.1), before any v0.3.0 DSP existed), to produce
// tests/data/aureate-v0.2.1-default-render.wav. It is deliberately NOT run
// in CI - regenerating the reference from the current tree would make test
// 6.1(c) self-fulfilling and worthless. Run manually with:
//   ./build/Tests "[!benchmark][.fixture]"   (or: ./build/Tests --list-tests)
TEST_CASE ("Fixture generator: write the v0.2.1 default-state reference render", "[.fixture]")
{
    const auto rendered = renderDefaultState();

    auto file = referenceRenderFile();
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    REQUIRE (stream != nullptr);

    // 32-bit float (SampleFormat::floatingPoint, which for WavAudioFormat is
    // only meaningful at 32 bits), so the reference carries the renderer's
    // full precision and the -120 dBFS tolerance in 6.1(c) is measuring the
    // DSP, not the fixture's own quantisation.
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (referenceRenderSampleRate)
                             .withNumChannels (referenceRenderNumChannels)
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream, options));
    REQUIRE (writer != nullptr);

    REQUIRE (writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples()));
    writer.reset();

    CHECK (file.existsAsFile());
}
