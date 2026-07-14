<p align="center"><img src="docs/assets/icon.png" alt="Aureate icon" width="160"/></p>

# Aureate

*Gild the orchestra — tape and console saturation glue for strings, brass and layers.*

[![CI](https://github.com/metal-up-your-ass/aureate/actions/workflows/ci.yml/badge.svg)](https://github.com/metal-up-your-ass/aureate/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Aureate is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Aureate is a tape/console saturation "glue" plugin for orchestral material - strings, brass, and layered/bussed tracks that need cohesion and a little analog warmth without sounding like a guitar pedal. It combines a 4x oversampled, character-selectable saturator (Tape/Console/Valve) with tape-transport artefacts (Wow/Flutter, Hiss) and a console-style tilt EQ plus independent HF/LF trim shelves, so a single Warmth knob adds harmonic richness and softens the top end the way tape does, while the rest of the controls let you dial in exactly which flavour of "glue" a section needs.

## Features

- **Wow/Flutter** (0-100%): tape-transport speed instability (slow "wow" + faster "flutter"), off by default.
- **Drive** (0-24 dB): gain into the saturator. Kept modest by design - Aureate is a glue processor, not a distortion.
- **Warmth** (0-100%): jointly controls the saturator's asymmetry (single-ended, tape-like character) and a gentle pre-clip high-frequency rolloff (tape bias/self-erasure character).
- **Bias** (-100% to +100%): additional, independent saturator asymmetry trim on top of Warmth's own contribution.
- **Character** (Tape / Console / Valve): selects the saturator's transfer-function family - smooth tanh, harder-edged cubic soft-clip, or exponential valve-style saturation.
- **Tone** (-100% to +100%): a console-style tilt EQ - darker below 0%, brighter above, flat/unity at 0%.
- **HF Trim** / **LF Trim** (-6 to +6 dB): independent fixed-frequency shelf trims, in addition to Tone's tilt.
- **Hiss** (0-100%): shaped tape-hiss noise floor mixed into the wet path, off by default.
- **Mix** (0-100%): dry/wet blend, delay-compensated so 0% is a sample-accurate passthrough of the input.
- **Output** (-24 to +24 dB): final trim applied after the dry/wet mix.
- Full latency reporting and host plugin-delay-compensation support (the 4x oversampler plus Wow/Flutter's fixed base delay are the plugin's only sources of latency, both accounted for in `getLatencySamples()`).
- AU, VST3, and Standalone builds.

See [`docs/manual.md`](docs/manual.md) for the full parameter reference and usage tips.

## Signal flow

```
input -> Wow/Flutter -> Drive
      -> [4x oversampled: Warmth HF-rolloff -> saturator (Character, Warmth+Bias asymmetry)
         -> Tone tilt -> HF/LF Trim -> Hiss]
      -> downsample -> Dry/Wet Mix -> Output trim -> output
```

See [`docs/architecture.md`](docs/architecture.md) for the full diagram, latency-compensation strategy, and real-time-safety notes.

## Roadmap

Tracked as GitHub milestones and issues (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0). Read them with `gh issue list` / `gh api repos/metal-up-your-ass/aureate/milestones`. The current GUI is a functional v0.1 slider editor; a custom vector-drawn LookAndFeel is a later milestone.
<!-- ==END BODY== -->

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Aureate is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Aureate is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
