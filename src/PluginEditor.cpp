#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/ToggleZoneSwap.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <cmath>
#include <utility>

namespace
{
    // Base (@1x, 100% scale) faceplate geometry lives in PluginEditorLayout.h
    // (aurt::layout) rather than here, so tests/gui/EditorLayoutTests.cpp can
    // assert layout invariants against the exact constants this file lays
    // components out with.
    using namespace aurt::layout;

    struct KnobLayoutEntry
    {
        const char* parameterId;
        const char* labelText; // accessible name only - no baked text labels
        float cxMaster, cyMaster, rMaster; // true measured knob geometry (crop source, see layoutManifest provenance in PluginEditorLayout.h)
        int cx1x; // interactive slider hit-area X centre (Y comes from isUpperRow's shared row constant)
        bool isUpperRow;
    };

    // Mapping decided for the M3 GUI pilot (docs/gui-mapping.md has the full
    // rationale table): row 1 (flanking the VU dial) carries the four
    // controls reached for on every use of the plugin - Drive/Warmth/Tone/
    // Output. Row 2 carries the next tier - Mix/Bias/Character plus the
    // v0.3.0 Glue section's two most load-bearing continuous controls
    // (Threshold, Makeup) and Iron. Master px geometry from
    // brand/mocks/tubecomp/layout-manifest.json's own "knobs" array (index
    // 1-10, reading order upper-then-lower) - EXCEPT Bias/Glue Threshold/
    // Iron (knobs 6/8/10), whose manifest entries are flagged
    // "confidence": "estimated" (weak shadow-ring fits) and were found,
    // during this pass's own visual QA of docs/gui-preview.png, to be
    // measurably wrong - the manifest's (cx,cy) sat well outside the knob's
    // actual rendered position in master-03-clean-base.png, producing a
    // visible crescent/double-knob artifact (MasterCropKnob's rotating crop
    // sampled from empty plate steel next to the real knob). Re-measured
    // directly against master-03-clean-base.png for this pass (simple
    // Sobel-gradient circle search, not committed as a script - see this
    // revision's handoff notes) and visually verified to land cleanly on
    // each knob's own inner face/pointer-notch boundary; the corrected
    // triples are still the exact (cxMaster, cyMaster, rMaster, cx1x) shape
    // as every other entry.
    constexpr std::array<KnobLayoutEntry, 10> knobLayout {
        KnobLayoutEntry { ParamIDs::drive, "Drive", 306.0f, 493.0f, 51.0f, 200, true },
        KnobLayoutEntry { ParamIDs::warmth, "Warmth", 455.0f, 493.0f, 50.0f, 298, true },
        KnobLayoutEntry { ParamIDs::tone, "Tone", 923.0f, 493.0f, 50.0f, 604, true },
        KnobLayoutEntry { ParamIDs::output, "Output", 1072.0f, 493.0f, 51.0f, 701, true },
        KnobLayoutEntry { ParamIDs::mix, "Mix", 306.0f, 637.0f, 51.0f, 200, false },
        KnobLayoutEntry { ParamIDs::bias, "Bias", 451.0f, 642.0f, 40.0f, 295, false },
        KnobLayoutEntry { ParamIDs::character, "Character", 612.0f, 635.0f, 49.0f, 400, false },
        KnobLayoutEntry { ParamIDs::compThreshold, "Glue Threshold", 775.0f, 642.0f, 40.0f, 507, false },
        KnobLayoutEntry { ParamIDs::compMakeup, "Glue Makeup", 924.0f, 637.0f, 51.0f, 604, false },
        KnobLayoutEntry { ParamIDs::iron, "Iron", 1092.0f, 648.0f, 38.0f, 714, false },
    };

    struct ToggleLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        int zoneIndex; // into aurt::layout::toggleZone1x / the toggle-N-down.png BinaryData table below
    };

    // Suite convention (ToggleZoneSwap.h): the master bakes every toggle
    // lever UP, and UP always means "this parameter's own APVTS default".
    // Every one of these four parameters defaults to its own index/state 0
    // (compEnable=false, autoGain=false, compModel="VCA"=index0,
    // quality="Classic"=index0), so every toggle's UP pose matches its
    // parameter's default with no mismatch/pop at construction.
    constexpr std::array<ToggleLayoutEntry, 4> toggleLayout {
        ToggleLayoutEntry { ParamIDs::compEnable, "Glue", 0 },
        ToggleLayoutEntry { ParamIDs::autoGain, "Auto Gain", 1 },
        ToggleLayoutEntry { ParamIDs::compModel, "Glue Model", 2 },
        ToggleLayoutEntry { ParamIDs::quality, "Quality", 3 },
    };

    // Vent-glow breathing ballistics (SubtractiveGlow.h's stepGlowMix()) -
    // driven from the same gain-reduction reading the needle uses (see
    // needleDbFromGrDb() below): idle (no gain reduction) breathes around
    // t~=0.85, rising to the hard t=1.0 ceiling as the Glue section engages
    // more heavily. Independent of the needle's own dB scale/direction -
    // this is a coarse "is the section working" indicator, not a precision
    // meter.
    constexpr float ventGlowTauSeconds = 0.15f;
    constexpr float ventGlowFloorDb = 0.0f;
    constexpr float ventGlowCeilingDb = 6.0f;
    constexpr float ventGlowIdleBreathCentre = 0.85f;
    constexpr float ventGlowIdleBreathHalfRange = 0.06f;
    constexpr float ventGlowPhaseSeed = 5.0f;

    // The VU needle displays the processor's own gain-reduction reading
    // (AureateAudioProcessor::getCurrentGrDb(), already atomic/real-time-safe
    // - see PluginProcessor.h's docs), NEGATED so idle (0dB GR) rests on the
    // dial's own "0" tick and increasing gain reduction swings the needle
    // toward the dial's negative labels (read as "-N dB of gain reduction"),
    // the same convention classic hardware bus-compressor GR meters use.
    // Clamped to the dial's own measured tick range - the dial's +1/+2/+3
    // (red) zone is structurally unreachable under this mapping (gain
    // reduction is never negative), a known, documented consequence of
    // reusing a bidirectional VU faceplate for a unidirectional GR reading.
    float needleDbFromGrDb (float grDb) noexcept
    {
        return juce::jlimit (-20.0f, 3.0f, -grDb);
    }

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame: selects German (resources/i18n/de.txt) or falls through
    // to English, once, at editor construction - see Localisation.h's docs.
    // `presetBar` is a member initialised via the constructor's initialiser
    // list, and its own constructor already calls TRANS() on every button
    // label - member initialisers run in declaration order regardless of the
    // order they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (AureateAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state.
    constexpr const char* uiScaleStepProperty = "uiScaleStep";
}

AureateAudioProcessorEditor::AureateAudioProcessorEditor (AureateAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    masterImage = loadImage (BinaryData::master_tubecomp_png, BinaryData::master_tubecomp_pngSize);

    // Creation order doubles as the accessibility/keyboard focus order
    // (JUCE's default FocusTraverser walks children in z-order, i.e.
    // creation order) - kept matching visual reading order: preset bar +
    // scale control, the needle/meter, the knob grid row-by-row, then the
    // four toggles.
    addAndMakeVisible (presetBar);

    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    basilica::gui::HubNeedle::Assets needleAssets;
    needleAssets.needleSprite = loadImage (BinaryData::needle_tubecomp_png, BinaryData::needle_tubecomp_pngSize);
    needle = std::make_unique<basilica::gui::HubNeedle> (
        needleAssets, "Gain Reduction meter",
        needleSpritePivotFraction, needleSpritePivotFraction,
        needleSpriteSizeFraction, needleBakedAngleDeg);
    addAndMakeVisible (*needle);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::MasterCropKnob> (
            masterImage, juce::Point<float> (entry.cxMaster, entry.cyMaster), entry.rMaster);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    const struct
    {
        const char* data;
        int size;
    } toggleDownAssets[4] = {
        { BinaryData::toggle_1_down_png, BinaryData::toggle_1_down_pngSize },
        { BinaryData::toggle_2_down_png, BinaryData::toggle_2_down_pngSize },
        { BinaryData::toggle_3_down_png, BinaryData::toggle_3_down_pngSize },
        { BinaryData::toggle_4_down_png, BinaryData::toggle_4_down_pngSize },
    };

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        auto& entry = toggleLayout[i];
        toggles[i].button = std::make_unique<juce::ToggleButton> (juce::String());
        toggles[i].button->setColour (juce::ToggleButton::tickColourId, juce::Colours::transparentBlack);
        toggles[i].button->setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::transparentBlack);
        toggles[i].button->setColour (juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
        toggles[i].otherPositionZoneImage =
            loadImage (toggleDownAssets[entry.zoneIndex].data, toggleDownAssets[entry.zoneIndex].size);
        configureToggle (toggles[i], entry.parameterId, entry.labelText);
    }

    auto glowImage = loadImage (BinaryData::vent_glow_tubecomp_png, BinaryData::vent_glow_tubecomp_pngSize);
    ventGlow = basilica::gui::SubtractiveGlow (
        masterImage, glowImage,
        { ventGlowZoneMasterPx.getX(), ventGlowZoneMasterPx.getY() }, 1.0f);
    ventGlowState.startTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    ventGlowMix = ventGlowIdleBreathCentre;

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));

    startTimerHz (30);
}

AureateAudioProcessorEditor::~AureateAudioProcessorEditor() = default;

void AureateAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider->setPopupDisplayEnabled (true, true, this);
    knob.slider->setTitle (labelText);
    knob.slider->setName (labelText);
    addAndMakeVisible (*knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        const auto defaultValue = param->getNormalisableRange().convertFrom0to1 (param->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);
    }

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below - JUCE 8.0.14's SliderParameterAttachment constructor
    // itself assigns slider.textFromValueFunction as part of wiring the
    // attachment, which would silently clobber an override set beforehand.
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        knob.slider->textFromValueFunction = [param] (double v)
        {
            return param->getText (param->convertTo0to1 ((float) v), 0) + " " + param->getLabel();
        };
        knob.slider->updateText();
    }
}

void AureateAudioProcessorEditor::configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText)
{
    toggle.button->setTitle (labelText);
    toggle.button->setName (labelText);
    addAndMakeVisible (*toggle.button);

    toggle.attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, *toggle.button);
}

void AureateAudioProcessorEditor::cycleScale()
{
    applyScaleStep ((scaleStepIndex + 1) % (int) scaleSteps.size());
}

void AureateAudioProcessorEditor::applyScaleStep (int newStepIndex)
{
    scaleStepIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, newStepIndex);
    audioProcessor.apvts.state.setProperty (uiScaleStepProperty, scaleStepIndex, nullptr);

    const auto percentText = juce::String ((int) (scaleSteps[(size_t) scaleStepIndex] * 100.0f)) + "%";
    scaleButton.setButtonText (percentText);
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];

    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

void AureateAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (float v) { return v * scale; };

    const auto stripHeight = (float) topStripHeight1x * scale;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17141a), 0.0f, 0.0f,
                                             juce::Colour (0xff0b090d), 0.0f, stripHeight, false));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), stripHeight));
    g.setColour (juce::Colour (0xff5a4420));
    g.fillRect (juce::Rectangle<float> (0.0f, stripHeight - 1.0f * scale, (float) getWidth(), 1.0f * scale));

    const auto plateOrigin = juce::Point<float> (0.0f, stripHeight + (float) topStripGap1x * scale);
    const auto plateBounds = juce::Rectangle<float> (plateOrigin.x, plateOrigin.y,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    const auto toScreenRect = [&] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<float> (plateOrigin.x + s ((float) local1x.getX()),
                                       plateOrigin.y + s ((float) local1x.getY()),
                                       s ((float) local1x.getWidth()),
                                       s ((float) local1x.getHeight()));
    };

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1. Baseline plate: the single master render, filling the plate bounds.
    // Bakes the steel plate, wooden cheeks, handle, empty VU dial, all 10
    // knobs at 12 o'clock, all 4 toggles UP, tube vents at full glow, and the
    // 3 blank brass nameplates - nothing else is drawn for any of those
    // elements.
    if (masterImage.isValid())
        g.drawImage (masterImage, plateBounds, juce::RectanglePlacement::centred, false);

    // (2. The 10 knobs are separate MasterCropKnob child components, drawn
    // automatically after this method returns - see resized() for their
    // bounds and MasterCropKnob.cpp for the rotating-crop draw itself.)

    // 3. Toggle-zone overlay: for each toggle away from its own baked/
    // default UP pose, blit its own pre-cropped "other position" image over
    // the baseline just drawn - independently per toggle (see
    // ToggleZoneSwap.h's docs for the UP==default convention).
    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        const auto& zone = toggleZone1x[(size_t) toggleLayout[i].zoneIndex];
        const auto destRect = toScreenRect (juce::Rectangle<int> (zone.x, zone.y, zone.w, zone.h));
        const auto isAwayFromDefault = toggles[i].button->getToggleState();

        basilica::gui::ToggleZoneSwap::draw (g, toggles[i].otherPositionZoneImage, destRect, isAwayFromDefault);
    }

    // 4. Vent-glow layer (SUBTRACTIVE, see SubtractiveGlow.h) - a single
    // cross-blend of the whole glow-sprite footprint, ballistically driven
    // by updateVentGlowMix() (called from timerCallback()) or directly by
    // the test/preview hooks below.
    {
        const auto destRect = toScreenRect (ventGlowZone1x);
        ventGlow.drawZone (g, destRect, ventGlowMix);
    }

    // (The VU needle is a separate HubNeedle child component, drawn after
    // this method returns - see resized() for its bounds. Everything else -
    // wooden cheeks, handle, tube-vent grille structure, the VU dial face,
    // the knobs' own baked outer rim/specular highlight, the 3 blank brass
    // nameplates - stays BAKED in the master, no draw calls for any of it.)
}

void AureateAudioProcessorEditor::resized()
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)).reduced (0, s (2)));
    presetBar.setBounds (topStrip.reduced (0, s (2)));

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table in PluginEditorLayout.h), then offset by the top strip +
    // gap and scaled.
    const auto toPlatePoint = [&] (juce::Point<int> plateLocal)
    {
        return juce::Point<int> (s (plateLocal.x),
                                 s (topStripHeight1x + topStripGap1x) + s (plateLocal.y));
    };

    const auto meterSize = s (meterComponentSize1x);
    const auto meterTopLeftScreen = toPlatePoint (meterTopLeft1x);
    needle->setBounds (meterTopLeftScreen.x, meterTopLeftScreen.y, meterSize, meterSize);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        const auto diameter = s (entry.isUpperRow ? knobDiameterUpper1x : knobDiameterLower1x);
        const auto rowY = entry.isUpperRow ? knobRowUpperY1x : knobRowLowerY1x;

        knobs[i].slider->setBounds (juce::Rectangle<int> (diameter, diameter)
                                        .withCentre (toPlatePoint ({ entry.cx1x, rowY })));
    }

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        const auto& zone = toggleZone1x[(size_t) toggleLayout[i].zoneIndex];
        const auto topLeft = toPlatePoint ({ zone.x, zone.y });
        toggles[i].button->setBounds (topLeft.x, topLeft.y, s (zone.w), s (zone.h));
    }

    // Vent-glow repaint region: recomputed here so timerCallback()'s
    // per-tick repaint() call only invalidates this area rather than the
    // whole plate.
    const auto toPlateRect = [&] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<int> (toPlatePoint (local1x.getPosition()), toPlatePoint (local1x.getBottomRight()));
    };

    ventGlowRepaintBounds = toPlateRect (ventGlowZone1x).expanded (s (4));
}

void AureateAudioProcessorEditor::updateVentGlowMix() noexcept
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    constexpr float dt = 1.0f / 30.0f;

    ventGlowMix = basilica::gui::stepGlowMix (
        ventGlowState, audioProcessor.getCurrentGrDb(), dt, now,
        ventGlowTauSeconds, ventGlowFloorDb, ventGlowCeilingDb,
        ventGlowIdleBreathCentre, ventGlowIdleBreathHalfRange, ventGlowPhaseSeed);
}

void AureateAudioProcessorEditor::timerCallback()
{
    needle->setTargetDb (needleDbFromGrDb (audioProcessor.getCurrentGrDb()));
    needle->tick (1.0f / 30.0f);

    updateVentGlowMix();
    repaint (ventGlowRepaintBounds);
}

void AureateAudioProcessorEditor::recomputeVentGlowForPreview() noexcept
{
    updateVentGlowMix();
    repaint (ventGlowRepaintBounds);
}

void AureateAudioProcessorEditor::setVentGlowElapsedSecondsForPreview (double elapsedSeconds) noexcept
{
    ventGlowState.startTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - elapsedSeconds;
    updateVentGlowMix();
    repaint (ventGlowRepaintBounds);
}

void AureateAudioProcessorEditor::setVentGlowMixForPreview (float t) noexcept
{
    ventGlowMix = juce::jlimit (0.0f, 1.0f, t);
    repaint (ventGlowRepaintBounds);
}
