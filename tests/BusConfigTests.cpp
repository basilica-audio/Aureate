#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Broadens bus-layout coverage per the M1 "broaden test coverage" issue:
// mono and stereo, the only two layouts AureateAudioProcessor::
// isBusesLayoutSupported() accepts (see PluginProcessor.cpp - in==out,
// mono or stereo only; no sidechain input bus exists), plus a couple of
// layouts that must be rejected.
TEST_CASE ("isBusesLayoutSupported: mono in/out is supported", "[processor][buses]")
{
    AureateAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::mono());

    CHECK (processor.isBusesLayoutSupported (layout));
}

TEST_CASE ("isBusesLayoutSupported: stereo in/out is supported", "[processor][buses]")
{
    AureateAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK (processor.isBusesLayoutSupported (layout));
}

TEST_CASE ("isBusesLayoutSupported: mismatched in/out channel counts are rejected", "[processor][buses]")
{
    AureateAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK_FALSE (processor.isBusesLayoutSupported (layout));
}

TEST_CASE ("isBusesLayoutSupported: surround (5.1) is rejected", "[processor][buses]")
{
    AureateAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::create5point1());
    layout.outputBuses.add (juce::AudioChannelSet::create5point1());

    CHECK_FALSE (processor.isBusesLayoutSupported (layout));
}

TEST_CASE ("isBusesLayoutSupported: a disabled main output is rejected", "[processor][buses]")
{
    AureateAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::disabled());

    CHECK_FALSE (processor.isBusesLayoutSupported (layout));
}

TEST_CASE ("Mono processing: prepareToPlay + processBlock on a mono buffer produces finite, sane output", "[processor][buses][mono]")
{
    AureateAudioProcessor processor;

    // Force the processor into mono by disabling the default stereo bus and
    // enabling a mono one, mirroring what a mono-track host would do.
    auto* inputBus = processor.getBus (true, 0);
    auto* outputBus = processor.getBus (false, 0);
    REQUIRE (inputBus != nullptr);
    REQUIRE (outputBus != nullptr);

    const auto monoSupported = inputBus->setCurrentLayout (juce::AudioChannelSet::mono())
                                && outputBus->setCurrentLayout (juce::AudioChannelSet::mono());
    REQUIRE (monoSupported);

    processor.prepareToPlay (48000.0, 512);

    auto* driveParam = processor.apvts.getParameter (ParamIDs::drive);
    REQUIRE (driveParam != nullptr);
    driveParam->setValueNotifyingHost (driveParam->convertTo0to1 (18.0f));

    juce::AudioBuffer<float> buffer (1, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);
    juce::MidiBuffer midi;

    for (int i = 0; i < 4; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (processor.getTotalNumInputChannels() == 1);
    CHECK (processor.getTotalNumOutputChannels() == 1);
}

TEST_CASE ("Stereo processing: prepareToPlay + processBlock on a stereo buffer produces finite, sane output", "[processor][buses][stereo]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* driveParam = processor.apvts.getParameter (ParamIDs::drive);
    REQUIRE (driveParam != nullptr);
    driveParam->setValueNotifyingHost (driveParam->convertTo0to1 (18.0f));

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);
    juce::MidiBuffer midi;

    for (int i = 0; i < 4; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (processor.getTotalNumInputChannels() == 2);
    CHECK (processor.getTotalNumOutputChannels() == 2);
}
