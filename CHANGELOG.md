# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
