#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Aureate. See docs/architecture.md for the corresponding signal-flow
// diagram.
//
// FROZEN AS OF THE v0.1.0 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
namespace ParamIDs
{
    // Input gain into the oversampled tape-style saturation stage.
    inline constexpr auto drive = "drive";

    // "Warmth": jointly controls the saturator's asymmetry bias (tape-style
    // single-ended saturation character) and a gentle pre-clip high-frequency
    // rolloff (tape self-erasure/bias-oscillator character), both applied
    // inside the oversampled domain.
    inline constexpr auto warmth = "warmth";

    // Console-style tilt tone control: negative darkens (low-shelf boost +
    // high-shelf cut), positive brightens (the inverse), 0 is flat/unity.
    inline constexpr auto tone = "tone";

    // Dry/wet mix. At 0% the plugin is a delay-compensated passthrough of
    // the input (see AureateEngine's DryWetMixer usage).
    inline constexpr auto mix = "mix";

    // Final output trim, applied after the dry/wet mix (a master trim on the
    // combined signal, unlike Drive which only affects the wet path).
    inline constexpr auto output = "output";

    // Additional saturator asymmetry trim, independent of (and added to)
    // Warmth's own bias contribution - lets a session dial in more/less
    // single-ended character without disturbing Warmth's HF-rolloff amount.
    inline constexpr auto bias = "bias";

    // Amount of tape-transport speed instability (slow "wow" + faster
    // "flutter" pitch modulation) applied to the wet path via a modulated
    // delay line. 0% is a fixed (non-modulated) delay - see AureateEngine
    // for why a small fixed delay is always present regardless of amount.
    inline constexpr auto wowFlutter = "wow_flutter";

    // Amount of shaped noise ("tape hiss") mixed into the wet path inside
    // the oversampled domain, after the saturator/tone stages and before
    // downsampling (so it inherits the downsampler's anti-aliasing filter).
    inline constexpr auto hiss = "hiss";

    // Selects which saturation transfer-function family the Drive/Warmth/
    // Bias stage uses (see TapeSaturator::Model): Tape (default), Console,
    // or Valve.
    inline constexpr auto character = "character";

    // Fixed-frequency high-shelf trim, independent of and in addition to
    // Tone's tilt shelves - a finer top-end adjustment at a higher corner
    // frequency than Tone's high shelf.
    inline constexpr auto hfTrim = "hf_trim";

    // Fixed-frequency low-shelf trim, independent of and in addition to
    // Tone's tilt shelves - a finer low-end adjustment at a lower corner
    // frequency than Tone's low shelf.
    inline constexpr auto lfTrim = "lf_trim";
}
