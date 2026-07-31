#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

#include "gui/HubNeedle.h"
#include "gui/MasterCropKnob.h"
#include "gui/SubtractiveGlow.h"
#include "presets/PresetBar.h"

class AureateAudioProcessor;

// M3 photoreal GUI (the "tubecomp" faceplate design) - PILOT implementation
// for the wave shared with sibling plugins requiem/tenebrae/apotheosis (see
// src/gui/'s HubNeedle/MasterCropKnob/SubtractiveGlow/ToggleZoneSwap, all
// built design-agnostic so those siblings can reuse them against their own
// master renders and geometry tables).
//
// Architecture, copied from basilica-audio/silentium's own master-baseline
// pattern (see that repo's PluginEditor.h top-of-file docs): a SINGLE baked
// master image (resources/gui/master_tubecomp.png) is the sole faceplate -
// steel plate, wooden cheeks, handle, empty VU dial, all 10 knobs baked at
// 12 o'clock, all 4 toggles baked UP, tube vents at full glow, 3 blank brass
// nameplates - and every dynamic element is a small, targeted live overlay
// drawn on top of it:
//   1. baseline master (paint())
//   2. 10x MasterCropKnob (own child components, each rotating a feathered
//      crop of its own knob's baked art)
//   3. 4x toggle-zone crop-swap (paint(), ToggleZoneSwap - only for toggles
//      away from their own baked/default pose)
//   4. vent-glow subtractive breathing (paint(), SubtractiveGlow)
//   5. HubNeedle (own child component, VU needle only - the dial face
//      itself stays fully baked)
class AureateAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit AureateAudioProcessorEditor (AureateAudioProcessor& processorToEdit);
    ~AureateAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Test/preview-only: mirrors basilica-audio/silentium's
    // setVentGlowMixForPreview()/setVentGlowElapsedSecondsForPreview() -
    // headless test binaries have no running message loop to pump real
    // timer ticks through (see tests/gui/EditorSnapshotTests.cpp's own
    // docs). Normal operation never calls these.
    void setVentGlowMixForPreview (float t) noexcept;
    void setVentGlowElapsedSecondsForPreview (double elapsedSeconds) noexcept;
    void recomputeVentGlowForPreview() noexcept;

private:
    void timerCallback() override;
    void updateVentGlowMix() noexcept;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::MasterCropKnob> slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
        juce::Image otherPositionZoneImage; // the pre-cropped "away from default" pose (toggle_N_down.png)
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();

    AureateAudioProcessor& audioProcessor;

    juce::Image masterImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    std::unique_ptr<basilica::gui::HubNeedle> needle;

    static constexpr int numKnobs = 10;
    std::array<Knob, numKnobs> knobs;

    static constexpr int numToggles = 4;
    std::array<Toggle, numToggles> toggles;

    basilica::gui::SubtractiveGlow ventGlow;
    float ventGlowMix = 1.0f;
    basilica::gui::GlowMixState ventGlowState;
    juce::Rectangle<int> ventGlowRepaintBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateAudioProcessorEditor)
};
