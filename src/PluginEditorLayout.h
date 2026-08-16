#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Aureate's own @1x faceplate/control-bay geometry table for the M3
// photoreal "tubecomp" GUI - lives in its own header, rather than as an
// anonymous-namespace block inside PluginEditor.cpp, so
// tests/gui/EditorLayoutTests.cpp can assert layout invariants directly
// against the SAME numbers PluginEditor.cpp actually lays components out
// with (basilica-audio/silentium's own PluginEditorLayout.h convention,
// copied here).
//
// Every constant below is derived from the tubecomp design's own measured
// provenance (repo-relative to the suite root, one level above this repo):
//   - brand/mocks/tubecomp/layout-manifest.json - plate/knob/toggle/screw
//     geometry, measured against master-01-base.png (an earlier render
//     generation than master-03-clean-base.png, which this build actually
//     ships - see the "PROVENANCE ASSUMPTION" note below).
//   - brand/mocks/tubecomp/components/toggles.json - the 4 toggle zones,
//     measured DIRECTLY against master-03-clean-base.png (the shipped
//     background), via quadrant-warp registration against
//     master-04-toggles-down.png.
//   - brand/mocks/tubecomp/components/needle.json - the needle hub pivot.
//   - brand/mocks/tubecomp/components/vent-glow.json - the vent-glow
//     layer's own canvas offset/size within the master.
//
// PROVENANCE ASSUMPTION (flagged explicitly, not silently accepted): the
// plate/knob/screw geometry in layout-manifest.json was measured against
// master-01-base.png, one render generation earlier than
// master-03-clean-base.png (this build's actual production background).
// The tubecomp design's own later artifacts (toggles.json, vent-glow.json,
// needle.json) all measure DIRECTLY against master-03-clean-base.png and
// agree with layout-manifest.json's plate/knob positions to visual
// inspection (see analysis/dial_verify.png's needle-tick cross-check,
// which used needle.json's master-03-registered pivot together with
// master-03-clean-base.png itself and lined up exactly) - i.e. the
// underlying plate/knob/screw layout appears IDENTICAL across the
// master-01..master-05 render generations (only the rendered CONTENT at
// each control changes generation to generation, not its position), the
// same "shared geometry across render generations" property
// basilica-audio/silentium's own master-01..master-06 family has. Not
// independently re-measured against master-03-clean-base.png pixel-for-
// pixel in this pass - re-run a knob/screw HoughCircles pass directly
// against master-03-clean-base.png if this assumption is ever in doubt.
namespace aurt::layout
{
    // Master render's own canvas size (brand/mocks/tubecomp/*.json's own
    // "imageSize"/"imageSize.w/h" fields, consistent across every
    // tubecomp-family artifact) - the scale factor below is
    // plateWidth1x / masterCanvasWidthPx.
    constexpr int masterCanvasWidthPx = 1376;
    constexpr int masterCanvasHeightPx = 768;

    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 502; // masterCanvasHeightPx scaled by the same factor as plateWidth1x

    //==========================================================================
    // Knob grid: 4 upper (flanking the VU dial) + 6 lower. Two separate
    // tables, same convention as silentium's knobLayout/knobDiscLayout split:
    //   - knobSlider1x (below, in PluginEditor.cpp's knobLayout table): the
    //     INTERACTIVE juce::Slider hit-area centre, row-snapped to a single
    //     shared Y per row (layout-manifest.json's own "rowYs" field is the
    //     source of truth for this snap - see its own
    //     "layoutInvariantViolations" note: the lower row's individual knob
    //     centres deviate up to 8px from that shared Y in the raw
    //     measurement, well within a ~45-51px knob radius, so snapping to
    //     the row's own mean Y is imperceptible and keeps the row-alignment
    //     invariant structurally guaranteed rather than approximately true).
    //   - knobMasterGeometry1x (PluginEditor.cpp): each knob's own true
    //     sub-pixel measured (cx, cy, r) in MASTER PIXELS (not row-snapped),
    //     used only to build MasterCropKnob's feathered crop from the exact
    //     baked art location - never for hit-testing.
    constexpr int knobRowUpperY1x = 322; // 493 master px * scale, rounded
    constexpr int knobRowLowerY1x = 415; // 635 master px * scale (rowYs mean_cy), rounded

    // Row-shared slider hit-area diameters - deliberately DIFFERENT between
    // the two rows (unlike silentium's single shared knobDiameter1x): the
    // manifest's own measured radii genuinely differ, upper row mean ~50.5
    // master px vs lower row mean ~47.8 master px (knob6/8/10 flagged
    // "estimated" - weak shadow-ring fits, see layout-manifest.json's own
    // notes - but still close enough that a single row diameter reads
    // cleanly against every knob in that row).
    constexpr int knobDiameterUpper1x = 66;
    constexpr int knobDiameterLower1x = 63;

    //==========================================================================
    // Toggle zones: 4 independent corners (NOT a shared row, unlike
    // silentium's 2-toggle footer design) - each zone is a full (x,y,w,h)
    // rect in master px (components/toggles.json's own "zones" field,
    // already registered/padded per-toggle), converted here to the @1x
    // table by the same masterCanvasWidthPx-based scale.
    struct ToggleZoneRect1x
    {
        int x, y, w, h;
    };

    // Numbering matches toggles.json's own "numbering" field (reading
    // order: 1=upper-left, 2=upper-right, 3=lower-left, 4=lower-right).
    constexpr ToggleZoneRect1x toggleZone1x[4] {
        { 107, 259, 80, 105 },  // toggle-1, upper-left
        { 718, 287, 85, 63 },   // toggle-2, upper-right
        { 58, 394, 129, 78 },   // toggle-3, lower-left
        { 715, 386, 126, 82 },  // toggle-4, lower-right
    };

    //==========================================================================
    // VU needle: HubNeedle's own component bounds, centred directly on the
    // measured hub pivot (components/needle.json's pivotXInMaster/
    // pivotYInMaster, 693.51/364.0 master px) - unlike an AnalogMeter-style
    // "dial bounding box" (this component draws ONLY the needle, never the
    // dial face, which is fully baked into the master), simplest correct
    // choice is to centre the component squarely on its own pivot.
    // meterComponentSize1x (380 master px, generous margin over the
    // needle's own 260x260 sprite canvas - see needle.json's spriteSize -
    // so the sprite's full rotation sweep, up to its own canvas diagonal,
    // never clips against the component's bounds) converts to 249 @1x.
    // juce::Point/Rectangle's constructors are not constexpr in JUCE 8.0.14
    // (verified against basilica-audio/silentium's own PluginEditorLayout.h
    // docs) - plain `const` namespace-scope values instead, still
    // initialised exactly once, well before any constructor runs.
    const juce::Point<float> needlePivotMasterPx { 693.51f, 364.0f };
    constexpr int meterComponentSize1x = 249;
    const juce::Point<int> meterTopLeft1x { 329, 114 }; // needlePivotMasterPx - meterComponentSize/2, scaled

    constexpr float needleSpritePivotFraction = 0.5f; // needle.json pivotXFrac/pivotYFrac - pivot at the sprite's own canvas centre
    constexpr float needleSpriteSizeFraction = 170.0f / (float) meterComponentSize1x; // spriteSize(260 master px) * scale / meterComponentSize1x
    constexpr float needleBakedAngleDeg = -0.912f; // needle.json bakedAngleDeg

    //==========================================================================
    // Vent-glow layer: the glow sprite's own canvas footprint within the
    // master (vent-glow.json's offsetX/offsetY/width/height) - ONE rect
    // covering both vent banks and the (glow-transparent) gap between them,
    // see SubtractiveGlow.h's own docs for why a single bounding rect is
    // both simpler and provably safe (vent-glow.json's own
    // "ceilingCheckNote": alpha is confined to two x-sub-ranges that don't
    // reach the VU dial in between).
    const juce::Rectangle<int> ventGlowZoneMasterPx { 82, 102, 1206, 266 };
    const juce::Rectangle<int> ventGlowZone1x { 54, 67, 789, 174 };

    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };
}
