#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

// The v0.3.0 "Iron" stage: a flux-domain output-transformer model, running
// per-sample inside AureateEngine's existing 4x oversampled region,
// immediately after the Character saturator (electronics first, then the
// output transformer they drive).
//
// The physics, and why the architecture falls out of it: by Faraday's law the
// flux in a transformer core is the *integral* of the applied voltage, so a
// constant-amplitude sine at frequency f produces a flux proportional to 1/f.
// A core that saturates therefore saturates far harder at low frequencies for
// the same terminal voltage. Modelling that is three cheap operations:
//
//     integrate -> saturate the flux -> differentiate back
//
// and the emergent, measurable signature is the one published for real bus
// transformers: at constant level, 3rd-harmonic distortion rises by about
// 12 dB per octave towards the bottom end (test 6.10 asserts exactly this,
// with no coefficient anywhere in this file fitted to make it true - it is a
// consequence of u proportional to 1/f passing through an odd nonlinearity).
//
// DISCRETISATION (binding design decision, not an implementation detail):
// the integrator/differentiator pair is BACKWARD-EULER matched, not
// trapezoidal/bilinear.
//
// The bilinear leaky one-pole has a zero at z = -1, so its *exact* inverse
// has an undamped pole at z = -1 - Nyquist of the 4x rate, marginally stable.
// With a tanh and a deliberate DC offset sitting between the two halves of
// the pair, any transient parks a never-decaying Nyquist oscillation in the
// differentiator state (for constant input the recursion degenerates to
// y[n] = c - y[n-1]), floating-point rounding random-walks it, and the state
// never reaches zero - which would directly contradict the denormal/idle
// guarantee (test 6.18) and risk slow growth over a long session.
//
// The backward-Euler leaky integrator
//     u[n] = (u[n-1] + T x[n]) / (1 + wL T)
// instead has an exact inverse that is a pure one-ZERO FIR
//     x[n] = ((1 + wL T) s[n] - s[n-1]) / T
// - unconditionally stable, no state to oscillate, and it decays to true
// zero. It also inverts the integrator actually used, so the drive -> 0 null
// is exact rather than approximate, and at fL = 10 Hz against a 4x sample
// rate the pole-placement difference from bilinear is far inside test 6.11's
// tolerances. (If a future voicing pass ever wants the bilinear integrator
// back, its inverse pole must be damped to z = -alpha with alpha ~ 0.995 -
// never the exact z = -1 inverse.)
//
// Neutrality: at iron == 0 AureateEngine skips this stage entirely, so it is
// a bit-identical bypass rather than a unity-gain pass-through.
class IronStage
{
public:
    // Flux integrator corner. Below it the stage stops behaving like an
    // integrator (a real transformer's primary inductance shunts DC), which
    // is what keeps subsonic content from driving the core to the rail.
    static constexpr double fluxCornerHz = 10.0;

    // Reference frequency at which the flux estimate is normalised to unity.
    // Purely a scaling choice - it sets what "100% Iron" means in absolute
    // terms without touching the 1/f law itself - but it has to be an
    // explicit constant, because the raw integrator output at audio levels is
    // around 1e-3 and would otherwise place the entire distortion signature
    // below the single-precision noise floor.
    static constexpr double fluxReferenceHz = 50.0;

    static constexpr float maximumDrive = 2.5f;
    static constexpr float maximumBumpDb = 1.5f;
    static constexpr float bumpFrequencyHz = 35.0f;
    static constexpr float bumpMinimumQ = 0.70f;
    static constexpr float bumpMaximumQ = 1.10f;
    static constexpr float highCutMinimumHz = 18000.0f;
    static constexpr float highCutMaximumHz = 20000.0f;
    static constexpr float highCutQ = 0.80f;
    static constexpr float driveSkewExponent = 1.0f / 0.4f;

    //==========================================================================
    // `spec` must describe the OVERSAMPLED domain (rate and block size), since
    // that is where this stage runs.
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        oversampledRate = spec.sampleRate;

        const auto T = 1.0 / juce::jmax (1.0, oversampledRate);
        const auto omega = juce::MathConstants<double>::twoPi * fluxCornerHz;

        integratorPole = 1.0 / (1.0 + omega * T);
        integratorGain = T * integratorPole;

        // The differentiator is the algebraic inverse of the two lines above,
        // written out so the relationship is impossible to break silently:
        //   x[n] = ((1 + wL T) s[n] - s[n-1]) / T
        differentiatorCurrent = (1.0 + omega * T) / T;
        differentiatorPrevious = 1.0 / T;

        // Normalise the flux estimate to unity at fluxReferenceHz.
        const auto referenceOmega = juce::MathConstants<double>::twoPi * fluxReferenceHz;
        fluxNormalisation = std::sqrt (referenceOmega * referenceOmega + omega * omega);

        bumpFilter.prepare (spec);
        highCutFilter.prepare (spec);

        setAmount (amount);

        // Prime both filters with the allocating factory once, here, so that
        // applyCoefficients() below (and every per-block call from the audio
        // thread afterwards) has already-allocated 2nd-order storage to write
        // into - the same allocate-in-prepare pattern as the engine's own IIR
        // stages, see src/dsp/RealtimeCoefficients.h.
        *bumpFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            oversampledRate, bumpFrequencyHz, bumpMinimumQ, 1.0f);
        *highCutFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
            oversampledRate, highCutMaximumHz, highCutQ);

        applyCoefficients();

        reset();
    }

    void reset()
    {
        integratorState.fill (0.0);
        differentiatorState.fill (0.0);
        adaaPreviousInput.fill (0.0);

        bumpFilter.reset();
        highCutFilter.reset();
    }

    //==========================================================================
    // `newAmount` is the iron parameter as a 0-1 proportion.
    void setAmount (float newAmount01)
    {
        amount = juce::jlimit (0.0f, 1.0f, newAmount01);

        // Drive is skewed by 0.4 (JUCE's convention: value = range *
        // proportion^(1/skew), so proportion^2.5 here), which puts the "a
        // hint of iron" region across most of the control's travel instead of
        // in its first few percent.
        drive = maximumDrive * std::pow (amount, driveSkewExponent);

        // A small DC offset inside the core's transfer function: real cores
        // are never perfectly symmetric, and the resulting 2nd harmonic is
        // most of what "iron" sounds like as distinct from plain soft
        // clipping. Scaled by drive so it vanishes with the effect.
        offset = 0.08f * drive;

        bumpDb = maximumBumpDb * amount;
        bumpQ = bumpMinimumQ + (bumpMaximumQ - bumpMinimumQ) * amount;
        highCutHz = highCutMaximumHz + (highCutMinimumHz - highCutMaximumHz) * amount;
    }

    float getDrive() const noexcept { return drive; }

    // Recomputes the two filters' coefficients from the current amount.
    // Allocation-free (ArrayCoefficients into already-allocated storage), so
    // it is safe to call once per block from the audio thread.
    void applyCoefficients()
    {
        if (bumpFilter.state == nullptr || highCutFilter.state == nullptr)
            return;

        const auto nyquist = static_cast<float> (oversampledRate) * 0.5f;
        const auto clampedBump = juce::jlimit (10.0f, nyquist * 0.9f, bumpFrequencyHz);
        const auto clampedHighCut = juce::jlimit (10.0f, nyquist * 0.9f, highCutHz);

        applyBiquad (*bumpFilter.state,
                     juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
                         oversampledRate, clampedBump, bumpQ, juce::Decibels::decibelsToGain (bumpDb)));

        applyBiquad (*highCutFilter.state,
                     juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
                         oversampledRate, clampedHighCut, highCutQ));
    }

    //==========================================================================
    // Processes one sample of one channel through the flux core only. The two
    // filters are applied block-wise afterwards via processFilters().
    float processCoreSample (float x, size_t channel) noexcept
    {
        auto& u = integratorState[channel];

        // Backward-Euler leaky integrator (see the class comment).
        u = u * integratorPole + integratorGain * static_cast<double> (x);

        // Guard the reciprocal below: the engine branch-skips this stage at
        // iron == 0, so a zero drive only ever appears transiently, but a
        // division by it would poison the integrator state permanently.
        const auto safeDrive = juce::jmax (1.0e-6f, drive);

        const auto v = static_cast<float> (u * fluxNormalisation) * safeDrive;

        // First-order ADAA on the core nonlinearity. Belt and braces at 4x -
        // it costs one extra logCosh per sample and buys back most of what
        // the deliberate DC offset would otherwise throw above Nyquist.
        auto& previous = adaaPreviousInput[channel];
        const auto previousV = static_cast<float> (previous);
        previous = static_cast<double> (v);

        const auto delta = v - previousV;

        const auto shaped = std::abs (delta) < 1.0e-5f
                                ? core (0.5f * (v + previousV))
                                : (coreAntiderivative (v) - coreAntiderivative (previousV)) / delta;

        // Undo both the drive and the flux normalisation, so the pair is an
        // exact inverse in the linear region and the whole stage nulls
        // towards its two filters alone as drive -> 0.
        const auto s = static_cast<double> (shaped) / (static_cast<double> (safeDrive) * fluxNormalisation);

        auto& previousS = differentiatorState[channel];
        const auto y = differentiatorCurrent * s - differentiatorPrevious * previousS;
        previousS = s;

        return static_cast<float> (y);
    }

    // LF resonance bump then gentle HF rounding, in that order (the bump is
    // core/laminate resonance, the roll-off is leakage inductance - and the
    // roll-off should shape the harmonics the core just generated, not be
    // shaped by them).
    void processFilters (juce::dsp::AudioBlock<float>& block)
    {
        juce::dsp::ProcessContextReplacing<float> context (block);
        bumpFilter.process (context);
        highCutFilter.process (context);
    }

private:
    // f(v) = tanh(v + b) - tanh(b): the same shift-then-recentre shape the
    // Character saturator uses, and for the same reason - it guarantees
    // f(0) == 0, so silence in is silence out. Without the recentring the
    // differentiator downstream would turn the core's standing offset into a
    // large constant output (the differentiator's DC gain applied to
    // tanh(b)), which is a far bigger effect than the 2nd harmonic the offset
    // exists to create.
    float core (float v) const noexcept
    {
        return std::tanh (v + offset) - std::tanh (offset);
    }

    float coreAntiderivative (float v) const noexcept
    {
        return logCosh (v + offset) - std::tanh (offset) * v;
    }

    static float logCosh (float v) noexcept
    {
        const auto absolute = std::abs (v);
        return absolute + std::log1p (std::exp (-2.0f * absolute)) - 0.6931471806f;
    }

    static void applyBiquad (juce::dsp::IIR::Coefficients<float>& target,
                             const std::array<float, 6>& raw) noexcept
    {
        auto* destination = target.getRawCoefficients();
        const auto a0 = raw[3];

        destination[0] = raw[0] / a0;
        destination[1] = raw[1] / a0;
        destination[2] = raw[2] / a0;
        destination[3] = raw[4] / a0;
        destination[4] = raw[5] / a0;
    }

    static constexpr size_t maxChannels = 8;

    double oversampledRate = 176400.0;

    double integratorPole = 0.0;
    double integratorGain = 0.0;
    double differentiatorCurrent = 0.0;
    double differentiatorPrevious = 0.0;
    double fluxNormalisation = 1.0;

    float amount = 0.0f;
    float drive = 0.0f;
    float offset = 0.0f;
    float bumpDb = 0.0f;
    float bumpQ = bumpMinimumQ;
    float highCutHz = highCutMaximumHz;

    // double state throughout the flux path: the integrator accumulates over
    // very long time scales at LF, and single precision measurably drifts
    // there (Neve research section 3.4).
    std::array<double, maxChannels> integratorState {};
    std::array<double, maxChannels> differentiatorState {};
    std::array<double, maxChannels> adaaPreviousInput {};

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> bumpFilter;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> highCutFilter;
};
