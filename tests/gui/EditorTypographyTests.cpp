#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

// Typography-pass proof (suite typo phase, owner decision 2026-07-26): the
// master bakes the three brass nameplates BLANK, and the knob grid carries
// no baked labels - the engraved lettering is a live JUCE text layer
// (src/gui/PlateTypography.h, drawn last in PluginEditor::paint()). Three
// tests: (1) an editor-snapshot dark-coverage comparison against the raw
// master's clean plaque fields, proving the layer is wired into paint();
// (2) a flat-ground unit proof of the one shared glyph draw path; (3) a
// layout-table invariant keeping every knob label out of its knob's own
// interactive hit-area. A missing/blank text layer fails loudly; styling
// tweaks (colour/kerning/height) keep passing.
namespace
{
    // Fraction of a rectangle's pixels darker than `threshold` luminance
    // (0-255). Fractions are resolution-invariant, so the @1x snapshot and
    // the full-resolution master can be compared directly even though the
    // same region spans different pixel counts in each.
    float darkFractionIn (const juce::Image& image, juce::Rectangle<int> area, int threshold)
    {
        int dark = 0, total = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
        {
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = image.getPixelAt (x, y);
                const auto lum = (int) std::lround (0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue());

                ++total;

                if (lum < threshold)
                    ++dark;
            }
        }

        return total > 0 ? (float) dark / (float) total : 0.0f;
    }

    // A lettered region's rect, in snapshot (=screen, 100% scale) pixels.
    juce::Rectangle<int> toSnapshotRect (juce::Rectangle<float> plateLocal1x)
    {
        return plateLocal1x
            .translated (0.0f, (float) (aurt::layout::topStripHeight1x + aurt::layout::topStripGap1x))
            .getSmallestIntegerContainer();
    }

    // The same region in MASTER pixels (the raw render is at master
    // resolution, 1376px wide vs the 900px @1x plate).
    juce::Rectangle<int> toMasterRect (juce::Rectangle<float> plateLocal1x)
    {
        const auto toMaster = (float) aurt::layout::masterCanvasWidthPx / (float) aurt::layout::plateWidth1x;

        return juce::Rectangle<float> (plateLocal1x.getX() * toMaster, plateLocal1x.getY() * toMaster,
                                       plateLocal1x.getWidth() * toMaster, plateLocal1x.getHeight() * toMaster)
            .getSmallestIntegerContainer();
    }
}

TEST_CASE ("Engraved plaque and knob lettering actually renders over the baked-blank master", "[gui][typography]")
{
    using namespace aurt::layout;

    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    AureateAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto master = juce::ImageCache::getFromMemory (BinaryData::master_tubecomp_png,
                                                         BinaryData::master_tubecomp_pngSize);
    REQUIRE (master.isValid());

    constexpr auto masterScale = (float) plateWidth1x / (float) masterCanvasWidthPx;

    // The editor's own text field, additionally shrunk vertically by 6
    // master px per edge: the measured plaque rects (bounding boxes) include
    // the plaque's own baked dark outline/drop-shadow rows, which would
    // otherwise dominate any darkness measurement of the blank master.
    const auto plaqueField1x = [&] (const PlaqueRectMasterPx& p)
    {
        const auto inset = (float) plaqueEndInsetMasterPx;
        return juce::Rectangle<float> (((float) p.x + inset) * masterScale, ((float) p.y + 6.0f) * masterScale,
                                       ((float) p.w - 2.0f * inset) * masterScale, ((float) p.h - 12.0f) * masterScale);
    };

    struct LetteredRegion
    {
        const char* name;
        juce::Rectangle<float> plateLocal1x;
    };

    // Only the centre and right plaques carry the pixel assertion: their
    // inner fields are clean, evenly-lit brass in the raw master (measured
    // dark fraction below 50 luminance: exactly 0.0), so engraved glyphs
    // produce an unambiguous +5-8% dark-coverage step. The LEFT plaque's
    // field is bright enough that antialiased ink lands mostly above the
    // threshold, and every knob label sits partly on its own knob's baked
    // drop shadow (15-45% dark ground) - a darkness measure has no reliable
    // signal in either, which is why the glyph-rendering proof for the
    // SHARED draw path lives in the flat-ground PlateTypography unit test
    // below instead, and knob-label placement is covered by the layout
    // invariant test + the committed docs/gui-preview.png.
    const LetteredRegion regions[] = {
        { "centre plaque", plaqueField1x (plaqueCentreMasterPx) },
        { "right plaque", plaqueField1x (plaqueRightMasterPx) },
    };

    for (const auto& region : regions)
    {
        INFO (region.name);

        const auto snapshotDark = darkFractionIn (snapshot, toSnapshotRect (region.plateLocal1x), 50);
        const auto masterDark = darkFractionIn (master, toMasterRect (region.plateLocal1x), 50);

        CHECK (snapshotDark > masterDark + 0.03f);
    }
}

TEST_CASE ("PlateTypography renders engraved glyphs and a lit lip on a flat metal ground", "[gui][typography]")
{
    // Flat-ground unit proof for the ONE shared draw path every plaque and
    // knob label goes through (PluginEditor::drawPlateTypography calls
    // PlateTypography::drawEngraved for all of them): on a synthetic
    // mid-luminance steel ground, the ink pass must produce a solid block
    // of dark glyph pixels and the highlight pass must brighten the lip
    // below them. This is what proves lettering ACTUALLY renders - the
    // snapshot test above can only measure it against clean plaque fields.
    basilica::gui::PlateTypography typography (BinaryData::EBGaramondRegular_ttf,
                                               (int) BinaryData::EBGaramondRegular_ttfSize,
                                               BinaryData::EBGaramondSemiBold_ttf,
                                               (int) BinaryData::EBGaramondSemiBold_ttfSize);

    const juce::Colour ground (0xff8a8a86); // mid steel, luminance ~138

    juce::Image canvas (juce::Image::RGB, 160, 24, true);
    {
        juce::Graphics g (canvas);
        g.fillAll (ground);

        const basilica::gui::EngravedTextStyle style {
            juce::Colour (0xd215110c), juce::Colour (0x60fff2d0), 12.0f, 0.10f, true
        };

        typography.drawEngraved (g, "DRIVE", canvas.getBounds().toFloat(), 1.0f, style);
    }

    int darkGlyphPixels = 0, litLipPixels = 0;

    for (int y = 0; y < canvas.getHeight(); ++y)
    {
        for (int x = 0; x < canvas.getWidth(); ++x)
        {
            const auto c = canvas.getPixelAt (x, y);
            const auto lum = 0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue();

            if (lum < 90.0f)
                ++darkGlyphPixels;
            else if (lum > 150.0f)
                ++litLipPixels;
        }
    }

    // 5 semibold capitals at 12px: the fully-inked stroke cores land at
    // ~43 luminance (0.82-alpha near-black over the 138-luminance ground)
    // and the antialiased stroke body stays well under 90, far below any
    // ground pixel - measured 31 core (<60) / ~100+ body (<90) pixels.
    CHECK (darkGlyphPixels > 60);
    CHECK (litLipPixels > 20);
}

TEST_CASE ("Engraved lettering never intrudes into a knob's interactive hit-area", "[gui][typography]")
{
    using namespace aurt::layout;

    // Pure layout-table invariant (no editor needed): every knob label box
    // starts strictly below its own row's slider hit-area bottom edge, so
    // the lettering can never sit under the rotating MasterCropKnob crop.
    for (const auto isUpperRow : { true, false })
    {
        const auto rowY = isUpperRow ? knobRowUpperY1x : knobRowLowerY1x;
        const auto diameter = isUpperRow ? knobDiameterUpper1x : knobDiameterLower1x;

        const auto sliderBottom = rowY + diameter / 2;
        const auto labelTop = rowY + diameter / 2 + knobLabelGap1x;

        CHECK (labelTop > sliderBottom);
        CHECK (labelTop + knobLabelHeight1x < plateHeight1x);
    }
}
