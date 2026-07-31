#include "gui/HubNeedle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// HubNeedle's ballistic integration and dB->tick-angle mapping are pure,
// static functions precisely so they're testable without a running
// juce::Timer/message loop (see HubNeedle.h's docs).
TEST_CASE ("HubNeedle::stepBallistics step response", "[gui]")
{
    using basilica::gui::HubNeedle;

    SECTION ("non-positive dt or tau snaps straight to target (defensive floor, never divides by zero)")
    {
        CHECK (HubNeedle::stepBallistics (-20.0f, 0.0f, 0.0f, 0.25f) == Catch::Approx (0.0f));
        CHECK (HubNeedle::stepBallistics (-20.0f, 0.0f, 1.0f / 30.0f, 0.0f) == Catch::Approx (0.0f));
    }

    SECTION ("repeated stepping monotonically approaches target without overshoot")
    {
        constexpr float tau = HubNeedle::ballisticsTauSeconds;
        constexpr float dt = 1.0f / 30.0f;
        constexpr float target = 3.0f;

        auto smoothed = -20.0f;
        auto previous = smoothed;

        for (int i = 0; i < 300; ++i)
        {
            smoothed = HubNeedle::stepBallistics (smoothed, target, dt, tau);
            CHECK (smoothed >= previous);
            CHECK (smoothed <= target);
            previous = smoothed;
        }

        CHECK (smoothed == Catch::Approx (target).margin (0.01f));
    }
}

TEST_CASE ("HubNeedle::tickAngleDegreesForDb interpolates the tubecomp master's own measured tick table", "[gui]")
{
    using basilica::gui::HubNeedle;

    // Exact table points - see analysis/measure_dial_ticks.py and
    // HubNeedle.cpp's `ticks` docs for provenance.
    CHECK (HubNeedle::tickAngleDegreesForDb (-20.0f) == Catch::Approx (-49.55f));
    CHECK (HubNeedle::tickAngleDegreesForDb (0.0f) == Catch::Approx (3.80f));
    CHECK (HubNeedle::tickAngleDegreesForDb (3.0f) == Catch::Approx (28.55f));

    SECTION ("midpoint between two adjacent ticks interpolates linearly")
    {
        // -10 -> -38.05deg, -7 -> -25.25deg; -8.5 is exactly halfway.
        CHECK (HubNeedle::tickAngleDegreesForDb (-8.5f) == Catch::Approx (-31.65f));
    }

    SECTION ("values beyond the table clamp to the nearest end, never extrapolate")
    {
        CHECK (HubNeedle::tickAngleDegreesForDb (-60.0f) == Catch::Approx (-49.55f));
        CHECK (HubNeedle::tickAngleDegreesForDb (12.0f) == Catch::Approx (28.55f));
    }

    SECTION ("angles increase monotonically across the whole table (no crossed/reversed ticks)")
    {
        constexpr std::array<float, 9> dbs { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 1.0f, 2.0f, 3.0f };
        for (size_t i = 1; i < dbs.size(); ++i)
            CHECK (HubNeedle::tickAngleDegreesForDb (dbs[i]) > HubNeedle::tickAngleDegreesForDb (dbs[i - 1]));
    }
}

TEST_CASE ("HubNeedle exposes a read-only, unit-suffixed accessible value", "[gui][a11y]")
{
    basilica::gui::HubNeedle::Assets assets; // deliberately default/invalid image - fine, this test never calls paint()
    basilica::gui::HubNeedle needle (assets, "Gain Reduction meter", 0.5f, 0.5f, 0.65f, -0.912f);

    // createAccessibilityHandler() directly (not getAccessibilityHandler()) -
    // the latter only returns non-null once the component has a live native
    // window peer, which this headless, no-message-loop test binary never
    // has.
    const auto handler = needle.createAccessibilityHandler();
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->isReadOnly());

    const auto valueText = valueInterface->getCurrentValueAsString();
    INFO ("HubNeedle accessible value = \"" << valueText.toStdString() << "\"");
    CHECK (valueText.endsWith ("dB"));
}

TEST_CASE ("HubNeedle::setImmediateDbForPreview seeds both target and smoothed reading immediately", "[gui]")
{
    basilica::gui::HubNeedle::Assets assets;
    basilica::gui::HubNeedle needle (assets, "Gain Reduction meter", 0.5f, 0.5f, 0.65f, -0.912f);

    needle.setImmediateDbForPreview (-7.0f);

    const auto handler = needle.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->getCurrentValueAsString().getFloatValue() == Catch::Approx (-7.0f));
}
