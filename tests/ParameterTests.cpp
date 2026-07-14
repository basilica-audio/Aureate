#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    // Convenience wrapper: fetches a parameter by ID and requires it to
    // exist before returning, so every SECTION below fails loudly (not with
    // a null-deref) if an ID typo ever creeps in.
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    // Checks that a float parameter's underlying NormalisableRange covers
    // [expectedMin, expectedMax], independent of any skew/log mapping.
    void checkFloatRange (juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id,
                           float expectedMin,
                           float expectedMax)
    {
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id));
        REQUIRE (param != nullptr);

        const auto range = param->getNormalisableRange().getRange();
        CHECK (range.getStart() == Catch::Approx (expectedMin));
        CHECK (range.getEnd() == Catch::Approx (expectedMax));
    }

    // Checks a float parameter's default value in real (non-normalised)
    // units, going through convertTo0to1 so log-skewed ranges are handled
    // the same way as linear ones.
    void checkFloatDefault (juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& id,
                             float expectedDefault)
    {
        auto* param = requireParam (apvts, id);
        CHECK (param->getDefaultValue() == Catch::Approx (param->convertTo0to1 (expectedDefault)).margin (1e-4));
    }
}

TEST_CASE ("Processor instantiates with the expected parameters", "[processor][parameters]")
{
    AureateAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("plugin name")
    {
        CHECK (processor.getName() == juce::String ("Aureate"));
    }

    SECTION ("all documented parameter IDs resolve")
    {
        static constexpr const char* allIds[] = {
            ParamIDs::drive, ParamIDs::warmth, ParamIDs::tone, ParamIDs::mix, ParamIDs::output,
            ParamIDs::bias, ParamIDs::wowFlutter, ParamIDs::hiss, ParamIDs::character,
            ParamIDs::hfTrim, ParamIDs::lfTrim,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.1.0 layout")
    {
        CHECK (apvts.processor.getParameters().size() == 11);
    }

    SECTION ("Drive: saturator input gain defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::drive, 6.0f);
        checkFloatRange (apvts, ParamIDs::drive, 0.0f, 24.0f);
    }

    SECTION ("Warmth: bias + HF-rolloff amount defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::warmth, 35.0f);
        checkFloatRange (apvts, ParamIDs::warmth, 0.0f, 100.0f);
    }

    SECTION ("Tone: tilt EQ defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::tone, 0.0f);
        checkFloatRange (apvts, ParamIDs::tone, -100.0f, 100.0f);
    }

    SECTION ("Mix: dry/wet defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::mix, 100.0f);
        checkFloatRange (apvts, ParamIDs::mix, 0.0f, 100.0f);
    }

    SECTION ("Output: post-mix trim defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::output, 0.0f);
        checkFloatRange (apvts, ParamIDs::output, -24.0f, 24.0f);
    }

    SECTION ("Bias: additional asymmetry trim defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::bias, 0.0f);
        checkFloatRange (apvts, ParamIDs::bias, -100.0f, 100.0f);
    }

    SECTION ("Wow/Flutter: tape-transport instability defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::wowFlutter, 0.0f);
        checkFloatRange (apvts, ParamIDs::wowFlutter, 0.0f, 100.0f);
    }

    SECTION ("Hiss: noise-floor amount defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::hiss, 0.0f);
        checkFloatRange (apvts, ParamIDs::hiss, 0.0f, 100.0f);
    }

    SECTION ("HF Trim: high-shelf trim defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::hfTrim, 0.0f);
        checkFloatRange (apvts, ParamIDs::hfTrim, -6.0f, 6.0f);
    }

    SECTION ("LF Trim: low-shelf trim defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::lfTrim, 0.0f);
        checkFloatRange (apvts, ParamIDs::lfTrim, -6.0f, 6.0f);
    }

    SECTION ("Character: model choice defaults to Tape (index 0), three choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::character));
        REQUIRE (param != nullptr);
        CHECK (param->choices.size() == 3);
        CHECK (param->choices[0] == "Tape");
        CHECK (param->choices[1] == "Console");
        CHECK (param->choices[2] == "Valve");
        CHECK (param->getIndex() == 0);
    }
}
