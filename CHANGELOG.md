# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
