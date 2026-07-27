#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace aur
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Drive: gain into the oversampled tape-style saturator. Kept modest
        // (0-24 dB) - Aureate is a "glue" saturator for orchestral material,
        // not a high-gain distortion.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::drive, 1 },
            "Drive",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            6.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Warmth: joint asymmetry-bias + pre-clip HF-rolloff amount (see
        // AureateEngine / TapeSaturator). 0% is a symmetric, HF-transparent
        // tanh saturator; 100% is maximally biased and darkened.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::warmth, 1 },
            "Warmth",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            35.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Tone: console-style tilt EQ. -100% fully dark, 0% flat, +100% fully
        // bright.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::tone, 1 },
            "Tone",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Mix: dry/wet. Default 100% (fully wet) - glue processing is
        // normally run fully in the signal path, not blended, though the
        // control is there for parallel/New-York-style use.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::mix, 1 },
            "Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Output: final trim, applied after the dry/wet mix.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::output, 1 },
            "Output",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Bias: additional saturator asymmetry trim, on top of (added to)
        // Warmth's own bias contribution. Default 0% - neutral, so Warmth
        // alone still fully determines the asymmetry unless a session
        // deliberately reaches for this control.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::bias, 1 },
            "Bias",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Wow / Flutter: independent tape-transport speed-instability
        // amounts (v0.2.0 - split from v0.1.0's single joint "Wow/Flutter"
        // proportion, see docs/design-brief.md §3.6). Both default to 0% -
        // off, so the plugin stays a clean glue processor unless this
        // character is deliberately engaged.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::wow, 1 },
            "Wow",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::flutter, 1 },
            "Flutter",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Hiss: shaped noise floor mixed into the wet path. Default 0% - off.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::hiss, 1 },
            "Hiss",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Character: selects the saturator's transfer-function family. Tape
        // (index 0) is the default/original model, matching the v0.1 core
        // DSP's behaviour exactly, so existing sessions/tests that never
        // touch this parameter keep the original sound.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::character, 1 },
            "Character",
            juce::StringArray { "Tape", "Console", "Valve" },
            0));

        //======================================================================
        // HF Trim / LF Trim: fixed-frequency shelf trims, independent of and
        // in addition to Tone's tilt shelves. Default 0 dB - flat.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::hfTrim, 1 },
            "HF Trim",
            juce::NormalisableRange<float> (-6.0f, 6.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::lfTrim, 1 },
            "LF Trim",
            juce::NormalisableRange<float> (-6.0f, 6.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // v0.3.0 "Glue" additions. APPENDED after the twelve frozen v0.1/v0.2
        // parameters, never interleaved, and every one of them neutral at its
        // default: the whole point of this release is that an existing session
        // that has never touched any of them keeps playing back through the
        // exact same code path it did in v0.2.1 (verified bit-exact in-process
        // by tests/EngineTests.cpp, and at the -120 dBFS class against the
        // checked-in v0.2.1 reference render by tests/StateTests.cpp).

        //======================================================================
        // Glue: master enable for the bus-compressor section. Off by default -
        // this is the neutrality gate, and it is a *branch*, not a 0%-depth
        // mix, so "off" costs nothing and changes nothing.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::compEnable, 1 },
            "Glue",
            false));

        //======================================================================
        // Glue Model: VCA (dB-domain feedback timing network) or Vari-Mu
        // (softplus dead-zone sidechain into a three-capacitor release
        // network). Both laws are always prepared; switching crossfades the
        // applied gain over 30 ms with the incoming law's envelope warm-started
        // from the outgoing gain reduction, so a live switch never jumps.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::compModel, 1 },
            "Glue Model",
            juce::StringArray { "VCA", "Vari-Mu" },
            0));

        //======================================================================
        // Glue Threshold: dB relative to the -18 dBFS RMS calibration point,
        // i.e. 0 dB means a -18 dBFS RMS sine sits exactly at threshold. In a
        // feedback topology the *effective* threshold is where the loop settles
        // rather than a hard corner - which is exactly why the knee is soft and
        // emergent rather than dialled in (see docs/manual.md).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::compThreshold, 1 },
            "Glue Threshold",
            juce::NormalisableRange<float> (-30.0f, 10.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Glue Ratio: the three classic bus-compressor positions. In the
        // feedback loop the *applied* law is k = R - 1, which is what makes the
        // knee widen at low ratios and the effective attack speed up at high
        // ones without either behaviour being programmed in.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::compRatio, 1 },
            "Glue Ratio",
            juce::StringArray { "2:1", "4:1", "10:1" },
            0));

        //======================================================================
        // Glue Attack: the six stepped positions of the classic bus
        // compressor. Ignored by the Vari-Mu law, whose attack is intrinsic to
        // its current-limited rectifier (~0.3-0.6 ms effective) and therefore
        // not a user control - documented in docs/manual.md rather than hidden.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::compAttack, 1 },
            "Glue Attack",
            juce::StringArray { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" },
            4)); // 10 ms

        //======================================================================
        // Glue Release: four fixed positions plus "Auto" - the dual-time-
        // constant ladder (VCA law) / triple program-dependent capacitor
        // network (Vari-Mu law) that makes brief peaks recover quickly while
        // sustained gain reduction lets go slowly. Auto is the default because
        // it is the position that makes the section behave like glue rather
        // than like a level control.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::compRelease, 1 },
            "Glue Release",
            juce::StringArray { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" },
            4)); // Auto

        //======================================================================
        // Glue Makeup: static output gain for the section. Deliberately not
        // auto-derived from threshold/ratio - the section's own gain staging
        // stays the engineer's decision (Auto Gain, below, compensates Drive,
        // which is a different question).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::compMakeup, 1 },
            "Glue Makeup",
            juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Glue SC Filter: detector-path-only high-pass, log-skewed because it
        // is a frequency. 20 Hz is a hard bypass (the filter is skipped
        // outright, not run at a harmless corner), so the default costs nothing.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::compScHpf, 1 },
            "Glue SC Filter",
            juce::NormalisableRange<float> (20.0f, 500.0f, 0.1f, 0.35f),
            20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Iron: flux-domain output-transformer stage amount. 0% is a hard
        // branch-skip (bit-identical bypass). The drive mapping is skewed so
        // the usable "a touch of iron" region occupies most of the travel
        // rather than the first few percent.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::iron, 1 },
            "Iron",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Quality: Classic is v0.2.1's exact saturator sample path; HQ swaps in
        // first-order antiderivative anti-aliasing on the same three transfer
        // functions. Same oversampling factor, same reported latency, same
        // voicing - only the alias floor moves.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::quality, 1 },
            "Quality",
            juce::StringArray { "Classic", "HQ" },
            0));

        //======================================================================
        // Auto Gain: wet-path Drive compensation, off by default. A listening/
        // A-B aid, not loudness matching - see docs/manual.md's honesty note.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::autoGain, 1 },
            "Auto Gain",
            false));

        return layout;
    }
}
