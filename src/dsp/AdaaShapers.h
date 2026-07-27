#pragma once

#include "TapeSaturator.h"

#include <cmath>

// First-order antiderivative anti-aliasing (ADAA1) for Aureate's three
// Character transfer functions - the whole of the v0.3.0 "HQ" quality mode.
//
// This header deliberately does NOT touch TapeSaturator.h. Classic quality
// must keep v0.2.1's exact sample path, bit for bit, and the surest way to
// guarantee that is for the Classic path to still be the same code it always
// was rather than a special case of a new generic one. The closed-form
// antiderivatives below are therefore a parallel, independently-verifiable
// definition (tests/AdaaAliasingTests.cpp checks F' == f on a grid for all
// three models before it checks anything about aliasing).
//
// The method (Parker/Zavalishin/Le Bivic, DAFx-16): instead of point-sampling
// a nonlinearity, integrate it over the segment between consecutive input
// samples,
//
//     y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
//
// with F the antiderivative of f. Sharp corners in f are smeared over the
// sampling interval instead of generating unbounded harmonic energy, so the
// components that would have folded back below Nyquist are attenuated at the
// source. It composes with oversampling rather than replacing it: this runs
// inside the same 4x region the Classic path already used.
//
// Two honest caveats, both test-enforced rather than hand-waved:
//  1. ADAA1 carries an inherent HALF-SAMPLE delay at the rate it runs at (in
//     the linear limit the expression collapses to y[n] = (x[n]+x[n-1])/2, a
//     two-point averager). At 4x that is 0.125 host samples - far below
//     getLatencySamples()'s integer granularity and inaudible, but it does
//     mean HQ and Classic cannot null against each other in the raw time
//     domain. Test 6.9(c) compensates for it explicitly before differencing.
//  2. The same averager has a cos(w/2) magnitude droop, so very high
//     harmonics come out fractionally quieter than in Classic. At 3 kHz
//     inside a 4x/44.1 kHz region that is 0.01 dB; at 30 kHz it is 1.3 dB.
//     HQ is an alias-floor option, not a re-voicing - test 6.9(d) pins the
//     audible harmonics to Classic's within +/- 1 dB.
namespace AdaaShapers
{
    namespace detail
    {
        // ln cosh(v), evaluated without ever forming cosh(v) itself (which
        // overflows for |v| > ~89 in float and is exactly the regime a
        // 24 dB Drive pushes the saturator into):
        //     ln cosh(v) = |v| + ln(1 + e^(-2|v|)) - ln 2
        inline double logCosh (double v) noexcept
        {
            const auto absolute = std::abs (v);
            return absolute + std::log1p (std::exp (-2.0 * absolute)) - 0.69314718055994531;
        }

        // Antiderivatives of the three *un-biased* base curves. Any additive
        // constant cancels in the ADAA difference, so none is carried.
        inline double tapeAntiderivative (double v) noexcept
        {
            return logCosh (v);
        }

        inline double consoleAntiderivative (double v) noexcept
        {
            // d/dv [ 4 * ln cosh(v/2) ] = 2 tanh(v/2), which is exactly
            // TapeSaturator::detail::consoleSoftKnee with its scale of 2.
            constexpr double scale = TapeSaturator::detail::consoleSoftKneeScale;
            return scale * scale * logCosh (v / scale);
        }

        inline double valveAntiderivative (double v) noexcept
        {
            // d/dv [ |v| + e^(-|v|) - 1 ] = sign(v) * (1 - e^(-|v|)) on both
            // branches (for v > 0: 1 - e^(-v); for v < 0: -1 + e^(v)), so the
            // form is valid across zero without a special case - verified
            // numerically in tests/AdaaAliasingTests.cpp.
            const auto absolute = std::abs (v);
            return absolute + std::exp (-absolute) - 1.0;
        }

        inline double baseAntiderivative (double v, TapeSaturator::Model model) noexcept
        {
            switch (model)
            {
                case TapeSaturator::Model::console:
                    return consoleAntiderivative (v);
                case TapeSaturator::Model::valve:
                    return valveAntiderivative (v);
                case TapeSaturator::Model::tape:
                default:
                    return tapeAntiderivative (v);
            }
        }

        // The double-precision twins of TapeSaturator's three curves. They
        // exist so the ADAA arithmetic below can be done in double while the
        // Classic path keeps calling TapeSaturator's own float functions,
        // untouched.
        inline double baseCurve (double v, TapeSaturator::Model model) noexcept
        {
            switch (model)
            {
                case TapeSaturator::Model::console:
                {
                    constexpr double scale = TapeSaturator::detail::consoleSoftKneeScale;
                    return scale * std::tanh (v / scale);
                }
                case TapeSaturator::Model::valve:
                    return std::copysign (1.0 - std::exp (-std::abs (v)), v);
                case TapeSaturator::Model::tape:
                default:
                    return std::tanh (v);
            }
        }
    }

    // The exact function TapeSaturator::processSample() computes, restated
    // here so the antiderivative below can be checked against it directly:
    //     f(x) = base(x + bias) - base(bias)
    inline float shape (float x, float bias, TapeSaturator::Model model) noexcept
    {
        const auto b = static_cast<double> (bias);
        return static_cast<float> (detail::baseCurve (static_cast<double> (x) + b, model)
                                    - detail::baseCurve (b, model));
    }

    // Its antiderivative. The bias is handled the way the brief specifies -
    // f(x+b) - f(b) integrates to F(x+b) - f(b)*x - so the recentring that
    // guarantees silence-in/silence-out survives into the ADAA path unchanged.
    inline double antiderivative (double x, double bias, TapeSaturator::Model model) noexcept
    {
        return detail::baseAntiderivative (x + bias, model)
                - detail::baseCurve (bias, model) * x;
    }

    // Per-channel ADAA1 state: one previous input sample.
    //
    // Only the input is stored, not the previous antiderivative value. The
    // bias moves every block (Warmth/Bias are smoothed), and a cached F(x[n-1])
    // computed under the *previous* block's bias would be inconsistent with
    // the current sample's F(x[n]) - a small step in the output at every block
    // boundary, i.e. a zipper artefact introduced by the optimisation rather
    // than by the parameter. Recomputing both antiderivative evaluations per
    // sample costs one extra transcendental and removes the failure mode
    // entirely.
    struct Adaa1State
    {
        float previousInput = 0.0f;

        void reset() noexcept { previousInput = 0.0f; }
    };

    // The ill-conditioning guard: as x[n] -> x[n-1] the difference quotient
    // becomes 0/0 and loses catastrophic precision well before it becomes
    // exactly that. Below the threshold the correct limit is simply the
    // midpoint evaluation of f, which is continuous with the quotient on
    // either side, so there is no discontinuity where the branch switches.
    inline constexpr float minimumDelta = 1.0e-5f;

    inline float processSample (float x, float bias, TapeSaturator::Model model, Adaa1State& state) noexcept
    {
        const auto previous = state.previousInput;
        state.previousInput = x;

        const auto delta = x - previous;

        if (std::abs (delta) < minimumDelta)
            return shape (0.5f * (x + previous), bias, model);

        // Evaluated in double. The difference quotient divides the difference
        // of two antiderivative values by delta, so it amplifies their
        // absolute rounding error by 1/delta: in single precision, with F
        // reaching magnitudes around 9 and delta allowed down to 1e-5, that
        // is a relative error of a few percent right at the guard threshold -
        // measured as a 0.027 discrepancy against the Classic path on a
        // slowly-varying probe, which is an audible artefact at every turning
        // point of a low-frequency signal, not a rounding detail. In double
        // the same quotient is accurate to about 1e-11.
        const auto b = static_cast<double> (bias);

        return static_cast<float> ((antiderivative (static_cast<double> (x), b, model)
                                     - antiderivative (static_cast<double> (previous), b, model))
                                    / static_cast<double> (delta));
    }
}
