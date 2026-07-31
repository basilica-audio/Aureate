#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"
#include "gui/MasterCropKnob.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// GUI smoke tests for the M3 photoreal "tubecomp" editor (src/PluginEditor.h,
// src/gui/). juce::ScopedJuceInitialiser_GUI is installed once for the whole
// test binary in tests/TestMain.cpp, so Components/Timers are safe to
// construct here even though this is a headless console executable with no
// running message loop (timers simply never fire, which is fine - these
// tests only exercise synchronous construction/paint/destruction).
TEST_CASE ("Editor constructs, lays out, and destroys cleanly", "[gui]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    {
        AureateAudioProcessorEditor editor (processor);

        CHECK (editor.getWidth() > 0);
        CHECK (editor.getHeight() > 0);
    }
    // editor destroyed here - JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (used throughout src/gui/ and on the editor itself) asserts at process
    // exit in Debug builds if any tagged instance was ever leaked, so a
    // clean run of this whole test binary is itself the leak check.
}

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

    // Configures a deliberately "alive-looking" state before snapshotting,
    // per the M3 GUI briefing: the needle deflected off its idle "0" tick,
    // one toggle away from its baked/default UP pose (exercising the
    // toggle-zone crop-swap overlay), the vent glow partway between fully
    // dim and its baked ceiling, and the knob grid at varied, non-default
    // rotations across both rows.
    //
    // HubNeedle's own ~250ms ballistic ramp and the editor's own
    // timerCallback()-driven vent-glow ballistics would need real timer
    // ticks pumped through a running message loop to actually reach these
    // values - this headless test binary has no such loop, so the
    // test/preview-only hooks (setImmediateDbForPreview()/
    // setVentGlowMixForPreview()) seed the readings directly instead.
    void configureLiveLookingState (AureateAudioProcessorEditor& editor)
    {
        if (auto* needle = findChildByTitle<basilica::gui::HubNeedle> (editor, "Output Level meter"))
            needle->setImmediateDbForPreview (-7.0f);

        if (auto* glue = findChildByTitle<juce::ToggleButton> (editor, "Glue"))
            glue->setToggleState (true, juce::dontSendNotification); // away from default -> toggle-zone crop-swap overlay

        editor.setVentGlowMixForPreview (0.7f);

        struct KnobValue
        {
            const char* label;
            double normalisedValue;
        };

        const KnobValue knobValues[] = {
            { "Drive", 0.20 }, { "Warmth", 0.70 }, { "Tone", 0.30 }, { "Output", 0.80 },
            { "Mix", 0.50 }, { "Bias", 0.65 }, { "Character", 1.0 }, { "Glue Threshold", 0.40 },
            { "Glue Makeup", 0.75 }, { "Iron", 0.90 },
        };

        for (const auto& kv : knobValues)
            if (auto* knob = findChildByTitle<juce::Slider> (editor, kv.label))
                knob->setValue (knob->proportionOfLengthToValue (kv.normalisedValue), juce::dontSendNotification);
    }
}

TEST_CASE ("Editor snapshot at 100% is non-blank and is written for PR review", "[gui]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    AureateAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    configureLiveLookingState (editor);

    // SoftwareImageType (rather than the default NativeImageType) avoids any
    // dependency on an actual native graphics context/window, which keeps
    // this robust on headless CI runners.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});

    REQUIRE (snapshot.isValid());
    CHECK (snapshot.getWidth() == editor.getWidth());
    CHECK (snapshot.getHeight() == editor.getHeight());

    // Non-blank: sample a small grid of points and confirm they are not all
    // identical to the top-left corner - a completely blank/solid-fill
    // render (e.g. every asset failing to decode) would fail this.
    const auto reference = snapshot.getPixelAt (0, 0);
    bool foundDifference = false;

    for (int y = 0; y < snapshot.getHeight() && ! foundDifference; y += juce::jmax (1, snapshot.getHeight() / 20))
        for (int x = 0; x < snapshot.getWidth() && ! foundDifference; x += juce::jmax (1, snapshot.getWidth() / 20))
            if (snapshot.getPixelAt (x, y) != reference)
                foundDifference = true;

    CHECK (foundDifference);

#ifdef AUREATE_DOCS_DIR
    // Committed directly for PR review (docs/gui-preview.png).
    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File (AUREATE_DOCS_DIR).getChildFile ("gui-preview.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (snapshot, *stream));
    }
    else
    {
        FAIL ("could not open output stream for " << outFile.getFullPathName());
    }
#endif
}

// Proof that MasterCropKnob's rotating crop actually moves: two knobs
// (one per row) set to distinctly non-rest proportions must visibly differ,
// within their own bounds, from their construction-time (APVTS-default)
// rendering.
TEST_CASE ("MasterCropKnob instances visibly rotate at distinctly non-default values", "[gui]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    AureateAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    const auto restSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (restSnapshot.isValid());

    struct ZoomKnob
    {
        const char* label;
        double proportion;
    };

    constexpr ZoomKnob zoomKnobs[] = {
        { "Drive", 0.05 },  // row 1
        { "Iron", 0.95 },   // row 2
    };

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);
        knob->setValue (knob->proportionOfLengthToValue (zk.proportion), juce::dontSendNotification);
    }

    const auto movedSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (movedSnapshot.isValid());

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);

        const auto cropBounds = knob->getBounds().expanded (4);
        const auto restCrop = restSnapshot.getClippedImage (cropBounds);
        const auto movedCrop = movedSnapshot.getClippedImage (cropBounds);

        REQUIRE (restCrop.isValid());
        REQUIRE (movedCrop.isValid());
        REQUIRE (restCrop.getWidth() == movedCrop.getWidth());
        REQUIRE (restCrop.getHeight() == movedCrop.getHeight());

        int changedPixels = 0;
        const int totalPixels = restCrop.getWidth() * restCrop.getHeight();

        for (int y = 0; y < restCrop.getHeight(); ++y)
        {
            for (int x = 0; x < restCrop.getWidth(); ++x)
            {
                const auto a = restCrop.getPixelAt (x, y);
                const auto b = movedCrop.getPixelAt (x, y);
                const auto diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                                 + std::abs (a.getBlue() - b.getBlue());
                if (diff > 24)
                    ++changedPixels;
            }
        }

        INFO (zk.label << ": " << changedPixels << "/" << totalPixels << " px changed between rest and moved pose");
        CHECK (changedPixels > totalPixels / 20); // >5% of the crop visibly moved
    }
}

// Item 5-style idle-breathing proof: at true silence (fresh processor,
// never processBlock()'d), the vent glow must still be visibly time-varying
// (never reads as flatly "off") - see SubtractiveGlow.h/PluginEditor.cpp's
// ventGlowIdleBreath* constants.
TEST_CASE ("Vent glow idle breathing is visibly time-varying at silence", "[gui]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    AureateAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    editor.setVentGlowElapsedSecondsForPreview (5.0);
    const auto frame1 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame1.isValid());

    editor.setVentGlowElapsedSecondsForPreview (11.0);
    const auto frame2 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame2.isValid());

    REQUIRE (frame1.getWidth() == frame2.getWidth());
    REQUIRE (frame1.getHeight() == frame2.getHeight());

    long long diffEnergy = 0;

    for (int y = 0; y < frame1.getHeight(); ++y)
    {
        for (int x = 0; x < frame1.getWidth(); ++x)
        {
            const auto a = frame1.getPixelAt (x, y);
            const auto b = frame2.getPixelAt (x, y);
            diffEnergy += std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                        + std::abs (a.getBlue() - b.getBlue());
        }
    }

    INFO ("total diff energy between the two idle vent-glow frames = " << diffEnergy);
    CHECK (diffEnergy > 0);
}
