#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>

// Suite-reusable pivot-centred VU-needle overlay - the "tubecomp" faceplate
// family's needle component (basilica-audio wave: aureate/requiem/tenebrae/
// apotheosis all share this design and its master render).
//
// The dial FACE (plate, bezel, tick marks, "VU" wordmark) is baked into the
// design's master render (e.g. resources/gui/master_tubecomp.png) - this
// component draws ONLY the live needle sprite on top of it, rotated about
// the sprite's own measured hub pivot via a live juce::AffineTransform
// (never a pre-rotated frame stack - see needle.json's own provenance notes
// for why the master-extraction pipeline deliberately does not rotate the
// sprite to a canonical pose: doing so would resample and soften the
// master's own pixels).
//
// CRITICAL (binding rule, see the M3 GUI briefing): the sprite's pivot is
// the needle's HUB CENTRE, not the visible rod end - components/needle.json's
// own pivotXInMaster/pivotYInMaster fields already encode this, and this
// component's pivotXFraction/pivotYFraction constructor parameters must be
// derived from that same point (never the rod end), or the needle base will
// visibly lift off its hub as it rotates.
namespace basilica::gui
{
    class HubNeedle : public juce::Component
    {
    public:
        struct Assets
        {
            // The master-extracted needle sprite (e.g. needle_tubecomp.png) -
            // PIVOT-CENTRED canvas (pivot sits at the sprite's own exact
            // canvas centre, fraction 0.5/0.5 - see needle.json's
            // pivotXFrac/pivotYFrac), so no additional pivot-offset maths is
            // needed when rotating it about its own centre.
            juce::Image needleSprite;
        };

        // pivotXFraction/pivotYFraction: where the needle's hub pivot sits,
        // as a fraction of this component's own local bounds - measured
        // once against the master render (see PluginEditorLayout.h's
        // needlePivot1x docs) and passed in here, so this component itself
        // carries no design-specific geometry beyond the dB->angle tick
        // table (which IS specific to the tubecomp master's own dial
        // artwork - see HubNeedle.cpp's `ticks` table docs).
        //
        // spriteSizeFraction: the needle sprite's own drawn diameter, as a
        // fraction of jmin(width,height) of this component's bounds.
        //
        // bakedAngleDegIn: the sprite's own rest pose in the master render
        // it was extracted from (needle.json's bakedAngleDeg) - rotation
        // applied each frame is (targetAngle - bakedAngleDegIn), NOT
        // targetAngle alone (see paint()'s docs).
        HubNeedle (Assets assetsIn, juce::String accessibleTitle,
                  float pivotXFraction, float pivotYFraction, float spriteSizeFraction,
                  float bakedAngleDegIn);
        ~HubNeedle() override;

        // Thread-safe (plain atomic store): the instantaneous value in dB,
        // written from the audio thread (or the editor's own polling
        // timer). Ballistic smoothing is applied separately, on the GUI
        // thread, so this is real-time safe to call from anywhere.
        void setTargetDb (float newTargetDb) noexcept { targetDb.store (newTargetDb, std::memory_order_relaxed); }

        // Advances the ballistic smoothing by dtSeconds and repaints if the
        // smoothed value changed meaningfully - called from the editor's own
        // timer (see PluginEditor.cpp), NOT owned internally by a
        // juce::Timer on this component, so headless tests can drive it
        // deterministically without a running message loop.
        void tick (float dtSeconds) noexcept;

        // Test/preview-only: seeds both the raw target and the ballistic-
        // smoothed reading to the same value immediately, bypassing the
        // ramp - mirrors basilica-audio/silentium's AnalogMeter::
        // setImmediateDbForPreview() rationale (headless test binaries have
        // no running message loop to pump real ticks through). Normal
        // operation never calls this.
        void setImmediateDbForPreview (float db) noexcept;

        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        // One-pole ballistic integration step, exposed as a pure/static
        // function so it is directly unit-testable without a running timer.
        static float stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept;

        // dB -> face-relative rotation angle in degrees, piecewise-linearly
        // interpolated across the tubecomp master's own measured tick table
        // (see HubNeedle.cpp's `ticks` docs and
        // analysis/measure_dial_ticks.py) and clamped beyond the table's
        // ends. Degrees are clockwise from straight-up (12 o'clock).
        static float tickAngleDegreesForDb (float db) noexcept;

        static constexpr float ballisticsTauSeconds = 0.25f;

    private:
        class ValueInterface;

        Assets assets;
        juce::String title;

        std::atomic<float> targetDb { 0.0f };
        float smoothedDb = 0.0f;

        const float pivotXFraction;
        const float pivotYFraction;
        const float spriteSizeFraction;
        const float bakedAngleDeg;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HubNeedle)
    };
}
