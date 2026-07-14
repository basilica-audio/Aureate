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
        // Wow/Flutter: tape-transport speed instability amount. Default 0% -
        // off, so the plugin stays a clean glue processor unless this
        // character is deliberately engaged.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::wowFlutter, 1 },
            "Wow/Flutter",
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

        return layout;
    }
}
