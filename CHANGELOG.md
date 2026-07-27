# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-07-27

The "Glue" release: the program-dependent bus dynamics the plugin's name always implied,
plus a transformer stage, an anti-aliased quality mode, and drive-compensated listening.

Every one of the eleven new parameters is neutral at its default, and every neutral default
is a **branch-skip rather than a transparent setting** - the Glue section returns before
touching a sample when disabled, the Iron stage is stepped over at 0%, Classic quality still
runs v0.2.1's own saturator loop, and Auto Gain off applies no gain rather than a gain of 1.0.
An existing session therefore plays back **bit-identically**, and reported latency is unchanged
at every setting and every sample rate.

### Added

- **Glue compressor section** (`src/dsp/GlueCompressor.{h,cpp}`), at the host sample rate ahead
  of Drive, in console insert order and adding **zero latency**. One mono-summed sidechain and a
  *feedback* detector tap - the detector sees the signal after the gain cell, one sample old -
  which is what produces the soft emergent knee, the ratio-dependent effective attack and the
  characteristic law, none of it curve-fitted. Two selectable laws:
  - **VCA**: dB-domain timing network driven by a linear-domain overshoot test, with a
    dual-time-constant Auto release whose slow reservoir is only ever charged by release-phase
    ripple - so sustained programme fills it and a lone transient does not. Measured: ratio
    asymptotes within 0.03 of 1/R at all three positions, exact transparency below threshold,
    63% attack at tau/(1+k) (4.96 / 2.46 / 0.98 ms at the 10 ms position for 2:1 / 4:1 / 10:1),
    fixed releases within 20% of their time constants, Auto recovering a 50 ms burst in 0.28 s
    and 10 s of sustained gain reduction in 2.37 s (8.3x) with a monotonic tail.
  - **Vari-Mu**: softplus dead-zone sidechain, current-limited rectifier and a trapezoidally-
    discretised three-capacitor release network, into a gain cell derived from the analytic
    transconductance of a Koren triode law. Measured: knee width above 6 dB at the low ratio
    position, slope 0.07 above +15 dB at the high one, 37% recovery at 0.29 / 0.78 / 1.89 /
    4.72 s across the four fixed positions, and Auto recovering 9.6x slower after sustained
    programme than after a burst. The attack is a genuine slew: peak dGR/dt grows only 1.22x
    when the step size doubles, against 2.00x for the VCA law's exponential.
  - Shared: detector-only high-pass (20 Hz = hard bypass), static makeup, a 10 ms crossfade on
    the section and a 30 ms crossfade on a law change with the incoming law's envelope
    warm-started from the outgoing gain reduction.
- **Iron stage** (`src/dsp/IronStage.h`), inside the existing 4x oversampled region after the
  saturator: a leaky flux integrator, a saturating core with a deliberate asymmetry offset, and
  the exact algebraic inverse of that integrator. Because flux is the integral of the signal,
  third-harmonic distortion rises towards low frequencies at a measured 10.3 dB per octave -
  the published bus-transformer signature, emergent rather than fitted. Plus a 35 Hz resonance
  bump and gentle high-frequency rounding.
- **HQ quality mode** (`src/dsp/AdaaShapers.h`): first-order antiderivative anti-aliasing on the
  same three Character transfer functions, same oversampling factor, same reported latency, same
  voicing. Measured 24 dB lower non-harmonic floor on every Character at 10 kHz / 0 dBFS /
  24 dB Drive.
- **Auto Gain**: wet-path Drive compensation with per-Character constants, calibrated against
  equal-RMS pink-noise renders and frozen. Holds output within 1.5 dB from Drive 0 to 18 dB.
- **Gain-reduction readout** in the editor, polled at 30 Hz. Not an APVTS parameter - it is a
  measurement, and making it one would expose it to automation, undo and preset serialisation.
- **Three factory presets**: Orchestral Bus Glue, Soft Tube Glue, Iron Bus Weight. The eleven
  existing presets are byte-identical and now SHA-256 pinned in CI.
- **State schema 3**: `getStateInformation()` stamps `stateSchema="3"`; an older state has every
  schema-3 parameter injected at its own default on load, because `APVTS::replaceState()` leaves
  parameters absent from the incoming tree at whatever the instance already held. Unknown newer
  schemas load tolerantly.

### Fixed

- **Iron/ADAA numerical conditioning**, found by the new tests rather than in the field. The ADAA
  difference quotient amplifies its antiderivative's rounding error by 1/delta; in single
  precision that reached a few percent right at the guard threshold, which is an artefact at
  every turning point of a low-frequency signal. Both the Iron core and the Character shapers now
  evaluate the quotient in double. Separately, a 35 Hz biquad at 4x the host rate has no
  significant digits left in single precision - measured, the "35 Hz" bump peaked nearer 32 Hz
  and lost a third of its gain - so the Iron stage's two filters carry double coefficients and
  double state.

### Notes

- The Iron stage's integrator/differentiator pair is **backward-Euler matched, not bilinear**.
  The bilinear leaky one-pole has a zero at z = -1, so its exact inverse has an undamped pole at
  Nyquist: a transient would park an oscillation that never decays and that rounding
  random-walks. The backward-Euler pair's inverse is a pure one-zero FIR - unconditionally
  stable, decaying to true zero, and an exact inverse of the integrator actually used.
- Voicing is anchored to published circuit analysis and triode laws, not to measured hardware.
  Every test asserts a behavioural invariant - knee softness, slew ordering, time-constant
  ratios, the HD3 slope - and never a "sounds like" claim. See `docs/manual.md`.
- Three assertions deviate from the brief's stated numbers, each documented at the site of the
  call: the VCA attack is tau/(1+k) rather than tau*k/(1+k) (which would get slower with ratio,
  contradicting the same section's own claim); the Vari-Mu slew is asserted as saturation of
  peak dGR/dt rather than as a ratio of 90%-gain-reduction times, which a closed loop confounds;
  and the absolute -80 dBFS alias floor is unreachable at the specified fixture with 4x
  oversampling, where the oversampler's own half-band stopband sets the floor rather than the
  shaper.

## [0.2.1] - 2026-07-23

### Fixed

- **RT-safety: per-block IIR coefficient allocation** (issue #22): `AureateEngine::process()`
  recomputed the Warmth low-pass, LF head-bump peak, Tone tilt shelf pair, and HF/LF Trim
  shelves' coefficients once per block via `juce::dsp::IIR::Coefficients<float>::makeLowPass`/
  `makeHighShelf`/`makeLowShelf`/`makePeakFilter`, each of which heap-allocates a fresh
  reference-counted `Coefficients` object internally - a genuine audio-thread allocation on
  every `processBlock()` call, present since v0.1.0. Replaced with
  `juce::dsp::IIR::ArrayCoefficients<float>::make*` (stack-only) writing directly into the
  already-allocated filter state (`src/dsp/RealtimeCoefficients.h`, ported from sibling
  `basilica-audio/overture`'s issue #12 fix). `hissShelf`'s fixed coefficients (computed once in
  `prepare()`) were already correct and are unchanged. New `tests/AllocationTests.cpp` guards
  this permanently (`operator new`-instrumented, see `tests/AllocationGuard.{h,cpp}`) at both the
  `AureateEngine` and `AureateAudioProcessor` level; verified red (192 allocations across a
  32-block sweep) against the pre-fix code before landing the fix.

## [0.2.0] - 2026-07-16

A research-derived deep-dive rework of the saturation core, plus the suite's M2 preset system
and a German i18n frame. See `docs/design-brief.md` for the full brief (grounded in
`docs/research-notes.md`'s sourced citations) - every change below is either carried over
unchanged from v0.1.0 where research found no contradiction, or chosen to sit inside a sourced
band/qualitative ordering, not calibrated against measured hardware. See the brief's own Honesty
section (§6) for the complete accounting.

### Added

- **Preset system (M2)**: factory/user presets, save/save-as/rename/delete, import/export
  (single files and zip banks), a startup default, and a horizontal preset bar docked at the top
  of the editor (`src/presets/`) - the suite-wide `.scaffold/specs/preset-system-m2.md` pattern,
  ported from sibling `basilica-audio/nave`'s pilot implementation. Eleven factory presets ship
  (`presets/factory/*.json`, documented in `docs/presets.md`), including one certified
  passthrough `Default`. User presets live at `~/Library/Audio/Presets/Yves Vogl/Aureate/`
  (macOS) / `%APPDATA%/Yves Vogl/Aureate/Presets/` (Windows).
- **German localisation**: all preset-bar/dialog frame strings are wrapped in `TRANS()` and
  ship a German translation (`resources/i18n/de.txt`), selected automatically from the system
  language. Parameter names/units are never translated.
- **LF head bump**: a new gentle resonant peak (80 Hz, Q 0.9, up to +1.5 dB at 100% Warmth)
  modelling tape-transport head-bump resonance - the v0.1 chain only ever shaped the top end.
- **Wow / Flutter split**: the single v0.1.0 "Wow/Flutter" parameter is now two independent
  parameters (Wow, Flutter), each still off by default, so a session can dial in slow pitch
  drift without any faster shimmer or vice versa. Flutter's default rate moved from 6.5 Hz to
  11 Hz to sit more clearly inside its sourced band, away from Wow's 0.7 Hz.

### Changed

- **Character harmonic-balance reordering**: each Character model's Warmth-driven asymmetry
  bias now scales to its own ceiling (Tape 0.12, Console 0.10, Valve 0.30 - was a single shared
  0.3), so Tape is measurably the most odd-harmonic-dominant, Valve the most
  asymmetric/even-harmonic-forward, and Console the most transparent at low-to-moderate drive.
- **Console character curve**: replaced v0.1's hard-flat cubic soft-clip with an asymmetric
  scaled-tanh soft knee, so Console is the *least* characterful of the three until pushed hard
  (previously the hardest-clipping) - a soft, blended-harmonic knee closer to real
  console/transformer summing-bus saturation.
- **Hiss spectral tilt**: the noise tap now routes through its own dedicated +4 dB-above-4kHz
  high-shelf filter before being summed into the wet signal, so it reads as HF-forward broadband
  hiss rather than inheriting only the downsampler's own darkening.
- **Documentation-only Tone honesty fix**: Tone is now described as a dual independent-corner
  shelf pair, not an unqualified "console-style tilt EQ" (canonical tilt topologies pivot around
  a single frequency) - no range/topology change.
- **Gain-staging calibration note** (docs only): Drive/Warmth's defaults are documented as tuned
  assuming a nominal -18 dBFS RMS input, anchoring the new factory presets' Drive/Output pairing.
- Version bumped to 0.2.0.

### Breaking

- The single v0.1.0 `wow_flutter` parameter no longer exists; `wow`/`flutter` replace it. A
  v0.1.0-saved state still loads without error - its old value is copied onto both new
  parameters - but this is an audible, non-crashing change, not a state-loading failure.
- Identical Warmth/Bias/Character combinations sound different after upgrading for Tape and
  Console (less asymmetric than before); Valve is unchanged (kept at the old shared ceiling).

### Testing

- 96 Catch2 tests total (up from 60): the design brief's new/changed DSP guarantees
  (`tests/DesignBriefV2Tests.cpp` - Character harmonic-balance ordering, Console
  transparency-at-low-drive, Tape near-symmetry, head-bump filter response, Hiss spectral tilt,
  Character-dependent bias ceiling regression), Wow/Flutter independence and per-parameter
  latency-independence regressions (`tests/WowFlutterTests.cpp`), a v0.1.0-state migration test
  (`tests/StateTests.cpp`), 17 preset-system tests (`tests/PresetManagerTests.cpp`), and 4
  localisation tests (`tests/LocalisationTests.cpp`).

## [0.1.1] - 2026-07-16

### Changed

- Housekeeping: canonical squircle icon cutout embedded into the plugin binary (`ICON_BIG`) and README/manual, org link sweep, heavy-music copy reframe, README pointed at GitHub Releases, and the signed tag-triggered release CI workflow added.

### Fixed

- Warmth low-pass smoother now ramps over the documented ~50ms instead of ~200ms: `warmthLowPassHzSmoothed` was `reset()` at the oversampled rate (`sampleRate * 4`) while `process()` always advances it via `skip()` with host-rate sample counts, giving it 4x the intended `stepsToTarget` (#12).
- `AureateEngine::process()` now clamps to the sample/channel counts declared to `prepare()` before processing, guarding against an out-of-bounds heap write in `juce::dsp::Oversampling`'s internal buffers if a host ever calls `processBlock()` with a block larger than it promised via `prepareToPlay()` (#13).

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Aureate signal path with unit tests (Drive, Warmth, Tone, Mix, Output).
- Wow/Flutter: tape-transport speed instability (slow "wow" + faster "flutter" modulation) via a modulated delay line ahead of Drive/oversampling, with a fixed base delay so the plugin's reported latency never changes when the amount is automated.
- Bias: an independent saturator asymmetry trim, added on top of Warmth's own bias contribution.
- Character: a Tape/Console/Valve saturation model selector, adding a cubic soft-clip ("Console") and an exponential saturation curve ("Valve") alongside the original tanh-based Tape model.
- HF Trim / LF Trim: independent fixed-frequency (8 kHz/150 Hz) shelf trims, in addition to Tone's tilt shelves.
- Hiss: a shaped tape-hiss noise floor mixed into the wet path inside the oversampled domain, off by default.
- `docs/manual.md`: a full user manual with a musical description of every parameter, signal-flow overview, and usage tips.
- Broadened Catch2 test coverage: sample-rate sweeps (44.1-192 kHz), extreme parameter automation, mono/stereo bus-layout configurations, and long-run (multi-second) NaN/Inf stability tests, plus dedicated coverage for every parameter added in this release (60 test cases total, up from 24).

### Fixed

- `DryWetMixer`'s internal delay-line capacity increased from 1024 to 8192 samples, so the dry-path delay compensation stays correct at high sample rates now that Wow/Flutter's fixed base delay is included in the plugin's reported latency.
