# M3 photoreal GUI - parameter mapping (tubecomp design)

Status: pilot implementation for the basilica-audio wave shared with
requiem/tenebrae/apotheosis. Owner visual sign-off pending - see the PR.

## Design source

`brand/mocks/tubecomp/` (repo-relative to the suite root, one level above
this repo): `master-03-clean-base.png` (production background, embedded as
`resources/gui/master_tubecomp.png`), `components/needle.{png,json}`,
`components/toggle-{1..4}-{up,down}.png` + `toggles.json`,
`components/vent-glow.{png,json}`, `layout-manifest.json`.

Aureate has 21 automatable parameters (15 `AudioParameterFloat`, 6
`AudioParameterChoice`) plus 2 `AudioParameterBool`. The design provides 10
knob positions and 4 toggle positions - fewer than the full parameter count,
so a subset had to be chosen. The rest remain fully functional via
automation, host generic UI, and presets; nothing is removed from the APVTS.

## Knob mapping (10 of 21 continuous/choice parameters)

| # | Position | Master px (cx, cy, r) | Parameter | Why here |
|---|----------|------------------------|-----------|----------|
| 1 | Row 1 (upper, flanking VU), far left | (306, 493, 51) | **Drive** (`drive`) | The single most-reached-for control of any saturation plugin. |
| 2 | Row 1, left | (455, 493, 50) | **Warmth** (`warmth`) | Core saturator character (asymmetry + HF rolloff). |
| 3 | Row 1, right | (923, 493, 50) | **Tone** (`tone`) | Core tilt-EQ voicing control. |
| 4 | Row 1, far right | (1072, 493, 51) | **Output** (`output`) | Final trim - always visible alongside Drive. |
| 5 | Row 2 (lower), far left | (306, 637, 51) | **Mix** (`mix`) | Dry/wet - secondary but frequently used. |
| 6 | Row 2, left | (403, 627, 46) | **Bias** (`bias`) | Fine asymmetry trim, pairs with Warmth. |
| 7 | Row 2, centre-left | (612, 635, 49) | **Character** (`character`, 3-way choice: Tape/Console/Valve) | Selects the saturator transfer-function family - a natural fit for a rotary-switch-style knob. |
| 8 | Row 2, centre-right | (727, 631, 45) | **Glue Threshold** (`comp_threshold`) | The v0.3.0 bus-compressor section's primary "how much" control. |
| 9 | Row 2, right | (924, 637, 51) | **Glue Makeup** (`comp_makeup`) | Pairs directly with Glue Threshold. |
| 10 | Row 2, far right | (1038, 643, 45) | **Iron** (`iron`) | v0.3.0's other headline character control (flux-domain transformer stage). |

Rotation convention: `MasterCropKnob`'s ±135° sweep maps parameter
proportion 0.5 → 0° (12 o'clock, the master's own baked rest pose, so a
knob at exactly its parameter's *range midpoint* renders pixel-identical to
the baked art with zero live rotation). This is **not** the same as "the
knob visually rests unrotated at the parameter's *default* value" for every
knob - several defaults are not at their range's midpoint (e.g. Mix
defaults to 100%, proportion 1.0 → the full +135° extreme; Glue Makeup and
Iron both default to their range minimum, proportion 0.0 → −135°). This is
deliberate and correct: the knob's rotation always honestly reflects the
parameter's *actual* current value, exactly like a real rotary control -
"reset to a neutral pose regardless of value" was never the goal.
`Character`, a 3-position choice, follows the same convention and therefore
rests at the full CCW extreme (−135°) at its own default ("Tape", index 0) -
also correct/expected for a 3-way rotary selector switch, not a defect.

### Not wired to a knob (automation/preset-only)

`wow`, `flutter`, `hiss`, `hf_trim`, `lf_trim`, `comp_ratio`, `comp_attack`,
`comp_release`, `comp_sc_hpf` - all 9 remain fully functional APVTS
parameters (automatable, saved/restored in state and presets), simply
without a dedicated physical control in this pass. `comp_attack`/
`comp_release` in particular are documented in `docs/manual.md` as
largely "set once" controls in this design's feedback topology.

## Toggle mapping (4 of 6 boolean-capable parameters)

| # | Zone (master px x,y,w,h) | Parameter | UP (baked default) | DOWN |
|---|---------------------------|-----------|---------------------|------|
| 1 | upper-left (164, 396, 122, 160) | **Glue** (`comp_enable`, bool) | Off | On |
| 2 | upper-right (1098, 439, 130, 96) | **Auto Gain** (`auto_gain`, bool) | Off | On |
| 3 | lower-left (89, 603, 197, 120) | **Glue Model** (`comp_model`, 2-way choice) | VCA (index 0) | Vari-Mu (index 1) |
| 4 | lower-right (1093, 590, 193, 125) | **Quality** (`quality`, 2-way choice) | Classic (index 0) | HQ (index 1) |

Convention (binding for every tubecomp-family plugin, see
`src/gui/ToggleZoneSwap.h`): the master bakes every lever **UP**, and UP
always means *this parameter's own APVTS default*. All four parameters
above default to index/state 0, so every toggle's baked UP pose matches its
parameter's default with zero mismatch/pop at construction - this is why
these four (not e.g. `comp_ratio`, a 3-way choice with no natural
up/down/binary shape) were chosen for the toggle slots.

Two-way `AudioParameterChoice` parameters (`comp_model`, `quality`) are
wired via `juce::AudioProcessorValueTreeState::ButtonAttachment`, exactly
like the two real `AudioParameterBool` parameters - valid because a
2-choice parameter's normalised range is `[0,1]` with exactly two reachable
points, so the button's boolean toggle state maps 1:1 onto the two choice
indices.

### Not wired to a toggle

`comp_ratio` (3-way) - automation/preset-only.

## VU needle

Displays `AureateAudioProcessor::getCurrentOutputLevelDb()` (already an
atomic, real-time-safe measurement published once per block, post-chain -
i.e. exactly what leaves the plugin, Output trim included - see
`PluginProcessor.h`), converted to VU at the suite's **Standard-A**
calibration (0 VU = -18 dBFS, same convention as sibling
basilica-audio/silentium) and clamped to the dial's own measured tick range
`[-20, +3]`:

```
vuDb = clamp(getCurrentOutputLevelDb() - (-18), -20, +3)
```

Aureate is a tape/console saturation unit, and the classic hardware
metaphor this VU-style dial is meant to evoke - "driving the output into
the red" - requires an actual signal-level reading. An earlier revision fed
this same needle from gain reduction instead; because gain reduction is
never positive, the dial's `+1/+2/+3` (red) zone was structurally
unreachable under that mapping. Switching the source to output level
resolves that: the needle now genuinely reaches the red zone whenever the
output is driven above -18 dBFS by 1-3 dB. The dB→angle tick table itself
(`HubNeedle.cpp`) is unchanged - it was always VU-calibrated; only the
source measurement feeding it changed.

## Vent-glow "breathing"

Driven from `AureateAudioProcessor::getCurrentGrDb()` (0 dB → idle
breathing around t≈0.85; ≥6 dB of gain reduction → the hard t=1.0 ceiling),
independently of the VU needle above. This is a deliberately coarse "is the
Glue section working" indicator - unrelated to the needle's own dB scale/
direction or its output-level source, and unaffected by whether Glue is
even enabled (idle breathing continues regardless, per the suite's "idle
flicker must never read as fully off" rule).

## Component family (reusable by requiem/tenebrae/apotheosis)

All under `src/gui/`, built design-agnostic (no aureate-specific constants
baked into the classes themselves - geometry is always a constructor
parameter):

- `HubNeedle` - pivot-centred sprite rotation + ballistic smoothing + a11y.
  The dB→angle tick table (`HubNeedle.cpp`) IS specific to the tubecomp
  master's own dial artwork and is shared verbatim by any sibling plugin
  reusing this exact master render.
- `SubtractiveGlow` - the tube-vent breathing overlay, implementing the
  design's own measured subtractive runtime model.
- `ToggleZoneSwap` - stateless zone-crop-swap draw helper.
- `MasterCropKnob` - feathered circular master-crop rotary knob (JUCE
  `Slider` subclass, wireable via the standard `SliderAttachment`).

`src/gui/Flicker.h` is copied verbatim from `basilica-audio/silentium`
(unmodified - already fully design-agnostic).

## Typography pass (suite typo phase)

Owner decision 2026-07-26 ("Weg 2", after AI-baked scale numerals failed 3x
on the victorian design): text is never baked into the AI master - it is set
locally as a sharp JUCE text layer in the suite serif (EB Garamond, embedded
via BinaryData, OFL - the same two faces sibling basilica-audio/silentium
ships). Implementation: `src/gui/PlateTypography.h` (design-agnostic engraved
draw: dark incision ink + a one-scaled-pixel lit lip offset DOWN, the inverse
of silentium's raised gold-on-dark labels), drawn LAST in
`PluginEditor::paint()` so neither the toggle-zone swap nor the vent-glow
blit can cover it.

What is lettered:

- **The three brass nameplates** (baked BLANK in the master; rects measured
  directly against `master_tubecomp.png` for this pass, see
  `aurt::layout::plaque*MasterPx`): classic hardware arrangement - maker's
  mark left (`BASILICA AUDIO`), type designation right (`BUS SATURATOR`),
  model name on the prominent centre plaque under the VU (`AUREATE`).
- **One engraved label under each of the 10 knobs**, centred on the knob's
  own interactive hit-area cx (so label and knob can never drift apart).
  Labels are terser than the accessible names where the section context
  already disambiguates: `THRESHOLD`/`MAKEUP` for Glue Threshold/Glue Makeup.

Deliberately NOT lettered: the four toggles. Every candidate spot either
collides with a toggle-zone swap rect's own lever art (a zone blit would sit
under lettering that describes a *different* state), the left/right plaques
(directly above the upper zones), or the plate's bottom bevel (below the
lower zones). Their names stay on the accessible title/tooltip surface.

Tests: `tests/gui/EditorTypographyTests.cpp` (snapshot dark-coverage proof
against the raw master's clean plaque fields, a flat-ground glyph-rendering
unit proof of the shared draw path, and a layout invariant keeping every
label out of its knob's hit-area).
