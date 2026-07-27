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
        inline float logCosh (float v) noexcept
        {
            const auto absolute = std::abs (v);
            return absolute + std::log1p (std::exp (-2.0f * absolute)) - 0.6931471806f;
        }

        // Antiderivatives of the three *un-biased* base curves. Any additive
        // constant cancels in the ADAA difference, so none is carried.
        inline float tapeAntiderivative (float v) noexcept
        {
            return logCosh (v);
        }

        inline float consoleAntiderivative (float v) noexcept
        {
            // d/dv [ 4 * ln cosh(v/2) ] = 2 tanh(v/2), which is exactly
            // TapeSaturator::detail::consoleSoftKnee with its scale of 2.
            constexpr float scale = TapeSaturator::detail::consoleSoftKneeScale;
            return scale * scale * logCosh (v / scale);
        }

        inline float valveAntiderivative (float v) noexcept
        {
            // d/dv [ |v| + e^(-|v|) - 1 ] = sign(v) * (1 - e^(-|v|)) on both
            // branches (for v > 0: 1 - e^(-v); for v < 0: -1 + e^(v)), so the
            // form is valid across zero without a special case - verified
            // numerically in tests/AdaaAliasingTests.cpp.
            const auto absolute = std::abs (v);
            return absolute + std::exp (-absolute) - 1.0f;
        }

        inline float baseAntiderivative (float v, TapeSaturator::Model model) noexcept
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

        inline float baseCurve (float v, TapeSaturator::Model model) noexcept
        {
            switch (model)
            {
                case TapeSaturator::Model::console:
                    return TapeSaturator::detail::consoleSoftKnee (v);
                case TapeSaturator::Model::valve:
                    return TapeSaturator::detail::exponentialSoftClip (v);
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
        return detail::baseCurve (x + bias, model) - detail::baseCurve (bias, model);
    }

    // Its antiderivative. The bias is handled the way the brief specifies -
    // f(x+b) - f(b) integrates to F(x+b) - f(b)*x - so the recentring that
    // guarantees silence-in/silence-out survives into the ADAA path unchanged.
    inline float antiderivative (float x, float bias, TapeSaturator::Model model) noexcept
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

        return (antiderivative (x, bias, model) - antiderivative (previous, bias, model)) / delta;
    }
}
