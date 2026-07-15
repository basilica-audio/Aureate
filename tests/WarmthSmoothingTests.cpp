#include "dsp/AureateEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Regression test for issue #12: warmthLowPassHzSmoothed used to be reset()
// at the *oversampled* rate (sampleRate * 4) while process() always calls
// its skip() with a host-rate sample count (identically to every other
// smoother in AureateEngine) - so its stepsToTarget countdown took 4x as
// many skip() calls to exhaust, stretching its ramp to ~4x
// smoothingTimeSeconds (~0.2s) in real time instead of the documented/
// intended 0.05s. Verified against JUCE 8.0.14's
// juce::SmoothedValue::reset()/skip() (juce_SmoothedValue.h), which never
// reference sampleRate again after reset() - skip(N) simply decrements the
// stepsToTarget countdown by the raw N passed in.
//
// This test measures the bug's real, audible symptom rather than reaching
// into AureateEngine's private smoother: it jumps Warmth from 0% (cutoff
// pinned near 20 kHz, transparent to a high-frequency probe tone) to 100%
// (cutoff at 3 kHz, deep in the probe's stopband) and processes exactly one
// smoothingTimeSeconds' worth of host-rate samples in a single block. By
// the SmoothedValue contract (skip(N) with N == stepsToTarget lands exactly
// on the target), a correctly host-rate-clocked smoother must be fully
// converged to the 3 kHz cutoff at that point; the (buggy) oversampled-rate
// clocked version only completes 1/4 of its ramp in the same host-sample
// count, leaving the cutoff far higher (~12.4 kHz - see the class comment
// above) and the probe barely attenuated.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double smoothingTimeSeconds = 0.05; // must match AureateEngine's private constant
    constexpr double probeFrequencyHz = 12000.0; // near-unity at Warmth 0% (20 kHz cutoff), deep in the stopband at Warmth 100% (3 kHz cutoff)

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int maximumBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maximumBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Warmth low-pass smoother converges within one smoothingTimeSeconds of host samples (not 4x that)",
           "[dsp][engine][warmth][smoothing]")
{
    // floor()'d exactly the way SmoothedValue::reset() computes
    // stepsToTarget, so a single process() call of exactly this many host
    // samples exhausts a correctly-clocked ramp's countdown in one skip().
    const auto rampSamples = static_cast<int> (std::floor (smoothingTimeSeconds * testSampleRate));

    AureateEngine engine;

    // Neutral everywhere except Warmth, so the probe tone's level change is
    // attributable to the Warmth low-pass alone.
    engine.setDriveDb (0.0f);
    engine.setToneProportion (0.0f);
    engine.setBiasProportion (0.0f);
    engine.setWowFlutterProportion (0.0f);
    engine.setHissProportion (0.0f);
    engine.setHfTrimDb (0.0f);
    engine.setLfTrimDb (0.0f);
    engine.setMixProportion (1.0f);
    engine.setOutputDb (0.0f);
    engine.setCharacter (TapeSaturator::Model::tape);

    // Start fully transparent (Warmth 0%) - prepare() primes current ==
    // target immediately, so no settling wait is needed for this baseline.
    engine.setWarmthProportion (0.0f);
    engine.prepare (makeTestSpec (1, rampSamples));

    juce::AudioBuffer<float> reference (1, rampSamples);
    TestHelpers::fillWithSine (reference, testSampleRate, probeFrequencyHz, 0.5f);

    juce::AudioBuffer<float> transparentOutput;
    transparentOutput.makeCopyOf (reference);
    {
        juce::dsp::AudioBlock<float> block (transparentOutput);
        engine.process (block);
    }
    const auto transparentRms = TestHelpers::rms (transparentOutput);
    REQUIRE (transparentRms > 0.1); // sanity: near-unity passthrough at Warmth 0%

    // Jump Warmth's target to 100% (3 kHz cutoff) without re-preparing, then
    // process exactly one ramp's worth of host samples in a single block.
    engine.setWarmthProportion (1.0f);

    juce::AudioBuffer<float> convergedOutput;
    convergedOutput.makeCopyOf (reference);
    {
        juce::dsp::AudioBlock<float> block (convergedOutput);
        engine.process (block);
    }
    const auto convergedRms = TestHelpers::rms (convergedOutput);

    // At a fully-converged 3 kHz cutoff, a 12 kHz probe (2 octaves above,
    // through a 2nd-order/12 dB-per-octave Butterworth low-pass) is
    // attenuated by roughly 24 dB (~0.06x). Before the fix, only 1/4 of the
    // (4x too slow) ramp has elapsed at this point, leaving the cutoff
    // around ~12.4 kHz - essentially still at the probe frequency, so the
    // probe passes at close to full level. -6 dB (0.5x) sits well between
    // the two, cleanly separating correct from buggy behaviour.
    CHECK (convergedRms < transparentRms * 0.5);
}
