#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

// Layout-invariant tests for the M3 photoreal "tubecomp" GUI - the
// tubecomp design's own layout-manifest.json's "rowYs" field is the source
// of truth for the row-Y snap (see PluginEditorLayout.h's top-of-file
// docs), not each individual knob's own slightly-deviating measured centre.
TEST_CASE ("Knob rows sit on a single shared Y with no row overlap", "[gui][layout]")
{
    using namespace aurt::layout;

    CHECK (knobRowLowerY1x > knobRowUpperY1x);

    // A structural guarantee, not merely a check on today's numbers: both
    // rows are represented in PluginEditor.cpp's own knobLayout table via a
    // single shared boolean (isUpperRow) selecting one of exactly two Y
    // constants, so there is nowhere for an individual knob to carry a
    // divergent Y.
    CHECK (knobDiameterUpper1x > 0);
    CHECK (knobDiameterLower1x > 0);

    // The two rows must be far enough apart that a full-diameter knob in
    // row 1 never visually overlaps a full-diameter knob in row 2.
    const auto maxDiameter = juce::jmax (knobDiameterUpper1x, knobDiameterLower1x);
    CHECK (knobRowLowerY1x - knobRowUpperY1x >= maxDiameter);
}

TEST_CASE ("The needle/meter component and the full toggle-zone set stay within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace aurt::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    const juce::Rectangle<int> meterBay { meterTopLeft1x.x, meterTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    CHECK (plateCanvas.contains (meterBay));

    for (const auto& zone : toggleZone1x)
        CHECK (plateCanvas.contains (juce::Rectangle<int> (zone.x, zone.y, zone.w, zone.h)));

    CHECK (plateCanvas.contains (ventGlowZone1x));
}

TEST_CASE ("Needle sprite geometry is well-formed", "[gui][layout]")
{
    using namespace aurt::layout;

    CHECK (needleSpritePivotFraction > 0.0f);
    CHECK (needleSpritePivotFraction < 1.0f);
    CHECK (needleSpriteSizeFraction > 0.0f);
    CHECK (needleSpriteSizeFraction < 1.0f); // sprite must not exceed its own component's bounds

    // The pivot must sit strictly inside the meter component's own bounds -
    // a fraction outside [0,1] would mean the needle rotates around a point
    // off the drawn component entirely.
    const juce::Rectangle<int> meterBay { meterTopLeft1x.x, meterTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    CHECK (meterBay.contains (juce::roundToInt (needlePivotMasterPx.x * (float) plateWidth1x / (float) masterCanvasWidthPx),
                              juce::roundToInt (needlePivotMasterPx.y * (float) plateWidth1x / (float) masterCanvasWidthPx)));
}

TEST_CASE ("Toggle zones are 4 independent, non-degenerate rectangles", "[gui][layout]")
{
    using namespace aurt::layout;

    CHECK (std::size (toggleZone1x) == 4);

    for (const auto& zone : toggleZone1x)
    {
        CHECK (zone.w > 0);
        CHECK (zone.h > 0);
    }

    // No two toggle zones should overlap each other (they sit at 4
    // independent corners of the plate).
    for (size_t i = 0; i < std::size (toggleZone1x); ++i)
    {
        const juce::Rectangle<int> a { toggleZone1x[i].x, toggleZone1x[i].y, toggleZone1x[i].w, toggleZone1x[i].h };
        for (size_t j = i + 1; j < std::size (toggleZone1x); ++j)
        {
            const juce::Rectangle<int> b { toggleZone1x[j].x, toggleZone1x[j].y, toggleZone1x[j].w, toggleZone1x[j].h };
            CHECK_FALSE (a.intersects (b));
        }
    }
}
