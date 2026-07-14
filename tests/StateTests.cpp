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
    auto* wowFlutterParam = processor.apvts.getParameter (ParamIDs::wowFlutter);
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
    REQUIRE (wowFlutterParam != nullptr);
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
    wowFlutterParam->setValueNotifyingHost (wowFlutterParam->convertTo0to1 (40.0f));
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
    const auto savedWowFlutter = wowFlutterParam->getValue();
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
    wowFlutterParam->setValueNotifyingHost (wowFlutterParam->getDefaultValue());
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
    REQUIRE (wowFlutterParam->getValue() != Catch::Approx (savedWowFlutter));
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
    CHECK (wowFlutterParam->getValue() == Catch::Approx (savedWowFlutter).margin (1e-6));
    CHECK (hissParam->getValue() == Catch::Approx (savedHiss).margin (1e-6));
    CHECK (characterParam->getValue() == Catch::Approx (savedCharacter).margin (1e-6));
    CHECK (hfTrimParam->getValue() == Catch::Approx (savedHfTrim).margin (1e-6));
    CHECK (lfTrimParam->getValue() == Catch::Approx (savedLfTrim).margin (1e-6));
}
