#include "dsp/AureateEngine.h"
#include "dsp/GlueCompressor.h"
#include "dsp/IronStage.h"
#include "dsp/TapeSaturator.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Executable proofs for docs/design-brief.md v2's §5 "Catch2 test guarantees"
// list (items 1-5 and 9; items 6-8 live in tests/WowFlutterTests.cpp and
// tests/StateTests.cpp respectively, since they exercise Wow/Flutter
// independence and the state-migration policy directly).
//
// Tests 1-3 operate on TapeSaturator::processSample() directly (not the full
// AureateEngine) - deliberately, so the harmonic-balance/asymmetry-ordering
// proofs are decoupled from the oversampler/Warmth-low-pass/head-bump/tone/
// trim filters an engine-level test would otherwise also route the signal
// through. The exact per-model bias values used below
// (AureateEngine::characterMaxWarmthBias()) are numerically verified (see
// the PR description / commit history for the Python cross-check) to
// reproduce the brief's claimed orderings deterministically at the chosen
// signal amplitudes - this is the same math the engine's saturator stage
// runs, just without the surrounding filter chain's own frequency response
// as a confound.
namespace
{
    constexpr double testSampleRate = 48000.0;

    // A large, power-of-two-ish block gives fine-grained FFT bin spacing;
    // picking the fundamental at an exact integer bin (frequency = bin *
    // sampleRate / N) guarantees a leakage-free Goertzel reading even with
    // no window function, since the captured block covers an exact whole
    // number of cycles.
    constexpr int fftSize = 16384;
    constexpr int fundamentalBin = 64;
    constexpr double fundamentalHz = testSampleRate * fundamentalBin / fftSize;

    std::vector<float> sineSignal (double amplitude, double frequencyHz)
    {
        std::vector<float> data (static_cast<size_t> (fftSize));

        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<float> (amplitude * std::sin (juce::MathConstants<double>::twoPi * frequencyHz * static_cast<double> (i) / testSampleRate));

        return data;
    }

    std::vector<float> applySaturator (const std::vector<float>& input, float bias, TapeSaturator::Model model)
    {
        std::vector<float> output (input.size());

        for (size_t i = 0; i < input.size(); ++i)
            output[i] = TapeSaturator::processSample (input[i], bias, model);

        return output;
    }
}

//==============================================================================
// 1. Character harmonic-balance ordering: Tape.H2/H3 < Console.H2/H3 <
// Valve.H2/H3, i.e. Tape is the most odd-dominant, Valve the most
// even-dominant, Console in between (docs/design-brief.md §3.3).
//
// Deviation from the brief's literal "at matched Bias=Warmth=0 settings"
// wording, documented explicitly: with bias exactly 0, every model's
// shift-then-recentre construction degenerates to the plain curve f(x),
// which is an exactly odd function for all three models (tanh, the scaled-
// tanh console knee, and the exponential valve curve all satisfy
// f(-x) == -f(x) identically, not just approximately) - and a genuinely odd
// nonlinearity fed a sine wave produces *zero* even harmonics regardless of
// curve shape (a general half-wave-antisymmetry result, not specific to
// these curves). H2 would be ~0 for all three at bias=0, so no ordering
// could be measured there. This test instead drives each model with its own
// actual production bias ceiling (characterMaxWarmthBias()) - i.e. the
// literal Warmth=100%/Bias=0 operating point the brief's own table (§3.3)
// exists to realise - which is what actually produces the claimed ordering.
TEST_CASE ("Design brief v2: Character harmonic-balance ordering (H2/H3) is Tape < Console < Valve at each model's own bias ceiling",
           "[dsp][saturator][character][designbrief]")
{
    constexpr double driveAmplitude = 2.5; // moderately driven, well past the small-signal region for all three curves

    const auto input = sineSignal (driveAmplitude, fundamentalHz);

    auto measureH2OverH3 = [&] (TapeSaturator::Model model)
    {
        const auto bias = AureateEngine::characterMaxWarmthBias (model);
        const auto output = applySaturator (input, bias, model);

        const auto h2 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, 2.0 * fundamentalHz);
        const auto h3 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, 3.0 * fundamentalHz);

        REQUIRE (h3 > 0.0);
        return h2 / h3;
    };

    const auto tapeRatio = measureH2OverH3 (TapeSaturator::Model::tape);
    const auto consoleRatio = measureH2OverH3 (TapeSaturator::Model::console);
    const auto valveRatio = measureH2OverH3 (TapeSaturator::Model::valve);

    CHECK (tapeRatio < consoleRatio);
    CHECK (consoleRatio < valveRatio);
}

//==============================================================================
// 2. Console transparency-at-low-drive test: at a fixed low input level and
// Warmth=0 (bias=0, so every model is a plain odd curve - see the note
// above), Console's THD is the lowest of the three (was previously the
// highest/hardest via v0.1's hard-flat cubic soft-clip).
TEST_CASE ("Design brief v2: Console has the lowest THD of the three models at a fixed low input level, Warmth=0",
           "[dsp][saturator][character][designbrief]")
{
    constexpr double lowLevelAmplitude = 0.25; // approx -12 dBFS pre-Drive, per the brief

    const auto input = sineSignal (lowLevelAmplitude, fundamentalHz);

    auto measureThd = [&] (TapeSaturator::Model model)
    {
        const auto output = applySaturator (input, 0.0f, model);

        const auto h1 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, fundamentalHz);
        const auto h2 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, 2.0 * fundamentalHz);
        const auto h3 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, 3.0 * fundamentalHz);
        const auto h4 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, 4.0 * fundamentalHz);
        const auto h5 = TestHelpers::goertzelMagnitude (output.data(), fftSize, testSampleRate, 5.0 * fundamentalHz);

        REQUIRE (h1 > 0.0);
        return std::sqrt (h2 * h2 + h3 * h3 + h4 * h4 + h5 * h5) / h1;
    };

    const auto tapeThd = measureThd (TapeSaturator::Model::tape);
    const auto consoleThd = measureThd (TapeSaturator::Model::console);
    const auto valveThd = measureThd (TapeSaturator::Model::valve);

    CHECK (consoleThd < tapeThd);
    CHECK (consoleThd < valveThd);
}

//==============================================================================
// 3. Tape near-symmetry test: at each model's own Warmth=100% bias ceiling,
// Tape's positive/negative ceiling asymmetry ratio is smaller in magnitude
// than Valve's at the same (per-model) Warmth setting.
TEST_CASE ("Design brief v2: Tape's ceiling asymmetry ratio is smaller than Valve's, each at its own Warmth=100% bias ceiling",
           "[dsp][saturator][character][designbrief]")
{
    constexpr float extremeInput = 50.0f; // deep into each curve's asymptotic ceiling

    auto asymmetryRatio = [&] (TapeSaturator::Model model)
    {
        const auto bias = AureateEngine::characterMaxWarmthBias (model);
        const auto positiveCeiling = TapeSaturator::processSample (extremeInput, bias, model);
        const auto negativeCeiling = TapeSaturator::processSample (-extremeInput, bias, model);

        const auto largerMagnitude = std::max (std::abs (positiveCeiling), std::abs (negativeCeiling));
        REQUIRE (largerMagnitude > 0.0f);

        return std::abs (std::abs (positiveCeiling) - std::abs (negativeCeiling)) / largerMagnitude;
    };

    const auto tapeAsymmetry = asymmetryRatio (TapeSaturator::Model::tape);
    const auto valveAsymmetry = asymmetryRatio (TapeSaturator::Model::valve);

    CHECK (tapeAsymmetry < valveAsymmetry);
}

//==============================================================================
// 4. Head-bump filter response test: at Warmth=100%, the LF peak's magnitude
// response at 80 Hz exceeds its response at 20 Hz and at 500 Hz; at Warmth
// =0%, the head bump contributes ~0 dB everywhere (regression/off-state
// proof).
namespace
{
    juce::dsp::ProcessSpec makeHeadBumpSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 8192;
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Measures the engine's RMS gain (output RMS / input amplitude-implied
    // reference) for a single test tone, with everything except Warmth held
    // neutral (Drive 0 dB, Bias/Tone/Trim/Hiss/Wow/Flutter all off) so the
    // measured response is attributable to the head-bump stage (plus, at
    // Warmth=100%, a tiny near-unity contribution from the saturator's own
    // small-signal gain at a low test amplitude - see the low amplitude
    // chosen below).
    double measureRmsAtFrequency (float warmthProportion01, double frequencyHz)
    {
        AureateEngine engine;
        engine.setDriveDb (0.0f);
        engine.setWarmthProportion (warmthProportion01);
        engine.setBiasProportion (0.0f);
        engine.setToneProportion (0.0f);
        engine.setHfTrimDb (0.0f);
        engine.setLfTrimDb (0.0f);
        engine.setHissProportion (0.0f);
        engine.setWowProportion (0.0f);
        engine.setFlutterProportion (0.0f);
        engine.setMixProportion (1.0f);
        engine.setOutputDb (0.0f);
        engine.setCharacter (TapeSaturator::Model::tape);
        engine.prepare (makeHeadBumpSpec (1));

        juce::AudioBuffer<float> buffer (1, 8192);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, 0.05f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return TestHelpers::rms (buffer);
    }
}

TEST_CASE ("Design brief v2: head-bump peak at Warmth=100% boosts 80 Hz measurably above 20 Hz and 500 Hz",
           "[dsp][engine][headbump][designbrief]")
{
    const auto rms20 = measureRmsAtFrequency (1.0f, 20.0);
    const auto rms80 = measureRmsAtFrequency (1.0f, 80.0);
    const auto rms500 = measureRmsAtFrequency (1.0f, 500.0);

    // A conservative fraction of the documented +1.5 dB peak gain (~1.189x
    // linear) - comfortably distinguishes "boosted" from "flat" without
    // being tuned to the exact peak-filter shape.
    CHECK (rms80 > rms20 * 1.05);
    CHECK (rms80 > rms500 * 1.05);
}

TEST_CASE ("Design brief v2: head-bump contributes ~0 dB everywhere at Warmth=0% (off-state regression)",
           "[dsp][engine][headbump][designbrief]")
{
    const auto rms20 = measureRmsAtFrequency (0.0f, 20.0);
    const auto rms80 = measureRmsAtFrequency (0.0f, 80.0);
    const auto rms500 = measureRmsAtFrequency (0.0f, 500.0);

    CHECK (rms80 == Catch::Approx (rms20).margin (rms20 * 0.02));
    CHECK (rms80 == Catch::Approx (rms500).margin (rms500 * 0.02));
}

//==============================================================================
// 5. Hiss spectral-tilt test: Hiss-only output's high-band (6-12 kHz) energy
// is now greater than or equal to its low-band (100-500 Hz) energy -
// inverting v0.1's implicit darker-than-flat behaviour (docs/design-brief.md
// §3.5).
TEST_CASE ("Design brief v2: Hiss-only output has high-band energy >= low-band energy (HF-forward, not darkened)",
           "[dsp][engine][hiss][designbrief]")
{
    AureateEngine engine;
    engine.setDriveDb (0.0f);
    engine.setWarmthProportion (0.0f);
    engine.setBiasProportion (0.0f);
    engine.setToneProportion (0.0f);
    engine.setHfTrimDb (0.0f);
    engine.setLfTrimDb (0.0f);
    engine.setHissProportion (1.0f);
    engine.setWowProportion (0.0f);
    engine.setFlutterProportion (0.0f);
    engine.setMixProportion (1.0f);
    engine.setOutputDb (0.0f);
    engine.prepare (makeHeadBumpSpec (1));

    juce::AudioBuffer<float> buffer (1, 8192);
    buffer.clear();

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    REQUIRE (TestHelpers::allSamplesFinite (buffer));
    REQUIRE (TestHelpers::rms (buffer) > 0.0);

    // Low band centered ~223 Hz (geometric mean of 100/500 Hz), Q chosen so
    // the -3dB bandwidth roughly spans 100-500 Hz; high band centered
    // ~8485 Hz (geometric mean of 6/12 kHz), Q chosen so its bandwidth
    // roughly spans 6-12 kHz.
    const auto lowBandRms = TestHelpers::bandpassRms (buffer, testSampleRate, 223.0f, 0.56f);
    const auto highBandRms = TestHelpers::bandpassRms (buffer, testSampleRate, 8485.0f, 1.41f);

    CHECK (highBandRms >= lowBandRms);
}

TEST_CASE ("Design brief v2: Hiss stays off at 0% and keeps its documented peak ceiling at 100% (regression, unchanged from v0.1)",
           "[dsp][engine][hiss][designbrief]")
{
    AureateEngine offEngine;
    offEngine.setHissProportion (0.0f);
    offEngine.setMixProportion (1.0f);
    offEngine.setOutputDb (0.0f);
    offEngine.prepare (makeHeadBumpSpec (1));

    juce::AudioBuffer<float> offBuffer (1, 8192);
    offBuffer.clear();
    juce::dsp::AudioBlock<float> offBlock (offBuffer);
    offEngine.process (offBlock);

    CHECK (TestHelpers::rms (offBuffer) == Catch::Approx (0.0).margin (1e-9));

    AureateEngine onEngine;
    onEngine.setHissProportion (1.0f);
    onEngine.setMixProportion (1.0f);
    onEngine.setOutputDb (0.0f);
    onEngine.prepare (makeHeadBumpSpec (1));

    juce::AudioBuffer<float> onBuffer (1, 8192);
    onBuffer.clear();
    juce::dsp::AudioBlock<float> onBlock (onBuffer);
    onEngine.process (onBlock);

    CHECK (TestHelpers::allSamplesFinite (onBuffer));
    // The +4 dB HF-shelf shaping is applied to the noise tap specifically
    // (see AureateEngine::process()) - it tilts the spectrum, not the
    // overall level - so the documented ~-44 dBFS peak ceiling still holds
    // to a generous margin (allowing for the shelf's own gain contribution
    // at the very top of the band).
    CHECK (TestHelpers::peakAbsolute (onBuffer) < 0.02f); // well inside -34 dBFS, comfortably below clipping
}

//==============================================================================
// 9. Character-dependent bias ceiling regression: setWarmthProportion(1.0)
// under each Character model produces the new per-model max bias (0.12
// Tape / 0.10 Console / 0.30 Valve), not the old shared 0.3.
TEST_CASE ("Design brief v2: characterMaxWarmthBias() matches the brief's section 3.3 table exactly",
           "[dsp][engine][character][designbrief]")
{
    CHECK (AureateEngine::characterMaxWarmthBias (TapeSaturator::Model::tape) == Catch::Approx (0.12f));
    CHECK (AureateEngine::characterMaxWarmthBias (TapeSaturator::Model::console) == Catch::Approx (0.10f));
    CHECK (AureateEngine::characterMaxWarmthBias (TapeSaturator::Model::valve) == Catch::Approx (0.30f));
}

TEST_CASE ("Design brief v2: at Warmth=100%, each Character model's ceiling asymmetry matches its own characterMaxWarmthBias(), not the old shared 0.3",
           "[dsp][saturator][character][designbrief]")
{
    // Directly exercises the production mapping (mapWarmthToBias() inside
    // AureateEngine.cpp) by comparing TapeSaturator::processSample() output
    // at each model's *actual* ceiling against what the *old* v0.1.0 shared
    // ceiling (0.3) would have produced - proving the new per-model mapping
    // is live, not just declared.
    constexpr float extremeInput = 50.0f;
    constexpr float oldSharedMaxBias = 0.3f;

    for (auto model : { TapeSaturator::Model::tape, TapeSaturator::Model::console, TapeSaturator::Model::valve })
    {
        const auto newBias = AureateEngine::characterMaxWarmthBias (model);

        if (model == TapeSaturator::Model::valve)
        {
            // Valve is the one model that keeps the old shared ceiling
            // value (docs/design-brief.md §3.3's table) - same output.
            CHECK (newBias == Catch::Approx (oldSharedMaxBias));
            continue;
        }

        const auto outputAtNewBias = TapeSaturator::processSample (extremeInput, newBias, model);
        const auto outputAtOldSharedBias = TapeSaturator::processSample (extremeInput, oldSharedMaxBias, model);

        CAPTURE (newBias, oldSharedMaxBias);
        CHECK (outputAtNewBias != Catch::Approx (outputAtOldSharedBias).margin (1.0e-4f));
    }
}

//==============================================================================
// v0.3.0 voicing contract additions
//==============================================================================
TEST_CASE ("Design brief v0.3.0: the Iron stage's HD3-vs-frequency law is part of the voicing contract",
           "[dsp][designbrief][iron]")
{
    // Restated here, in the file that holds this plugin's voicing contract,
    // because it is a *claim about how the plugin sounds* and not only an
    // implementation detail: at constant level, third-harmonic distortion
    // must rise towards low frequencies at roughly 12 dB per octave. That is
    // the published measured signature of a bus transformer, and it comes out
    // of the flux-domain architecture for free (see IronStage.h) rather than
    // being fitted. tests/IronStageTests.cpp measures it in detail; this row
    // pins the contract.
    constexpr double oversampledRate = 192000.0;
    constexpr int analysisSize = 1 << 16;

    auto measureThirdHarmonicDb = [&] (double frequencyHz)
    {
        IronStage stage;
        stage.setAmount (1.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = oversampledRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (analysisSize * 2);
        spec.numChannels = 1;
        stage.prepare (spec);

        juce::AudioBuffer<float> buffer (1, analysisSize * 2);
        auto* data = buffer.getWritePointer (0);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                * static_cast<double> (sample) / oversampledRate;
            data[sample] = 0.5f * static_cast<float> (std::sin (phase));
        }

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            data[sample] = stage.processCoreSample (data[sample], 0);

        const auto* measured = data + analysisSize;

        const auto fundamental = TestHelpers::goertzelMagnitude (measured, analysisSize, oversampledRate, frequencyHz);
        const auto third = TestHelpers::goertzelMagnitude (measured, analysisSize, oversampledRate, 3.0 * frequencyHz);

        return 20.0 * std::log10 (std::max (1.0e-30, third / std::max (1.0e-30, fundamental)));
    };

    const auto binWidth = oversampledRate / static_cast<double> (analysisSize);
    const auto low = std::round (40.0 / binWidth) * binWidth;
    const auto high = std::round (160.0 / binWidth) * binWidth;

    const auto atLow = measureThirdHarmonicDb (low);
    const auto atHigh = measureThirdHarmonicDb (high);

    const auto slopePerOctave = (atLow - atHigh) / 2.0;

    INFO ("HD3 " << atLow << " dB at " << low << " Hz, " << atHigh << " dB at " << high
          << " Hz - slope " << slopePerOctave << " dB/octave");

    CHECK (atLow > atHigh);
    CHECK (slopePerOctave >= 9.0);
    CHECK (slopePerOctave <= 15.0);
}

TEST_CASE ("Design brief v0.3.0: the Glue section's threshold calibration is the documented -18 dBFS RMS point",
           "[dsp][designbrief][glue]")
{
    // Documented in docs/manual.md and relied on by every factory preset that
    // sets comp_threshold, so it is a contract rather than a constant.
    CHECK (GlueCompressor::thresholdReferenceDbfsRms == -18.0f);
}
