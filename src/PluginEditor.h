#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "presets/PresetBar.h"

#include <array>

class AureateAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per float parameter
// (plus a combo box for the Character choice parameter), bound to the APVTS
// via SliderAttachment/ComboBoxAttachment, laid out in a wrapping grid in
// signal-flow order. A custom vector-drawn GUI is a later milestone; this is
// deliberately plain but fully wired and usable.
class AureateAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit AureateAudioProcessorEditor (AureateAudioProcessor& processorToEdit);
    ~AureateAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // One knob + label per float parameter.
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // Character is a choice parameter (Tape/Console/Valve), so it gets a
    // combo box rather than a rotary knob. As of v0.3.0 the same shape also
    // serves Glue Model/Ratio/Attack/Release and Quality.
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    // v0.3.0: the two bool parameters (Glue, Auto Gain).
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);

    // Polls the processor's gain-reduction atomic for the read-only meter
    // below. Deliberately a timer rather than a listener: gain reduction is a
    // measurement that changes every block, not an event.
    void timerCallback() override;

    AureateAudioProcessor& audioProcessor;

    // M2 preset system (src/presets/PresetBar.h) - a horizontal strip
    // docked at the top of the editor. Constructed after the localisation
    // frame is installed (see the constructor) so its TRANS()'d strings
    // pick up the right language from the very first paint.
    basilica::presets::PresetBar presetBar;

    // In signal-flow order (see docs/architecture.md). Wow and Flutter are
    // independent parameters as of v0.2.0 (docs/design-brief.md §3.6).
    Knob wowKnob;
    Knob flutterKnob;
    Knob driveKnob;
    Knob warmthKnob;
    Knob biasKnob;
    Choice characterChoice;
    Knob toneKnob;
    Knob hfTrimKnob;
    Knob lfTrimKnob;
    Knob hissKnob;
    Knob mixKnob;
    Knob outputKnob;

    // v0.3.0. Minimal wiring into the existing generic editor - the photoreal
    // GUI is a separate milestone and lives on its own branches, so this adds
    // rows to the same grid rather than any new look and feel.
    Toggle compEnableToggle;
    Choice compModelChoice;
    Knob compThresholdKnob;
    Choice compRatioChoice;
    Choice compAttackChoice;
    Choice compReleaseChoice;
    Knob compMakeupKnob;
    Knob compScHpfKnob;
    Knob ironKnob;
    Choice qualityChoice;
    Toggle autoGainToggle;

    // Read-only gain-reduction readout. Not an APVTS parameter - it is a
    // measurement, and making it one would expose it to host automation, undo
    // history and preset serialisation, none of which mean anything here.
    juce::Label gainReductionLabel;
    juce::Label gainReductionValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateAudioProcessorEditor)
};
