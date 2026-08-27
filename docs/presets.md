# Factory presets

Fourteen factory presets ship with Aureate, embedded via BinaryData from
`presets/factory/*.json` (see `.scaffold/specs/preset-system-m2.md` for the
suite-wide M2 architecture this repo implements, and sibling `basilica-audio/
nave`'s `docs/preset-system-notes.md` for the pilot's build-wiring recipe
this repo followed). Most are sourced starting points from
`docs/design-brief.md`'s "Factory Presets" section (§4); **Default** is the certified
passthrough state, both this plugin's out-of-the-box default and an explicit
one-click way back to "no coloration." See the design brief's own Honesty
section (§6) for what these numbers are and aren't calibrated against
(research-derived engineering choices tuned for measurable separation, not
transcribed from any specific hardware or commercial preset).

**Three of them are not described below yet** — *Iron Bus Weight*, *Orchestral Bus Glue* and
*Soft Tube Glue*, added in v0.3.0 (basilica-audio/Aureate#43).

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | All parameters at their off/default position - the plugin's out-of-the-box sound and the M2 default-resolution target. |
| **String Section Glue** | Bus | A cohesion pass for a divisi string bus - Tape character, modest Drive/Warmth, a touch of brightening Tone. |
| **Brass Bloom** | Bus | A little push for a brass bus without grit - Console character (transparent at this level), light positive Bias/Tone/HF Trim. |
| **Choir Warmth** | Bus | Gentle darkening and roundness for a choral bus - Tape character, higher Warmth, negative Tone and HF Trim, a touch of LF Trim weight. |
| **Orchestral Submix Cohesion** | Bus | The "before it meets the metal instrumentation" bus pass the manual describes - Console character at a balanced, otherwise-neutral setting. |
| **Master Glue (Subtle)** | Master | A sparing master-bus pass - low Drive/Warmth and a 45% Mix, so the effect stays additive rather than defining. |
| **Vintage Tape Pad** | FX | Deliberate worn-tape character for sustained pads/mellotron-style patches - negative Bias/Tone, plus Wow, Flutter, and Hiss all engaged together. |
| **Valve Push** | Bus | The most aggressively asymmetric/even-harmonic option, for a section that wants audible tube-style push - Valve character, high Drive/Warmth/Bias. |
| **Parallel Grit (New York)** | Bus | A heavily driven wet signal (Valve, high Drive/Warmth) blended under a clean dry signal at 50% Mix - a classic parallel-processing technique. |
| **Console Summing Sheen** | Bus | The "least characterful until pushed" Console archetype at a level where it stays mostly transparent - low Drive/Warmth, a touch of brightening Tone. |
| **Air & Weight** | Bus | A Trim-forward preset showcasing HF/LF Trim independent of Tone/Warmth - positive HF and LF Trim, everything else near-neutral. |

None of the presets rely on Wow/Flutter or Hiss being audible except
**Vintage Tape Pad**, which deliberately engages all three as its defining
character.
