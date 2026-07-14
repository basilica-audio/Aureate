#include "dsp/TapeSaturator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE ("TapeSaturator: silence in, silence out, for any bias", "[dsp][saturator]")
{
    CHECK (TapeSaturator::processSample (0.0f, 0.0f) == Catch::Approx (0.0f).margin (1e-9));
    CHECK (TapeSaturator::processSample (0.0f, 0.15f) == Catch::Approx (0.0f).margin (1e-9));
    CHECK (TapeSaturator::processSample (0.0f, -0.3f) == Catch::Approx (0.0f).margin (1e-9));
}

TEST_CASE ("TapeSaturator: near-linear for small-signal input", "[dsp][saturator]")
{
    // For small x, tanh(x + bias) - tanh(bias) ~= x * sech^2(bias) (first-
    // order Taylor expansion around x=0). Checking against that predicted
    // small-signal gain - rather than against raw unity gain - is the
    // correct "near-linear" check for a curve that has a fixed insertion
    // loss/gain by design (the bias itself changes the slope at the
    // operating point, independent of any drive/gain staging elsewhere in
    // the chain).
    constexpr float bias = 0.3f;
    const auto sech2 = 1.0f - std::tanh (bias) * std::tanh (bias);

    for (float x : { 1.0e-4f, 5.0e-4f, 1.0e-3f, -1.0e-3f, 2.0e-3f })
    {
        const auto y = TapeSaturator::processSample (x, bias);
        const auto predictedLinear = x * sech2;

        // Margin slightly looser than a single-ULP-scale bound: y is
        // produced by float-precision std::tanh calls chained together
        // (tanh(x+bias) - tanh(bias)), so it accumulates a little more
        // rounding error than the single-multiply predictedLinear.
        CHECK (y == Catch::Approx (predictedLinear).margin (2.0e-6));
    }
}

TEST_CASE ("TapeSaturator: monotonically increasing (no folding)", "[dsp][saturator]")
{
    constexpr float bias = 0.3f;
    float previous = TapeSaturator::processSample (-5.0f, bias);

    for (int i = 1; i <= 200; ++i)
    {
        const auto x = -5.0f + static_cast<float> (i) * 0.05f;
        const auto y = TapeSaturator::processSample (x, bias);

        CHECK (y > previous);
        previous = y;
    }
}

TEST_CASE ("TapeSaturator: bounded output for extreme input, no NaN/Inf", "[dsp][saturator]")
{
    constexpr float bias = 0.3f;

    for (float x : { 1.0e6f, -1.0e6f, std::numeric_limits<float>::max() * 0.5f,
                      -std::numeric_limits<float>::max() * 0.5f, 0.0f })
    {
        const auto y = TapeSaturator::processSample (x, bias);

        CHECK (std::isfinite (y));
        CHECK (std::abs (y) < 2.5f); // tanh() saturates well inside +/-2.5 for any finite bias here
    }
}

TEST_CASE ("TapeSaturator: positive and negative half-cycles saturate at different ceilings", "[dsp][saturator]")
{
    constexpr float bias = 0.3f;

    const auto positiveCeiling = TapeSaturator::processSample (50.0f, bias);
    const auto negativeCeiling = TapeSaturator::processSample (-50.0f, bias);

    // Genuinely asymmetric: the two ceilings must differ in magnitude.
    CHECK (std::abs (positiveCeiling) != Catch::Approx (std::abs (negativeCeiling)).margin (1.0e-3));

    // Zero bias recovers a symmetric (odd) tanh curve as a sanity check that
    // the bias term - not some other bug - is what causes the difference
    // above.
    const auto symmetricPositive = TapeSaturator::processSample (50.0f, 0.0f);
    const auto symmetricNegative = TapeSaturator::processSample (-50.0f, 0.0f);
    CHECK (symmetricPositive == Catch::Approx (-symmetricNegative).margin (1.0e-6));
}

//==============================================================================
// Character models: the 3-argument overload used once a session picks
// something other than the default Tape model (see ParamIDs::character).

TEST_CASE ("TapeSaturator: the Model-taking overload's Tape case matches the 2-argument Tape entry point exactly",
           "[dsp][saturator][character]")
{
    for (float x : { -3.0f, -0.5f, 0.0f, 0.2f, 1.7f })
        for (float bias : { -0.3f, 0.0f, 0.3f })
            CHECK (TapeSaturator::processSample (x, bias, TapeSaturator::Model::tape)
                   == Catch::Approx (TapeSaturator::processSample (x, bias)).margin (1e-9));
}

TEST_CASE ("TapeSaturator: every model gives silence in, silence out, for any bias", "[dsp][saturator][character]")
{
    for (auto model : { TapeSaturator::Model::tape, TapeSaturator::Model::console, TapeSaturator::Model::valve })
        for (float bias : { -0.3f, 0.0f, 0.15f })
            CHECK (TapeSaturator::processSample (0.0f, bias, model) == Catch::Approx (0.0f).margin (1e-6));
}

TEST_CASE ("TapeSaturator: every model produces a bounded, finite output for extreme input", "[dsp][saturator][character]")
{
    for (auto model : { TapeSaturator::Model::tape, TapeSaturator::Model::console, TapeSaturator::Model::valve })
    {
        for (float x : { 1.0e6f, -1.0e6f, std::numeric_limits<float>::max() * 0.5f,
                          -std::numeric_limits<float>::max() * 0.5f, 0.0f })
        {
            const auto y = TapeSaturator::processSample (x, 0.3f, model);
            CHECK (std::isfinite (y));
            CHECK (std::abs (y) < 3.0f);
        }
    }
}

TEST_CASE ("TapeSaturator: Console model is monotonically non-decreasing (may flatten past the clip point)",
           "[dsp][saturator][character]")
{
    constexpr float bias = 0.2f;
    float previous = TapeSaturator::processSample (-5.0f, bias, TapeSaturator::Model::console);

    for (int i = 1; i <= 200; ++i)
    {
        const auto x = -5.0f + static_cast<float> (i) * 0.05f;
        const auto y = TapeSaturator::processSample (x, bias, TapeSaturator::Model::console);

        CHECK (y >= previous - 1.0e-7f);
        previous = y;
    }
}

TEST_CASE ("TapeSaturator: Valve model is strictly monotonically increasing everywhere", "[dsp][saturator][character]")
{
    constexpr float bias = 0.2f;
    float previous = TapeSaturator::processSample (-5.0f, bias, TapeSaturator::Model::valve);

    for (int i = 1; i <= 200; ++i)
    {
        const auto x = -5.0f + static_cast<float> (i) * 0.05f;
        const auto y = TapeSaturator::processSample (x, bias, TapeSaturator::Model::valve);

        CHECK (y > previous);
        previous = y;
    }
}

TEST_CASE ("TapeSaturator: Console and Valve models are asymmetric under a nonzero bias, like Tape",
           "[dsp][saturator][character]")
{
    constexpr float bias = 0.3f;

    for (auto model : { TapeSaturator::Model::console, TapeSaturator::Model::valve })
    {
        const auto positiveCeiling = TapeSaturator::processSample (50.0f, bias, model);
        const auto negativeCeiling = TapeSaturator::processSample (-50.0f, bias, model);

        CHECK (std::abs (positiveCeiling) != Catch::Approx (std::abs (negativeCeiling)).margin (1.0e-3));
    }
}

TEST_CASE ("TapeSaturator: the three models give audibly different output for the same input",
           "[dsp][saturator][character]")
{
    // At a moderately driven amplitude (well past the small-signal linear
    // region for all three curves), the models must not coincide - proving
    // Character actually selects between distinct transfer functions rather
    // than three names for the same curve.
    constexpr float bias = 0.15f;
    constexpr float x = 0.8f;

    const auto tape = TapeSaturator::processSample (x, bias, TapeSaturator::Model::tape);
    const auto console = TapeSaturator::processSample (x, bias, TapeSaturator::Model::console);
    const auto valve = TapeSaturator::processSample (x, bias, TapeSaturator::Model::valve);

    CHECK (tape != Catch::Approx (console).margin (1.0e-3));
    CHECK (tape != Catch::Approx (valve).margin (1.0e-3));
    CHECK (console != Catch::Approx (valve).margin (1.0e-3));
}
