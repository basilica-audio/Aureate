#pragma once

#include <cmath>

// A single-ended, asymmetric soft-knee saturator - the nonlinearity at the
// heart of Aureate's tape-style "Warmth"/"Character" voicing. Pure,
// allocation-free, and stateless, so it is unit-testable in complete
// isolation from the rest of the signal chain (see
// tests/TapeSaturatorTests.cpp) and safe to call per-sample from the audio
// thread (it runs inside AureateEngine's 4x oversampled block).
//
// Default/Tape transfer function: y = tanh(x + bias) - tanh(bias)
//
// Shifting the curve by a fixed bias before re-centring it at y(0) == 0
// gives the two effects "Warmth"/"Bias" are meant to control together with
// the engine's companion HF-rolloff filter:
//   - The positive and negative half-cycles saturate towards different
//     asymptotic ceilings, i.e. genuine asymmetric clipping, emulating
//     single-ended tape/valve saturation rather than a symmetric fuzz.
//   - Because the curve is not globally linear, a zero-mean AC input
//     produces a small even-harmonic-rich, DC-shifted output - the gentle
//     "glue" character this plugin is voiced around.
// Subtracting f(bias) guarantees processSample(0, bias) == 0 for any bias,
// so the saturator never injects a constant DC offset into silence, for
// every model below.
namespace TapeSaturator
{
    // The "Character" parameter's three curve families. Each keeps the same
    // shift-then-recentre-by-bias shape as the original Tape model (see
    // above), so silence-in/silence-out and asymmetric-ceiling behaviour
    // hold identically for all three - only the underlying curve differs.
    // See docs/manual.md for the musical description of each.
    enum class Model
    {
        tape,    // asymmetric tanh - smooth, "infinite" soft compression (the original/default model)
        console, // asymmetric cubic soft-clip - harder-edged, more transparent below the clip point
        valve    // asymmetric exponential saturation - a different even/odd harmonic blend than tape
    };

    namespace detail
    {
        // Cubic soft-clip (a standard textbook soft-clipper, e.g. Zolzer's
        // DAFX): odd, monotonically non-decreasing, hard-flat beyond +/-1
        // rather than tanh's smooth asymptote - a more "console summing bus"
        // character than tape's continuous compression.
        inline float cubicSoftClip (float v) noexcept
        {
            if (v <= -1.0f)
                return -2.0f / 3.0f;
            if (v >= 1.0f)
                return 2.0f / 3.0f;
            return v - (v * v * v) / 3.0f;
        }

        // Exponential saturation: strictly monotonic (never flat, unlike the
        // cubic clip above) and asymptotic to +/-1, giving a different
        // even/odd harmonic blend than either the tanh or cubic curves.
        inline float exponentialSoftClip (float v) noexcept
        {
            return std::copysign (1.0f - std::exp (-std::abs (v)), v);
        }
    }

    // The original two-argument Tape-model entry point. Kept as a separate,
    // unconditional overload (rather than routing through the Model-taking
    // overload below) so it stays trivially inlinable and so existing
    // callers/tests that only know about the Tape model are unaffected by
    // the Character parameter's existence.
    inline float processSample (float x, float bias) noexcept
    {
        return std::tanh (x + bias) - std::tanh (bias);
    }

    // Model-selecting overload used by AureateEngine once Character is
    // wired up to more than just Tape.
    inline float processSample (float x, float bias, Model model) noexcept
    {
        switch (model)
        {
            case Model::console:
                return detail::cubicSoftClip (x + bias) - detail::cubicSoftClip (bias);
            case Model::valve:
                return detail::exponentialSoftClip (x + bias) - detail::exponentialSoftClip (bias);
            case Model::tape:
            default:
                return processSample (x, bias);
        }
    }
}
