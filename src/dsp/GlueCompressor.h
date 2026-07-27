#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// Aureate's v0.3.0 "Glue" section: a feedback bus compressor with two
// selectable laws, running at the host sample rate ahead of Drive (console
// insert order: dynamics, then console/tape colour).
//
// Why host rate, and why no oversampling here: the detector models no
// nonlinearity that generates audible aliasing. The VCA law's polynomial
// gain-cell distortion is deliberately omitted - glue is envelope behaviour,
// not distortion, and every dB of harmonic colour this plugin wants is
// already supplied by the Character saturator downstream - and the Vari-Mu
// law's rectifier ripple is crushed by its own >= 0.1 s timing-network poles
// before it can fold. That is what lets the whole section add exactly zero
// latency, which is in turn what keeps getLatencySamples() (and therefore the
// dry-path compensation and the Mix-at-0% null) identical to v0.2.1.
//
// Both laws share one plumbing layer:
//   - one mono-summed sidechain, so a stereo bus moves as one body rather
//     than as two independently-ducking channels;
//   - the *feedback* tap: the detector sees the signal AFTER the gain cell
//     (sc * g, with g one sample old). This single decision is what produces
//     the soft, emergent knee, the ratio-dependent effective attack, and the
//     characteristic "it never quite reaches the ratio you dialled" law -
//     none of which are curve-fitted anywhere in this file;
//   - a detector-only high-pass, static makeup gain, and a 10 ms crossfade on
//     the whole section so the enable switch is click-free.
//
// Neutrality contract: when the section is disabled and its crossfade has
// settled, process() returns before touching a single sample. That is what
// makes comp_enable=false a bit-identical bypass rather than a multiply by
// 1.0 (see tests/EngineTests.cpp's in-process A/B null).
class GlueCompressor
{
public:
    // Generic circuit-class names, not hardware brands (suite policy).
    enum class Law
    {
        vca = 0,    // dB-domain timing network, dummy-VCA feedback loop
        variMu = 1  // softplus dead-zone sidechain, slew-limited rectifier,
                    // three-capacitor program-dependent release network
    };

    static constexpr int numRatios = 3;    // 2:1, 4:1, 10:1
    static constexpr int numAttacks = 6;   // 0.1, 0.3, 1, 3, 10, 30 ms
    static constexpr int numReleases = 5;  // 0.1, 0.3, 0.6, 1.2 s, Auto
    static constexpr int autoReleaseIndex = numReleases - 1;

    // Calibration (docs/manual.md): threshold 0 dB means a sine whose RMS is
    // -18 dBFS sits exactly at threshold - the "0 VU" tape/console convention
    // the rest of the plugin's gain staging already assumes. The detector is
    // a full-wave peak rectifier, so the stored linear threshold is that
    // sine's *peak*, not its RMS.
    static constexpr float thresholdReferenceDbfsRms = -18.0f;

    // Explicit, because JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR below
    // declares a (deleted) copy constructor, and declaring any constructor
    // suppresses the implicit default one.
    GlueCompressor() = default;

    //==========================================================================
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Processes `block` in place at the host sample rate. Allocation-free.
    void process (juce::dsp::AudioBlock<float>& block);

    //==========================================================================
    void setEnabled (bool shouldBeEnabled);
    void setLaw (Law newLaw);
    void setThresholdDb (float newThresholdDb);
    void setRatioIndex (int newIndex);
    void setAttackIndex (int newIndex);
    void setReleaseIndex (int newIndex);
    void setMakeupDb (float newMakeupDb);
    void setSidechainHpfHz (float newCutoffHz);

    // True when the section is fully out of circuit - disabled AND its
    // crossfade has reached exactly zero - i.e. when process() is a no-op.
    bool isFullyBypassed() const noexcept;

    // Current gain reduction in dB, positive = attenuating. Updated once per
    // processed sample; read by the processor at block rate for the meter.
    float getCurrentGrDb() const noexcept { return currentGrDb; }

    //==========================================================================
    // The switch positions, exposed as pure functions so tests assert the
    // shipped constants rather than a copy of them.
    //
    // Index clamping uses std::clamp, not juce::jlimit: jlimit is not declared
    // constexpr in JUCE 8.0.14, so calling it here makes these functions
    // impossible to constant-evaluate. MSVC diagnoses that as C3615; Clang
    // accepts it silently (it is ill-formed, no diagnostic required).
    static constexpr float ratioValue (int index) noexcept
    {
        constexpr float values[numRatios] = { 2.0f, 4.0f, 10.0f };
        return values[std::clamp (index, 0, numRatios - 1)];
    }

    // The feedback ratio law: k = R - 1 (2:1 -> 1, 4:1 -> 3, 10:1 -> 9). The
    // loop turns this back into an input/output ratio of 1 + k = R, which is
    // the whole point - the dB-domain target is scaled by k, and the feedback
    // supplies the rest.
    static constexpr float ratioK (int index) noexcept { return ratioValue (index) - 1.0f; }

    static constexpr float attackTauSeconds (int index) noexcept
    {
        constexpr float values[numAttacks] = { 0.0001f, 0.0003f, 0.001f, 0.003f, 0.010f, 0.030f };
        return values[std::clamp (index, 0, numAttacks - 1)];
    }

    // The four fixed release positions' nominal switch markings. The Vari-Mu
    // law's capacitor network does NOT decay with these time constants - its
    // positions are calibrated to the measured 37%-recovery times documented
    // in docs/manual.md, exactly as the hardware class it models behaves.
    static constexpr float releaseTauSeconds (int index) noexcept
    {
        constexpr float values[numReleases - 1] = { 0.1f, 0.3f, 0.6f, 1.2f };
        return values[std::clamp (index, 0, numReleases - 2)];
    }

    //==========================================================================
    // Fast dB/linear conversions. The per-sample hot spot of the VCA law is a
    // gainToDb() and a dbToGain(), so both go through polynomial
    // approximations rather than std::log10/std::pow.
    //
    // Accuracy target: +/- 0.01 dB (brief section 3.1). The implementation
    // below is comfortably inside that - the log path's worst case is ~3e-5 dB
    // and the exp path's ~2e-3 dB - which matters because the *feedback* loop
    // integrates this error: a biased approximation would shift the effective
    // threshold rather than just dither the gain.
    //
    // Both are exactly reproducible (no libm calls in the reduced range, only
    // bit manipulation and multiply-add), which is what keeps the block-size
    // invariance test (6.8) bit-exact.
    struct FastMath
    {
        static float log2 (float x) noexcept
        {
            // Range-reduce to a mantissa in [1, 2), then again to
            // [1/sqrt(2), sqrt(2)) so the odd series below converges on
            // |u| <= 0.1716 instead of |u| <= 1/3.
            std::uint32_t bits {};
            std::memcpy (&bits, &x, sizeof (bits));

            auto exponent = static_cast<int> ((bits >> 23) & 0xFFu) - 127;
            bits = (bits & 0x007FFFFFu) | (127u << 23);

            float mantissa {};
            std::memcpy (&mantissa, &bits, sizeof (mantissa));

            if (mantissa > 1.4142135624f)
            {
                mantissa *= 0.5f;
                ++exponent;
            }

            // log2(m) = (2/ln2) * atanh((m-1)/(m+1)), truncated after u^5.
            const auto u = (mantissa - 1.0f) / (mantissa + 1.0f);
            const auto u2 = u * u;
            const auto series = u * (2.8853900818f + u2 * (0.9617966939f + u2 * 0.5770780164f));

            return static_cast<float> (exponent) + series;
        }

        static float exp2 (float x) noexcept
        {
            x = juce::jlimit (-126.0f, 126.0f, x);

            const auto whole = std::floor (x);
            const auto fraction = x - whole;

            // Degree-5 Taylor series of 2^r on r in [0, 1).
            const auto poly = 1.0f
                              + fraction * (0.6931471806f
                              + fraction * (0.2402265070f
                              + fraction * (0.0555041087f
                              + fraction * (0.0096181291f
                              + fraction * 0.0013333559f))));

            const auto exponentBits = static_cast<std::uint32_t> (static_cast<int> (whole) + 127) << 23;

            float scale {};
            std::memcpy (&scale, &exponentBits, sizeof (scale));

            return poly * scale;
        }

        // 20*log10(x) == (20/log2(10)) * log2(x).
        static float gainToDb (float linear) noexcept
        {
            return linear > 1.0e-20f ? 6.0205999133f * log2 (linear) : -400.0f;
        }

        static float dbToGain (float decibels) noexcept
        {
            return exp2 (0.1660964047f * decibels);
        }
    };

private:
    //==========================================================================
    // The VCA law's state: one linear gain plus the two dB-domain timing
    // capacitors. Both timing states are in dB of gain reduction, which is
    // what makes the release genuinely dB-linear (a constant dB/s slope)
    // rather than the linear-domain "fast then crawling" decay a naive
    // envelope follower gives.
    struct VcaState
    {
        float v1 = 0.0f; // fast timing capacitor (the applied GR)
        float v2 = 0.0f; // slow reservoir - only charged by release-phase
                         // ripple, which is exactly why sustained programme
                         // fills it and a lone transient does not
    };

    // The Vari-Mu law's state: the three capacitor voltages of the timing
    // network, in volts. Physical state, never reset on a switch change (a
    // capacitor does not forget its charge because a rotary switch moved).
    struct VariMuState
    {
        double vT = 0.0;
        double vU = 0.0;
        double vV = 0.0;
    };

    // Trapezoidally-discretised coefficients of the three-state timing
    // network, precomputed once per release position at prepare() time.
    // step is (I - (T/2)A)^-1 (I + (T/2)A); input is (I - (T/2)A)^-1 (T b).
    struct TimingNetworkCoefficients
    {
        std::array<double, 9> step {};
        std::array<double, 3> input {};
    };

    void updateThresholdTarget();
    void updateTimingCoefficients();
    void rebuildTimingNetworks();
    void rebuildGainCellTable();
    void warmStartVariMuFromGrDb (float grDb);
    void warmStartVcaFromGrDb (float grDb);

    float vcaStep (float rectified, float inverseThreshold) noexcept;
    float variMuStep (float rectified) noexcept;
    float gainCellGain (double vT) const noexcept;

    //==========================================================================
    double sampleRate = 44100.0;

    bool enabled = false;
    Law activeLaw = Law::vca;
    Law outgoingLaw = Law::vca;

    int ratioIndex = 0;
    int attackIndex = 4;   // 10 ms
    int releaseIndex = autoReleaseIndex;

    float thresholdDb = 0.0f;
    float makeupDb = 0.0f;
    float sidechainHpfHz = 20.0f;

    // Section enable crossfade (10 ms) and law crossfade (30 ms). Both are
    // multiplicative-free plain linear ramps: they blend a *gain*, not audio,
    // so there is no correlated-cancellation risk to avoid.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sectionMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lawMix;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> invThresholdLinear;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> makeupLinear;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sidechainHpfHzSmoothed;

    // Detector-path high-pass: a single TPT one-pole shared by the mono-summed
    // sidechain (one state, not one per channel - there is only one detector).
    float sidechainHpfG = 0.0f;
    float sidechainHpfState = 0.0f;
    bool sidechainHpfActive = false;

    VcaState vca;
    VariMuState variMu;

    // Applied gain, one sample old: THE feedback tap. Initialised to 1 so the
    // first sample of a freshly-enabled section sees an undisturbed detector.
    float appliedGain = 1.0f;
    float currentGrDb = 0.0f;

    // VCA timing coefficients for the current switch positions.
    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float ladderCoefficient = 0.0f;    // v1 -> v2 coupling (Auto only)
    float reservoirCoefficient = 0.0f; // v2 charging rate  (Auto only)

    std::array<TimingNetworkCoefficients, numReleases> timingNetworks {};

    // Gain-cell lookup: normalised transconductance ratio over the physically
    // reachable control-voltage range, cubic-Hermite interpolated.
    static constexpr int gainCellTableSize = 1024;
    static constexpr double gainCellMaxVolts = 60.0;
    std::array<float, gainCellTableSize + 3> gainCellTable {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueCompressor)
};
