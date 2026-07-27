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

    // v0.2.0 (docs/design-brief.md §3.6): the single v0.1.0 "Wow/Flutter"
    // proportion was split into two independent amounts - a slow tape-
    // transport pitch "wow" and a faster "flutter" shimmer - applied to the
    // wet path via a shared modulated delay line. 0% on both is a fixed
    // (non-modulated) delay - see AureateEngine for why a small fixed delay
    // is always present regardless of either amount.
    inline constexpr auto wow = "wow";
    inline constexpr auto flutter = "flutter";

    // FROZEN, retired v0.1.0 parameter ID - NOT a live APVTS parameter in
    // v0.2.0's ParameterLayout (superseded by wow/flutter above). Kept as a
    // named constant purely so AureateAudioProcessor::setStateInformation()'s
    // v0.1.0->v0.2.0 state migration can recognise a pre-split saved state
    // (a single <PARAM id="wow_flutter" .../> entry) and map it onto both new
    // parameters at the same value (docs/design-brief.md §7's migration
    // policy) rather than silently resetting Wow/Flutter to 0% on load.
    inline constexpr auto legacyWowFlutter = "wow_flutter";

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

    //==========================================================================
    // v0.3.0 additions (the "Glue" release scope). APPENDED ONLY - the twelve
    // IDs above keep their exact order, ID string, range, default and skew, so
    // a v0.1.x/v0.2.x session loads unchanged and APVTS fills every ID below
    // with its neutral default (see AureateAudioProcessor::setStateInformation()
    // and its stateSchema handling).
    //
    // Every one of these eleven parameters is neutral at its default, and each
    // neutral value selects the *same code path* v0.2.1 had, not a
    // mathematically-equivalent one: comp_enable=false hard-gates the whole
    // compressor section (making comp_model/threshold/ratio/attack/release/
    // makeup/sc_hpf inert), iron=0 branch-skips the Iron stage entirely,
    // quality=Classic keeps v0.2.1's exact saturator sample path, and
    // auto_gain=false applies a gain of exactly 1.

    // Glue: master enable for the program-dependent bus-compressor section
    // (host rate, ahead of Drive). Off by default - a v0.2.1 session that has
    // never seen this parameter must sound bit-identical.
    inline constexpr auto compEnable = "comp_enable";

    // Glue Model: which detector/gain-cell law the section runs. "VCA" is the
    // dB-domain feedback timing network with the dummy-VCA loop; "Vari-Mu" is
    // the softplus dead-zone sidechain with the slew-limited rectifier and the
    // three-capacitor program-dependent release network. Generic circuit-class
    // names only (suite policy: no hardware brand names).
    inline constexpr auto compModel = "comp_model";

    // Glue Threshold, in dB relative to the plugin's -18 dBFS RMS "0 VU"
    // calibration point (docs/manual.md) - i.e. 0 dB here means a -18 dBFS RMS
    // sine sits exactly at threshold.
    inline constexpr auto compThreshold = "comp_threshold";

    // Glue Ratio / Attack / Release: stepped switches, mirroring the discrete
    // rotary switches of the hardware classes being modelled rather than
    // continuous controls. Release's last position is "Auto" (the
    // program-dependent dual-time-constant / three-capacitor network).
    inline constexpr auto compRatio = "comp_ratio";
    inline constexpr auto compAttack = "comp_attack";
    inline constexpr auto compRelease = "comp_release";

    // Glue Makeup: static gain applied to the compressor section's output.
    inline constexpr auto compMakeup = "comp_makeup";

    // Glue SC Filter: 1st-order high-pass in the *detector* path only (the
    // audio path is untouched), so low-frequency energy stops pumping the
    // whole mix. 20 Hz is a hard bypass, not merely a very low corner.
    inline constexpr auto compScHpf = "comp_sc_hpf";

    // Iron: amount of the flux-domain output-transformer stage (leaky flux
    // integrator -> saturating core -> matched differentiator, plus an LF
    // resonance bump and gentle HF rounding), inside the existing 4x
    // oversampled region right after the Character saturator. 0% is a hard
    // branch-skip.
    inline constexpr auto iron = "iron";

    // Quality: "Classic" is v0.2.1's exact saturator math; "HQ" swaps in
    // first-order antiderivative anti-aliasing on the same three Character
    // transfer functions, inside the same 4x oversampling and at the same
    // reported latency.
    inline constexpr auto quality = "quality";

    // Auto Gain: drive-compensated wet-path listening level, so A/B-ing Drive
    // is not just an A/B of loudness. Explicitly a listening aid, not loudness
    // matching (docs/manual.md).
    inline constexpr auto autoGain = "auto_gain";
}
