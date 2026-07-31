#include "HubNeedle.h"

#include <cmath>

namespace
{
    struct Tick
    {
        float db;
        float deg;
    };

    // Measured directly against brand/mocks/tubecomp/master-03-clean-base.png
    // (the tubecomp design's production faceplate background) by
    // analysis/measure_dial_ticks.py - NOT copied from any other design's
    // table (basilica-audio/silentium's own dial has a visually different
    // tick layout and is not interchangeable). Re-run that script if the
    // tubecomp master render is ever replaced. Verified visually: overlaying
    // these exact angles back onto a crop of the dial (see the script's own
    // dial_verify.png, generated during development) lands every line
    // exactly on its printed tick mark.
    constexpr std::array<Tick, 9> ticks {
        Tick { -20.0f, -49.55f },
        Tick { -10.0f, -38.05f },
        Tick { -7.0f, -25.25f },
        Tick { -5.0f, -15.50f },
        Tick { -3.0f, -6.35f },
        Tick { 0.0f, 3.80f },
        Tick { 1.0f, 17.95f },
        Tick { 2.0f, 23.10f },
        Tick { 3.0f, 28.55f },
    };
}

namespace basilica::gui
{
    HubNeedle::HubNeedle (Assets assetsIn, juce::String accessibleTitle,
                          float pivotXFractionIn, float pivotYFractionIn, float spriteSizeFractionIn,
                          float bakedAngleDegIn)
        : assets (std::move (assetsIn)), title (std::move (accessibleTitle)),
          pivotXFraction (pivotXFractionIn), pivotYFraction (pivotYFractionIn),
          spriteSizeFraction (spriteSizeFractionIn), bakedAngleDeg (bakedAngleDegIn)
    {
        setTitle (title);
        setDescription (title);

        // Pure display - never steals mouse events from controls that may
        // sit under (or within the bounding box of) this component.
        setInterceptsMouseClicks (false, false);
    }

    HubNeedle::~HubNeedle() = default;

    float HubNeedle::tickAngleDegreesForDb (float db) noexcept
    {
        if (db <= ticks.front().db)
            return ticks.front().deg;

        if (db >= ticks.back().db)
            return ticks.back().deg;

        for (size_t i = 1; i < ticks.size(); ++i)
        {
            if (db <= ticks[i].db)
            {
                const auto& lo = ticks[i - 1];
                const auto& hi = ticks[i];
                const auto span = hi.db - lo.db;
                const auto t = span > 0.0f ? (db - lo.db) / span : 0.0f;
                return lo.deg + t * (hi.deg - lo.deg);
            }
        }

        return ticks.back().deg;
    }

    float HubNeedle::stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept
    {
        if (tauSeconds <= 0.0f || dtSeconds <= 0.0f)
            return target;

        const auto alpha = 1.0f - std::exp (-dtSeconds / tauSeconds);
        return currentSmoothed + (target - currentSmoothed) * alpha;
    }

    void HubNeedle::tick (float dtSeconds) noexcept
    {
        const auto target = targetDb.load (std::memory_order_relaxed);
        const auto next = stepBallistics (smoothedDb, target, dtSeconds, ballisticsTauSeconds);

        if (! juce::approximatelyEqual (next, smoothedDb))
        {
            smoothedDb = next;
            repaint();
        }
    }

    void HubNeedle::setImmediateDbForPreview (float db) noexcept
    {
        targetDb.store (db, std::memory_order_relaxed);
        smoothedDb = db;
        repaint();
    }

    void HubNeedle::paint (juce::Graphics& g)
    {
        if (! assets.needleSprite.isValid())
            return;

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto bounds = getLocalBounds().toFloat();
        const auto pivotX = bounds.getWidth() * pivotXFraction;
        const auto pivotY = bounds.getHeight() * pivotYFraction;

        const auto spriteDrawSize = spriteSizeFraction * juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto spriteScale = spriteDrawSize / (float) assets.needleSprite.getWidth();

        const auto targetDeg = tickAngleDegreesForDb (smoothedDb);

        // CRITICAL: rotationToApply = targetDeg - bakedAngleDeg. The sprite's
        // own rod already sits at bakedAngleDeg (its pose in the master
        // render it was cut from) - drawing it with targetDeg's own value as
        // the rotation would double-apply that baked pose. See HubNeedle.h's
        // top-of-file docs.
        const auto rotationToApplyDeg = targetDeg - bakedAngleDeg;
        const auto rotationRadians = juce::degreesToRadians (rotationToApplyDeg);

        const auto imageHalfW = 0.5f * (float) assets.needleSprite.getWidth();
        const auto imageHalfH = 0.5f * (float) assets.needleSprite.getHeight();

        const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                    .scaled (spriteScale)
                                    .rotated (rotationRadians)
                                    .translated (pivotX, pivotY);

        g.drawImageTransformed (assets.needleSprite, transform, false);
    }

    // Read-only text value interface exposing the current ballistic-smoothed
    // reading, mirroring basilica-audio/silentium's AnalogMeter::
    // MeterValueInterface (JUCE 8.0.14's own juce::AccessibilityTextValueInterface
    // shape).
    class HubNeedle::ValueInterface final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit ValueInterface (const HubNeedle& ownerIn) noexcept : owner (ownerIn) {}

        bool isReadOnly() const override { return true; }

        juce::String getCurrentValueAsString() const override
        {
            return juce::String (owner.smoothedDb, 1) + " dB";
        }

        void setValueAsString (const juce::String&) override {}

    private:
        const HubNeedle& owner;
    };

    std::unique_ptr<juce::AccessibilityHandler> HubNeedle::createAccessibilityHandler()
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this,
            juce::AccessibilityRole::label,
            juce::AccessibilityActions {},
            juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
    }
}
