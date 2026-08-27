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

        setAmount (amount);
        applyCoefficients();

        reset();
    }

    void reset()
    {
        integratorState.fill (0.0);
        differentiatorState.fill (0.0);
        adaaPreviousInput.fill (0.0);

        for (auto& state : bumpStates)
            state = {};

        for (auto& state : highCutStates)
            state = {};
    }

    //==========================================================================
    // `newAmount` is the iron parameter as a 0-1 proportion.
    void setAmount (float newAmount01)
    {
        const auto previousDrive = drive;

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
        offset = 0.08 * static_cast<double> (drive);

        bumpDb = maximumBumpDb * amount;
        bumpQ = bumpMinimumQ + (bumpMaximumQ - bumpMinimumQ) * amount;
        highCutHz = highCutMaximumHz + (highCutMinimumHz - highCutMaximumHz) * amount;

        rescaleAdaaHistoryForDriveChange (previousDrive);
    }

    // The ADAA history is stored in the DRIVE-SCALED flux domain
    // (v = u * fluxNormalisation * drive, see processCoreSample()), while the
    // shaped result is divided by the CURRENT drive on the way out. That pair
    // is an exact inverse only as long as both sides refer to the same drive -
    // and `drive` is re-derived from the Iron amount once per block, so any
    // Iron automation, preset recall or session restore that moves the control
    // leaves the stored `previousV` expressed in the OLD drive's units while
    // `v` is computed in the new one. The ADAA quotient then divides an
    // antiderivative difference by a `delta` dominated by the drive change
    // rather than by the signal, and the 1/drive factor downstream amplifies
    // the result - worst exactly where a ramp from zero spends its first
    // blocks, because drive = maximumDrive * amount^2.5 makes the RATIO
    // between consecutive small amounts enormous.
    //
    // Measured before this existed: recalling the Iron Bus Weight factory
    // preset mid-playback (Iron 0 -> 65 %) produced a +11.59 dBFS peak from
    // material that renders at -6.01 dBFS once the ramp has settled - a
    // 17.6 dB blast on a preset click. Re-expressing the history in the new
    // drive's units is the whole fix: the stored value is the same flux, just
    // in the units the next sample will be measured in, so `delta` is once
    // again the signal's own change and the inverse pair cancels exactly.
    //
    // At previousDrive == 0 there is nothing to rescale (the stage was
    // branch-skipped, so the history is zero) and nothing is scaled by zero.
    void rescaleAdaaHistoryForDriveChange (float previousDrive) noexcept
    {
        if (previousDrive <= 0.0f || drive <= 0.0f || drive == previousDrive)
            return;

        const auto ratio = static_cast<double> (drive) / static_cast<double> (previousDrive);

        for (auto& previous : adaaPreviousInput)
            previous *= ratio;
    }

    float getDrive() const noexcept { return drive; }

    // Recomputes the two filters' coefficients from the current amount.
    // Writes into plain value types, so it is allocation-free and safe to
    // call once per block from the audio thread.
    void applyCoefficients()
    {
        const auto nyquist = oversampledRate * 0.5;
        const auto clampedBump = juce::jlimit (10.0, nyquist * 0.9, static_cast<double> (bumpFrequencyHz));
        const auto clampedHighCut = juce::jlimit (10.0, nyquist * 0.9, static_cast<double> (highCutHz));

        bumpCoefficients = makePeaking (clampedBump, bumpQ, std::pow (10.0, bumpDb / 40.0));
        highCutCoefficients = makeLowPass (clampedHighCut, highCutQ);
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

        const auto v = u * fluxNormalisation * static_cast<double> (safeDrive);

        // First-order ADAA on the core nonlinearity. Belt and braces at 4x -
        // it costs one extra logCosh per sample and buys back most of what
        // the deliberate DC offset would otherwise throw above Nyquist.
        //
        // Evaluated in double like the rest of the flux path. The shaped
        // value is a difference of two numbers straddling tanh(offset), and
        // the differentiator downstream then multiplies the difference of two
        // CONSECUTIVE shaped values by the sample rate - a cancellation of a
        // cancellation. In single precision that lifted the small-signal
        // inverse-pair error above 10%; in double it is under 0.01%.
        auto& previous = adaaPreviousInput[channel];
        const auto previousV = previous;
        previous = v;

        const auto delta = v - previousV;

        // The fallback threshold is 1e-5, not something far smaller "to use
        // the exact form more often". The quotient divides the difference of
        // two antiderivative values by delta, so it amplifies their absolute
        // rounding error by 1/delta: at 1e-9 that is a factor of a billion,
        // which measurably lifted this stage's small-signal error to 4.7e-4
        // relative and parked Nyquist-rate spikes at every flux extremum -
        // exactly where delta passes through zero - even in double precision.
        // Below the threshold the midpoint evaluation is both the correct
        // limit and enormously better conditioned.
        const auto shaped = std::abs (delta) < 1.0e-5
                                ? core (0.5 * (v + previousV))
                                : (coreAntiderivative (v) - coreAntiderivative (previousV)) / delta;

        // Undo both the drive and the flux normalisation, so the pair is an
        // exact inverse in the linear region and the whole stage nulls
        // towards its two filters alone as drive -> 0.
        const auto s = shaped / (static_cast<double> (safeDrive) * fluxNormalisation);

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
        const auto channels = juce::jmin (block.getNumChannels(), maxChannels);

        for (size_t channel = 0; channel < channels; ++channel)
        {
            auto* data = block.getChannelPointer (channel);
            auto& bump = bumpStates[channel];
            auto& highCut = highCutStates[channel];

            for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
            {
                auto value = static_cast<double> (data[sample]);
                value = bump.process (bumpCoefficients, value);
                value = highCut.process (highCutCoefficients, value);
                data[sample] = static_cast<float> (value);
            }
        }
    }

private:
    // f(v) = tanh(v + b) - tanh(b): the same shift-then-recentre shape the
    // Character saturator uses, and for the same reason - it guarantees
    // f(0) == 0, so silence in is silence out. Without the recentring the
    // differentiator downstream would turn the core's standing offset into a
    // large constant output (the differentiator's DC gain applied to
    // tanh(b)), which is a far bigger effect than the 2nd harmonic the offset
    // exists to create.
    double core (double v) const noexcept
    {
        return std::tanh (v + offset) - std::tanh (offset);
    }

    double coreAntiderivative (double v) const noexcept
    {
        return logCosh (v + offset) - std::tanh (offset) * v;
    }

    static double logCosh (double v) noexcept
    {
        const auto absolute = std::abs (v);
        return absolute + std::log1p (std::exp (-2.0 * absolute)) - 0.69314718055994531;
    }

    //==========================================================================
    // A plain transposed-direct-form-II biquad with DOUBLE coefficients and
    // double state, rather than juce::dsp::IIR::Filter<float>.
    //
    // Not a stylistic preference: the resonance bump sits at 35 Hz while this
    // stage runs at 4x the host rate (192 kHz at a 48 kHz session), a
    // normalised frequency of 1.8e-4. At that ratio a bilinear-transform
    // biquad's coefficients differ from each other in the seventh significant
    // digit, which single precision simply does not have - measured, a float
    // implementation put the "35 Hz" peak's maximum nearer 32 Hz and lost a
    // third of its gain. Double precision has the headroom by three orders of
    // magnitude, and the cost is two extra multiplies per sample on a stage
    // that is branch-skipped whenever it is not in use.
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct BiquadState
    {
        double z1 = 0.0, z2 = 0.0;

        double process (const BiquadCoefficients& c, double x) noexcept
        {
            const auto y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };

    // RBJ cookbook peaking EQ. `amplitude` is sqrt(linear gain), i.e. A.
    BiquadCoefficients makePeaking (double frequencyHz, double q, double amplitude) const noexcept
    {
        const auto omega = juce::MathConstants<double>::twoPi * frequencyHz / oversampledRate;
        const auto cosine = std::cos (omega);
        const auto alpha = std::sin (omega) / (2.0 * q);

        const auto a0 = 1.0 + alpha / amplitude;

        BiquadCoefficients c;
        c.b0 = (1.0 + alpha * amplitude) / a0;
        c.b1 = (-2.0 * cosine) / a0;
        c.b2 = (1.0 - alpha * amplitude) / a0;
        c.a1 = (-2.0 * cosine) / a0;
        c.a2 = (1.0 - alpha / amplitude) / a0;
        return c;
    }

    BiquadCoefficients makeLowPass (double frequencyHz, double q) const noexcept
    {
        const auto omega = juce::MathConstants<double>::twoPi * frequencyHz / oversampledRate;
        const auto cosine = std::cos (omega);
        const auto alpha = std::sin (omega) / (2.0 * q);

        const auto a0 = 1.0 + alpha;

        BiquadCoefficients c;
        c.b0 = ((1.0 - cosine) * 0.5) / a0;
        c.b1 = (1.0 - cosine) / a0;
        c.b2 = c.b0;
        c.a1 = (-2.0 * cosine) / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
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
    double offset = 0.0;
    double bumpDb = 0.0;
    double bumpQ = bumpMinimumQ;
    double highCutHz = highCutMaximumHz;

    // double state throughout the flux path: the integrator accumulates over
    // very long time scales at LF, and single precision measurably drifts
    // there (Neve research section 3.4).
    std::array<double, maxChannels> integratorState {};
    std::array<double, maxChannels> differentiatorState {};
    std::array<double, maxChannels> adaaPreviousInput {};

    BiquadCoefficients bumpCoefficients {};
    BiquadCoefficients highCutCoefficients {};
    std::array<BiquadState, maxChannels> bumpStates {};
    std::array<BiquadState, maxChannels> highCutStates {};
};
