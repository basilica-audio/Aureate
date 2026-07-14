#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AureateAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per float parameter
// (plus a combo box for the Character choice parameter), bound to the APVTS
// via SliderAttachment/ComboBoxAttachment, laid out in a wrapping grid in
// signal-flow order. A custom vector-drawn GUI is a later milestone; this is
// deliberately plain but fully wired and usable.
class AureateAudioProcessorEditor final : public juce::AudioProcessorEditor
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
    // combo box rather than a rotary knob.
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);

    AureateAudioProcessor& audioProcessor;

    // In signal-flow order (see docs/architecture.md).
    Knob wowFlutterKnob;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateAudioProcessorEditor)
};
