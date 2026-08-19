#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// a11y coverage for every wired M3 photoreal-GUI control. Deliberately calls
// createAccessibilityHandler() directly rather than getAccessibilityHandler()
// - the latter (JUCE 8.0.14 juce_Component.cpp) only returns a handler once
// the component has a live native window peer, which this headless,
// no-message-loop test binary never has.
namespace
{
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;

        return nullptr;
    }

    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }
}

TEST_CASE ("Every wired MasterCropKnob exposes an accessible title, value, and declared unit", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    // One representative knob per unit family declared in
    // ParameterLayout.cpp (dB/%/Hz, plus the unit-less Character choice) -
    // see docs/gui-mapping.md for the full 10-knob mapping table.
    struct Expectation
    {
        const char* label;
        const char* unitSuffixOrEmpty; // empty => choice parameter, no unit suffix expected
    };

    const Expectation expectations[] = {
        { "Drive", "dB" },
        { "Warmth", "%" },
        { "Tone", "%" },
        { "Output", "dB" },
        { "Mix", "%" },
        { "Bias", "%" },
        { "Character", "" },
        { "Glue Threshold", "dB" },
        { "Glue Makeup", "dB" },
        { "Iron", "%" },
    };

    for (const auto& expectation : expectations)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, expectation.label);
        REQUIRE (knob != nullptr);
        CHECK (knob->getTitle() == expectation.label);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.label << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.isNotEmpty());

        if (juce::String (expectation.unitSuffixOrEmpty).isNotEmpty())
            CHECK (valueText.endsWith (expectation.unitSuffixOrEmpty));
    }
}

TEST_CASE ("Every wired toggle's accessible name matches its visual label and exposes a checkable state", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    for (const auto* label : { "Glue", "Auto Gain", "Glue Model", "Quality" })
    {
        auto* toggle = findChildByTitle<juce::ToggleButton> (editor, label);
        REQUIRE (toggle != nullptr);
        CHECK (toggle->getTitle() == juce::String (label));

        const auto handler = createHandlerForTest (*toggle);
        REQUIRE (handler != nullptr);

        // juce::ToggleButton's own constructor calls
        // setClickingTogglesState(true) (JUCE 8.0.14 juce_ToggleButton.cpp),
        // so juce::Button::isToggleable() is true and the base juce::Button
        // AccessibilityHandler correctly exposes checkable/checked state.
        CHECK (handler->getCurrentState().isCheckable());
    }
}

TEST_CASE ("HubNeedle (Output Level meter) exposes a read-only accessible value inside the real editor", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    auto* needle = findChildByTitle<basilica::gui::HubNeedle> (editor, "Output Level meter");
    REQUIRE (needle != nullptr);

    const auto handler = createHandlerForTest (*needle);
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);
    CHECK (valueInterface->isReadOnly());
    CHECK (valueInterface->getCurrentValueAsString().endsWith ("dB"));
}

TEST_CASE ("Scale button's accessible title reflects the current scale percentage, not a static string", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    auto* scaleButton = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("scaleButton"));
    REQUIRE (scaleButton != nullptr);

    CHECK (scaleButton->getTitle().contains ("100%"));

    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}

// Issue #5 (keyboard navigation): juce::Slider ships with
// setWantsKeyboardFocus(false) in JUCE 8.0.14 (juce_Slider.cpp:1461,
// Slider::init), so MasterCropKnob was silently unreachable by Tab and its
// keyPressed()/focus ring never fired - and even when focused, the base
// keyPressed (juce_Slider.cpp:1029) steps by the raw parameter interval
// (0.1% on a 200% Tone range) and ignores Shift entirely. These tests pin
// the fixed contract (setWantsKeyboardFocus(true) + KeyboardSteps.h).

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    int knobsSeen = 0, togglesSeen = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        auto* child = editor.getChildComponent (i);

        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            ++knobsSeen;
            INFO ("knob \"" << slider->getTitle().toStdString() << "\"");
            CHECK (slider->getWantsKeyboardFocus());
        }
        else if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
        {
            ++togglesSeen;
            INFO ("toggle \"" << toggle->getTitle().toStdString() << "\"");
            CHECK (toggle->getWantsKeyboardFocus());
        }
    }

    // All 10 knobs and 4 toggles must be present AND focusable - a
    // zero-match loop must not pass vacuously.
    CHECK (knobsSeen == 10);
    CHECK (togglesSeen == 4);

    auto* scaleButton = editor.findChildWithID ("scaleButton");
    REQUIRE (scaleButton != nullptr);
    CHECK (scaleButton->getWantsKeyboardFocus());
}

TEST_CASE ("Arrow keys step knobs by a practical amount, Shift+Arrow steps finer", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    // Tone: linear -100..+100 %, 0.1 interval (ParameterLayout.cpp) - the
    // base-class step would be 0.1 over a 200-unit range (2000 presses).
    auto* knob = findChildByTitle<juce::Slider> (editor, "Tone");
    REQUIRE (knob != nullptr);

    knob->setValue (0.0, juce::sendNotificationSync);

    // Called through Component& for the same [class.access.virt] reason
    // documented on createHandlerForTest().
    juce::Component& knobAsComponent = *knob;

    // Plain Right = 1% of the 200-unit range = 2.0.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (2.0).margin (1.0e-4));

    // Shift+Right = 0.1% = 0.2 (the keyboard analog of Shift-drag).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                          juce::ModifierKeys::shiftModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (2.2).margin (1.0e-4));

    // Plain Left steps back down symmetrically.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (0.2).margin (1.0e-4));

    // PageDown = 10% = 20.0.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
    CHECK (knob->getValue() == Catch::Approx (-19.8).margin (1.0e-4));

    // Home/End jump to the range extremes (WAI-ARIA slider pattern).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (-100.0).margin (1.0e-4));
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (knob->getValue() == Catch::Approx (100.0).margin (1.0e-4));

    // Ctrl/Cmd-modified presses are host shortcuts - never consumed.
    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                              juce::ModifierKeys::ctrlModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (100.0).margin (1.0e-4));
}

TEST_CASE ("Choice knobs step by exactly one choice per arrow press", "[gui][a11y]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    AureateAudioProcessorEditor editor (processor);

    // Character: 3-choice parameter, slider range 0..2 with interval 1 -
    // a 1% proportional step would collapse to zero under interval
    // snapping, so KeyboardSteps.h's one-interval fallback must kick in.
    auto* knob = findChildByTitle<juce::Slider> (editor, "Character");
    REQUIRE (knob != nullptr);

    knob->setValue (0.0, juce::sendNotificationSync);
    juce::Component& knobAsComponent = *knob;

    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (1.0).margin (1.0e-4));

    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-4));
}
