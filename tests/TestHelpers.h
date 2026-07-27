#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

// Small shared helpers used across the Tests target.
namespace TestHelpers
{
    // The neutrality-null program fixture (brief section 6, test 6.1): a
    // deterministic sine+noise signal, decorrelated between channels, used
    // both by the in-process A/B null (6.1b) and by the cross-version
    // tolerance null against the checked-in v0.2.1 reference render (6.1c).
    //
    // Deliberately built from an explicit LCG rather than juce::Random or
    // std::mt19937: it must produce bit-identical samples on every platform
    // and toolchain, forever, or the checked-in reference WAV stops meaning
    // anything. The sine component uses std::sin, whose last-ULP behaviour
    // *does* vary between Apple libm and the MSVC UCRT - which is exactly
    // why 6.1c is specified at a -120 dBFS tolerance rather than as a
    // bit-exact golden (see the brief's revision note 1).
    inline void fillNeutralityFixture (juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        constexpr double sineFrequencyHz = 220.0;
        constexpr float sineAmplitude = 0.40f;
        constexpr float noiseAmplitude = 0.05f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            // Per-channel seed: the two channels must not be identical, or
            // the fixture cannot exercise the compressor's shared-sidechain
            // stereo behaviour later on.
            juce::uint32 lcg = 0x9E3779B9u + static_cast<juce::uint32> (channel) * 0x85EBCA6Bu;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                lcg = lcg * 1664525u + 1013904223u;
                const auto noise = static_cast<float> (static_cast<double> (lcg >> 8) / 8388608.0 - 1.0);

                const auto phase = juce::MathConstants<double>::twoPi * sineFrequencyHz
                                    * static_cast<double> (sample) / sampleRate;

                data[sample] = sineAmplitude * static_cast<float> (std::sin (phase))
                                + noiseAmplitude * noise;
            }
        }
    }

    // Largest absolute per-sample difference between two identically-shaped
    // buffers - the measure both halves of test 6.1 are specified in.
    inline float maxAbsoluteDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        const auto numChannels = juce::jmin (a.getNumChannels(), b.getNumChannels());
        const auto numSamples = juce::jmin (a.getNumSamples(), b.getNumSamples());

        float worst = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* da = a.getReadPointer (channel);
            const auto* db = b.getReadPointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
                worst = std::max (worst, std::abs (da[sample] - db[sample]));
        }

        return worst;
    }

    // Fills every channel of the buffer with a sine wave of the given
    // frequency. `startSampleIndex` offsets the phase calculation, so
    // calling this for consecutive blocks with startSampleIndex incremented
    // by each block's length produces a phase-continuous sine across block
    // boundaries (needed whenever a test processes a "warm-up" block before
    // the measured one - see EngineTests.cpp's near-linear test). Defaults
    // to 0 (phase continuity across separate, unrelated calls is not needed
    // for the RMS-based checks that most callers use this for).
    inline void fillWithSine (juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              double frequencyHz,
                              float amplitude = 0.5f,
                              juce::int64 startSampleIndex = 0)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (startSampleIndex + sample) / sampleRate;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }

    // Root-mean-square level across all channels/samples in the buffer.
    inline double rms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // Largest absolute sample value across all channels/samples.
    inline float peakAbsolute (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }

    // Returns true if every sample in the buffer is finite (no NaN/Inf).
    inline bool allSamplesFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample]))
                    return false;
        }

        return true;
    }

    // Pearson correlation coefficient between two equally-sized single
    // channel signals - a shape-similarity measure that is insensitive to
    // constant gain and DC offset, making it a good proxy for "near-linear
    // processing" even when the stage under test also introduces some fixed
    // gain loss and/or DC bias (as an asymmetric saturator does at any drive
    // level). 1.0 means the two signals are perfectly linearly related.
    inline double correlation (const float* a, const float* b, int numSamples)
    {
        if (numSamples <= 0)
            return 0.0;

        double meanA = 0.0;
        double meanB = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            meanA += a[i];
            meanB += b[i];
        }

        meanA /= numSamples;
        meanB /= numSamples;

        double covariance = 0.0;
        double varianceA = 0.0;
        double varianceB = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto da = static_cast<double> (a[i]) - meanA;
            const auto db = static_cast<double> (b[i]) - meanB;

            covariance += da * db;
            varianceA += da * da;
            varianceB += db * db;
        }

        const auto denominator = std::sqrt (varianceA * varianceB);
        return denominator > 0.0 ? covariance / denominator : 0.0;
    }

    // Goertzel algorithm: the magnitude of a single frequency bin's DFT
    // component, without computing a full FFT. `targetFrequencyHz` should be
    // an exact multiple of sampleRate/numSamples (i.e. land on an integer
    // bin) for a leakage-free reading against a periodic test tone - used by
    // the Character harmonic-balance test (tests/CharacterHarmonicTests.cpp)
    // to measure H2/H3 energy directly from engine output. Magnitude is
    // insensitive to a constant phase/delay offset in the input, so callers
    // don't need to account for the plugin's own reported latency here.
    inline double goertzelMagnitude (const float* data, int numSamples, double sampleRate, double targetFrequencyHz)
    {
        const auto k = std::round (static_cast<double> (numSamples) * targetFrequencyHz / sampleRate);
        const auto omega = juce::MathConstants<double>::twoPi * k / static_cast<double> (numSamples);
        const auto cosine = std::cos (omega);
        const auto coeff = 2.0 * cosine;

        double q0 = 0.0, q1 = 0.0, q2 = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            q0 = coeff * q1 - q2 + static_cast<double> (data[i]);
            q2 = q1;
            q1 = q0;
        }

        const auto sine = std::sin (omega);
        const auto real = q1 - q2 * cosine;
        const auto imag = q2 * sine;

        return std::sqrt (real * real + imag * imag);
    }

    // RMS level of `buffer` after passing it through a 2nd-order Butterworth
    // band-pass filter centred at `centerHz` with the given Q - used by the
    // Hiss spectral-tilt test (tests/DesignBriefV2Tests.cpp) to compare noise
    // energy in a low band versus a high band. Operates on a copy, leaving
    // `buffer` untouched; not real-time safe (allocates), test-only.
    inline double bandpassRms (const juce::AudioBuffer<float>& buffer, double sampleRate, float centerHz, float q)
    {
        juce::AudioBuffer<float> filtered;
        filtered.makeCopyOf (buffer);

        // ProcessorDuplicator (not a bare juce::dsp::IIR::Filter), so each
        // channel gets its own independent filter state - a bare Filter
        // instance shares one set of state variables across every channel of
        // a multichannel ProcessContext, which would corrupt stereo input.
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> filter;
        filter.prepare (juce::dsp::ProcessSpec { sampleRate, static_cast<juce::uint32> (filtered.getNumSamples()),
                                                  static_cast<juce::uint32> (filtered.getNumChannels()) });
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centerHz, q);

        juce::dsp::AudioBlock<float> block (filtered);
        juce::dsp::ProcessContextReplacing<float> context (block);
        filter.process (context);

        return rms (filtered);
    }

    // Best Pearson correlation across a small range of sample-alignment
    // offsets. Real IIR filters (even ones nominally "out of the way" of a
    // test tone) have some residual group delay of their own on top of
    // whatever's reported via getLatencySamples() - typically a fraction of
    // a sample to a couple of samples, not something a plugin reports/
    // compensates as "latency". Searching a small window of offsets isolates
    // genuine shape (non)linearity from that legitimate, unreported
    // sub-block delay.
    inline double bestCorrelationOverShift (const float* a, const float* b, int numSamples, int maxShiftSamples)
    {
        double best = -1.0;

        for (int shift = -maxShiftSamples; shift <= maxShiftSamples; ++shift)
        {
            const auto length = numSamples - std::abs (shift);
            if (length <= 0)
                continue;

            const auto* aStart = a + std::max (0, shift);
            const auto* bStart = b + std::max (0, -shift);

            best = std::max (best, correlation (aStart, bStart, length));
        }

        return best;
    }
}
