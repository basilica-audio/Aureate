<p align="center"><img src="assets/icon.png" alt="Aureate icon" width="120"/></p>

# Aureate User Manual

## What Aureate is

Aureate is a tape/console saturation "glue" plugin for orchestral material - strings, brass, and layered/bussed tracks that need cohesion and a little analog warmth without sounding like a guitar pedal. It combines a 4x oversampled, character-selectable saturator (tanh-based tape, cubic-clip console, or exponential valve) with tape-transport artefacts (Wow/Flutter, Hiss) and a console-style tilt EQ plus independent HF/LF trim shelves.

## Where it sits in a heavy production chain

Aureate is designed to run **after** the individual layers of an orchestral/choral stack have been balanced (strings, brass, choir, etc.), typically on:

- A **string or brass bus**, to glue a divisi section together the way tape/console summing naturally does - a little Drive and Warmth, modest Mix, and the section reads as one instrument rather than a pile of close mics.
- An **orchestral/choir submix bus**, sitting after the individual sections and before the final mix bus, to add cohesion before the orchestral material meets the metal instrumentation (guitars, drums, bass).
- The **mix bus** itself (sparingly - low Drive, Mix well under 100%), as a subtle "master glue" pass, in the same role a tape machine or console summing bus would play in a hybrid analog/ITB workflow.

It is not a distortion or amp-sim plugin (that role belongs to `overture`/`tenebrae` elsewhere in the suite) - Drive is deliberately capped at a modest 24 dB and the default Warmth/Character settings stay well inside "adds richness," not "adds grit."

## Signal flow

```
input -> Wow/Flutter -> Drive
      -> [4x oversampled: Warmth HF-rolloff -> saturator (Character: Tape/Console/Valve,
         Warmth+Bias-driven asymmetry) -> Tone tilt -> HF/LF Trim -> Hiss]
      -> downsample -> Dry/Wet Mix -> Output trim -> output
```

Wow/Flutter and Drive run at the host sample rate; everything from the Warmth low-pass through Hiss runs inside the 4x oversampled domain, so the saturator's harmonics (and Hiss's noise) are generated and filtered at 4x the host rate before a single downsample step. Mix blends the processed ("wet") signal back with a delay-compensated copy of the untouched input, and Output is a final trim applied to the combined result. See [`docs/architecture.md`](architecture.md) for the full technical breakdown, including latency accounting and real-time-safety notes.

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Wow/Flutter** | 0-100 | 0 | % | Amount of tape-transport speed instability: a slow "wow" (~0.7 Hz) plus a faster "flutter" (~6.5 Hz) pitch wobble, applied via a modulated delay ahead of Drive. 0% is a fixed (non-modulated) delay - a true off state, not "very little." Use sparingly (10-25%) for a vintage-tape character on sustained pads/strings; higher settings are an obvious, deliberate effect. |
| **Drive** | 0-24 | 6 | dB | Gain into the saturator. Kept modest by design - Aureate is a glue processor, not a distortion pedal. Higher settings push the Character-selected curve harder, adding more harmonic content and compression. |
| **Warmth** | 0-100 | 35 | % | Jointly controls two things from one knob: the saturator's asymmetry bias (single-ended, tape-like character) and a gentle pre-clip high-frequency rolloff (tape self-erasure/bias-oscillator darkening). Higher Warmth = more asymmetric saturation and a darker top end - the single most important "character" knob on the plugin. |
| **Bias** | -100 to 100 | 0 | % | An additional, independent saturator asymmetry trim, added on top of Warmth's own bias contribution. Use this to skew the asymmetry further (or in the opposite direction) without touching Warmth's HF-rolloff amount - useful for dialing in odd-vs-even harmonic balance to taste. |
| **Character** | Tape / Console / Valve | Tape | - | Selects the saturator's transfer-function family. **Tape**: smooth asymmetric tanh, an "infinite" soft compression curve - the classic, forgiving tape-glue sound. **Console**: an asymmetric cubic soft-clip, harder-edged and more transparent below its clip point - closer to a solid-state summing bus. **Valve**: an asymmetric exponential saturation curve, a different even/odd harmonic blend than either - a rounder, tube-like flavour. |
| **Tone** | -100 to 100 | 0 | % | A console-style tilt EQ: negative darkens (low shelf up, high shelf down), positive brightens (the inverse), 0% is flat/unity. Use this for the broad tonal balance of the whole processed signal. |
| **HF Trim** | -6 to 6 | 0 | dB | A fixed-frequency (8 kHz) high-shelf trim, independent of Tone - a finer top-end adjustment (add air or tame harshness) after the broader Tone tilt has set the overall balance. |
| **LF Trim** | -6 to 6 | 0 | dB | A fixed-frequency (150 Hz) low-shelf trim, independent of Tone - a finer low-end adjustment (add weight or tighten up mud) after the broader Tone tilt. |
| **Hiss** | 0-100 | 0 | % | Amount of shaped noise ("tape hiss") mixed into the processed signal, generated inside the oversampled domain so it inherits a natural top-end softening from the downsampling filter. 0% is genuinely silent (no noise floor at all) - a deliberate "vintage" option for material that should sound like it came off a tape machine, not a mixing-desk artefact to leave on by default. |
| **Mix** | 0-100 | 100 | % | Dry/wet blend. At 0% the plugin is a sample-accurate (latency-compensated) passthrough of the input - useful for parallel/New-York-style blending, or for confirming Aureate isn't colouring a signal when you want to A/B it out. |
| **Output** | -24 to 24 | 0 | dB | Final output trim, applied *after* the dry/wet mix - unlike Drive (which only affects the wet path), Output scales the combined dry+wet signal as a whole. Use it to compensate for level changes introduced by Drive/Warmth/Character before the signal moves further down the chain. |

## Tips

- **Start with Character before reaching for Drive.** The three models sound quite different at the same Drive/Warmth settings - Tape is the most forgiving and glue-like, Console is more direct and "solid," Valve sits somewhere in between with a rounder top end. Pick the character first, then dial in Drive/Warmth to taste.
- **Warmth does double duty.** Because it drives both the saturator's asymmetry and the HF-rolloff together, a single Warmth move can sound like "more tape" without any other control changing - this is the fastest way to explore the plugin's core character. If you want the darkening effect without more saturation asymmetry, reach for the separate LF/HF Trim shelves instead, or offset the asymmetry back out with a negative Bias.
- **Bias is for fine-tuning, not the main event.** Leave it at 0% until Warmth (and Character) have you most of the way there, then nudge Bias if you want a touch more (or less, or reversed) asymmetric character without moving the HF-rolloff.
- **Wow/Flutter and Hiss are both off by default on purpose.** They're deliberate "worn tape" effects for material that wants to sound like it came off a physical machine (a mellotron-style string patch, a vintage-tape-emulation pass on a whole mix) - most orchestral glue use cases should leave both at 0% and only reach for them when that specific character is the goal.
- **Use Mix for parallel processing.** Because Mix is sample-accurately delay-compensated, you can blend a heavily driven, characterful wet signal back under the clean dry signal (New-York-style parallel saturation) without any phase smearing from the oversampling latency.
- **HF/LF Trim vs. Tone.** Tone's tilt shelves move the whole spectral balance in one gesture; HF/LF Trim are independent, so you can, for example, brighten with Tone and then pull a little top end back with a negative HF Trim if Tone's brightening also over-emphasizes something specific near 8 kHz.
- **Use Output to gain-match.** Since Drive/Warmth/Character all change the wet signal's level and perceived loudness, use Output to bring the processed signal back to roughly the same loudness as the dry input before comparing them (avoids the "louder always sounds better" trap when A/B-ing Mix or bypass).
