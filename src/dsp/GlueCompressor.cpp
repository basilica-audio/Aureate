#include "GlueCompressor.h"

namespace
{
    //==========================================================================
    // Shared helpers
    //==========================================================================

    // Numerically stable softplus, ln(1 + e^x): the naive form overflows for
    // x above ~88 in float and loses all precision below about -20. Both
    // regimes are reached routinely here - the rectifier's drive stage clamps
    // at +/- 100 V, which lands the diode argument near +1600.
    template <typename FloatType>
    FloatType softplus (FloatType x) noexcept
    {
        const auto positivePart = std::max (x, static_cast<FloatType> (0));
        return positivePart + std::log1p (std::exp (-std::abs (x)));
    }

    // One-pole coefficient for a time constant tau: a = 1 - exp(-1/(fs*tau)),
    // i.e. the state reaches 1 - 1/e of a step in exactly tau seconds
    // regardless of sample rate.
    float onePoleCoefficient (double sampleRate, double tauSeconds) noexcept
    {
        if (tauSeconds <= 0.0 || sampleRate <= 0.0)
            return 1.0f;

        return static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * tauSeconds)));
    }

    //==========================================================================
    // Vari-Mu timing network (brief section 3.2 item 3)
    //==========================================================================

    // Continuous-time rates of the three-capacitor network, one row per
    // release switch position:
    //
    //   C_T dV_T/dt = I_sc - V_T/R_T - (V_T-V_U)/R_U - (V_T-V_V)/R_V
    //   C_U dV_U/dt = (V_T-V_U)/R_U
    //   C_V dV_V/dt = (V_T-V_V)/R_V
    //
    // expressed as the rate constants the discretisation actually needs:
    //   kT  = 1/(R_T C_T)   the main bleed off the fast capacitor
    //   kUT = 1/(R_U C_T)   how fast C_T loses charge into C_U
    //   kU  = 1/(R_U C_U)   how fast C_U follows C_T
    //   kVT, kV             the same pair for the third capacitor
    //
    // A zero row entry means that capacitor is switched out of circuit, and
    // its state then simply stays where it was - which is the physically
    // correct behaviour and the reason position switching preserves state.
    //
    // These are calibrated against the *measured* 37%-recovery targets of the
    // hardware class (0.3 / 0.8 / 2 / 5 s for the four fixed positions), not
    // against the numbers printed on its front panel (0.1 / 0.3 / 0.6 / 1.2 s,
    // which is what comp_release's choice strings show). That gap is a real
    // property of the circuit, documented in docs/manual.md rather than
    // quietly "fixed".
    struct NetworkRates
    {
        double kT, kUT, kU, kVT, kV;
    };

    constexpr NetworkRates networkRates[GlueCompressor::numReleases] = {
        //   kT        kUT        kU         kVT       kV
        { 1.0 / 0.30, 0.0,       0.0,       0.0,      0.0      },  // pos 1
        { 1.0 / 0.80, 0.0,       0.0,       0.0,      0.0      },  // pos 2
        { 1.0 / 0.90, 1.0 / 1.5, 1.0 / 5.0, 0.0,      0.0      },  // pos 3
        { 1.0 / 1.60, 1.0 / 2.4, 1.0 / 14.0, 0.0,     0.0      },  // pos 4
        // Auto: all three capacitors in circuit. A lone transient charges
        // only C_T and recovers on the fast bleed; sustained gain reduction
        // slowly fills C_U and C_V, which then hold C_T up and stretch the
        // recovery by several times. Program dependence as an emergent
        // property of the network, not as a level-triggered rule.
        { 1.0 / 0.50, 1.0 / 1.0, 1.0 / 6.0, 1.0 / 3.0, 1.0 / 30.0 }
    };

    // 1/C_T, i.e. how many volts per second one amp of rectifier current
    // develops across the fast capacitor. Together with the rectifier's
    // I_max = 0.5 A this sets the maximum slew rate of the control voltage
    // (60 kV/s), which is what makes the Vari-Mu attack a slew rather than an
    // exponential: a bigger overshoot does not charge proportionally faster,
    // it simply takes proportionally longer.
    constexpr double timingInputGain = 120000.0;

    // Inverse of a 3x3 matrix stored row-major. The matrices involved here
    // are strictly diagonally dominant by construction (the diagonal carries
    // 1 + (T/2) * sum of the row's rates), so no pivoting is needed and the
    // determinant is never near zero.
    bool invert3x3 (const std::array<double, 9>& m, std::array<double, 9>& out) noexcept
    {
        const auto c00 = m[4] * m[8] - m[5] * m[7];
        const auto c01 = m[5] * m[6] - m[3] * m[8];
        const auto c02 = m[3] * m[7] - m[4] * m[6];

        const auto determinant = m[0] * c00 + m[1] * c01 + m[2] * c02;

        if (std::abs (determinant) < 1.0e-300)
            return false;

        const auto inverseDeterminant = 1.0 / determinant;

        out[0] = c00 * inverseDeterminant;
        out[1] = (m[2] * m[7] - m[1] * m[8]) * inverseDeterminant;
        out[2] = (m[1] * m[5] - m[2] * m[4]) * inverseDeterminant;
        out[3] = c01 * inverseDeterminant;
        out[4] = (m[0] * m[8] - m[2] * m[6]) * inverseDeterminant;
        out[5] = (m[2] * m[3] - m[0] * m[5]) * inverseDeterminant;
        out[6] = c02 * inverseDeterminant;
        out[7] = (m[1] * m[6] - m[0] * m[7]) * inverseDeterminant;
        out[8] = (m[0] * m[4] - m[1] * m[3]) * inverseDeterminant;

        return true;
    }

    std::array<double, 9> multiply3x3 (const std::array<double, 9>& a, const std::array<double, 9>& b) noexcept
    {
        std::array<double, 9> result {};

        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                result[static_cast<size_t> (row * 3 + column)] =
                    a[static_cast<size_t> (row * 3 + 0)] * b[static_cast<size_t> (0 * 3 + column)]
                    + a[static_cast<size_t> (row * 3 + 1)] * b[static_cast<size_t> (1 * 3 + column)]
                    + a[static_cast<size_t> (row * 3 + 2)] * b[static_cast<size_t> (2 * 3 + column)];

        return result;
    }

    //==========================================================================
    // Vari-Mu sidechain statics (brief section 3.2 items 1 and 2)
    //==========================================================================

    // Class-B softplus dead zone: the grid-bias network's own turn-on curve.
    // Threshold, ratio and knee interact through it - that IS the authentic
    // behaviour of this circuit class, not a modelling compromise.
    constexpr float variMuDriveGain = 8.4f;      // the drive stage ahead of the rectifier
    constexpr float variMuDriveClamp = 100.0f;   // the stage's own rail
    constexpr float variMuDiodeVolts = 0.3f;     // V_d
    constexpr float variMuDiodeLambda = 10.0f;   // lambda (turn-on sharpness)
    constexpr float variMuOutputOhms = 160.0f;   // R_out
    constexpr float variMuMaxAmps = 0.5f;        // I_max (the current limit)

    // How much detector drive one unit of rectified signal produces. Chosen so
    // that at comp_threshold = 0 dB a sine at the plugin's -18 dBFS RMS
    // calibration point sits right in the softplus knee.
    constexpr float variMuSidechainScale = 59.0f;

    // comp_threshold maps logarithmically onto the AC grid-drive coefficient.
    float variMuPhiAc (float thresholdDb) noexcept
    {
        constexpr float minPhi = 0.05f;
        constexpr float maxPhi = 1.0f;
        const auto proportion = juce::jlimit (0.0f, 1.0f, (thresholdDb + 30.0f) / 40.0f);
        return maxPhi * std::pow (minPhi / maxPhi, proportion);
    }

    // comp_ratio maps onto the DC dead-zone width. A wider dead zone is a
    // softer knee and a gentler slope, so the "2:1" position is the widest.
    constexpr float variMuPhiDc (int ratioIndex) noexcept
    {
        constexpr float values[GlueCompressor::numRatios] = { 0.5f, 0.3f, 0.15f };
        return values[ratioIndex < 0 ? 0 : (ratioIndex > 2 ? 2 : ratioIndex)];
    }

    // ...and onto how hard the sidechain drives the rectifier, which is what
    // actually separates a 2:1-flavoured slope from a 10:1-flavoured one in a
    // topology whose gain cell is shared across all three positions.
    constexpr float variMuRatioDrive (int ratioIndex) noexcept
    {
        constexpr float values[GlueCompressor::numRatios] = { 0.45f, 0.9f, 2.2f };
        return values[ratioIndex < 0 ? 0 : (ratioIndex > 2 ? 2 : ratioIndex)];
    }

    //==========================================================================
    // Gain cell (brief section 3.2 item 4)
    //==========================================================================

    // Normalised transconductance of a remote-cutoff twin triode operated at
    // a fixed anode voltage, as the analytic derivative dI_a/dV_gk of a Koren
    // triode law. The control voltage V_T pushes the grid further negative,
    // g_m falls, and the stage's gain falls with it - that is the entire gain
    // cell, and the reason its law is smooth, monotone and (unlike a VCA) not
    // dB-linear.
    //
    // Honesty note (continuing docs/design-brief.md section 6's tradition):
    // these are published-triode-law parameters fitted to give the documented
    // control range and curvature, not a measurement of a specific device. No
    // test in this repo asserts that this "sounds like" any particular piece
    // of hardware - they assert behavioural invariants (knee softness, slew
    // ordering, network time-constant ratios) that follow from the topology.
    constexpr double tubeAnodeVolts = 250.0;
    constexpr double tubeMu = 35.0;
    constexpr double tubeKp = 25.0;
    constexpr double tubeKvb = 300.0;
    constexpr double tubeExponent = 1.4;
    constexpr double tubeBiasVolts = -7.2;

    double triodeTransconductance (double gridVolts) noexcept
    {
        const auto sqrtTerm = std::sqrt (tubeKvb + tubeAnodeVolts * tubeAnodeVolts);
        const auto z = tubeKp * (1.0 / tubeMu + gridVolts / sqrtTerm);

        // e1 = (V_a / Kp) * ln(1 + e^z); strictly positive for every finite z,
        // so the pow() below never sees a negative base.
        const auto e1 = (tubeAnodeVolts / tubeKp) * softplus (z);

        if (e1 <= 1.0e-30)
            return 0.0;

        // d(e1)/d(V_gk) = V_a * sigmoid(z) / sqrt(Kvb + V_a^2)
        const auto sigmoid = 1.0 / (1.0 + std::exp (-z));
        const auto de1 = tubeAnodeVolts * sigmoid / sqrtTerm;

        return tubeExponent * std::pow (e1, tubeExponent - 1.0) * de1;
    }
}

//==============================================================================
void GlueCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    // 10 ms section crossfade, 30 ms law crossfade (brief section 3.6).
    sectionMix.reset (sampleRate, 0.010);
    sectionMix.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);

    lawMix.reset (sampleRate, 0.030);
    lawMix.setCurrentAndTargetValue (1.0f);

    invThresholdLinear.reset (sampleRate, 0.030);
    makeupLinear.reset (sampleRate, 0.030);
    sidechainHpfHzSmoothed.reset (sampleRate, 0.020);

    updateThresholdTarget();
    invThresholdLinear.setCurrentAndTargetValue (invThresholdLinear.getTargetValue());

    makeupLinear.setTargetValue (juce::Decibels::decibelsToGain (makeupDb));
    makeupLinear.setCurrentAndTargetValue (makeupLinear.getTargetValue());

    sidechainHpfHzSmoothed.setTargetValue (juce::jmax (20.0f, sidechainHpfHz));
    sidechainHpfHzSmoothed.setCurrentAndTargetValue (sidechainHpfHzSmoothed.getTargetValue());

    updateTimingCoefficients();
    rebuildTimingNetworks();
    rebuildGainCellTable();

    reset();
}

void GlueCompressor::reset()
{
    vca = {};
    variMu = {};
    appliedGain = 1.0f;
    currentGrDb = 0.0f;
    sidechainHpfState = 0.0f;
    outgoingLaw = activeLaw;
    lawMix.setCurrentAndTargetValue (1.0f);
    sectionMix.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
}

//==============================================================================
void GlueCompressor::updateThresholdTarget()
{
    // The stored linear threshold is the PEAK amplitude of a sine whose RMS
    // sits at (thresholdReferenceDbfsRms + thresholdDb) - the detector is a
    // peak rectifier, so it must be compared against a peak. Stored as its
    // reciprocal because the per-sample loop multiplies rather than divides.
    const auto thresholdPeak = juce::Decibels::decibelsToGain (thresholdReferenceDbfsRms + thresholdDb)
                                * juce::MathConstants<float>::sqrt2;

    invThresholdLinear.setTargetValue (1.0f / juce::jmax (1.0e-9f, thresholdPeak));
}

void GlueCompressor::updateTimingCoefficients()
{
    attackCoefficient = onePoleCoefficient (sampleRate, attackTauSeconds (attackIndex));

    if (releaseIndex == autoReleaseIndex)
    {
        // Dual-time-constant ladder (brief section 3.1 "Auto release"). The
        // fast bleed empties v1; the ladder coefficient bleeds v1 into the
        // slow reservoir v2, which then holds v1 up. v2 is only ever charged
        // during the *release* branch, so a lone transient (all attack, no
        // sustained ripple) leaves it empty and recovers fast, while seconds
        // of sustained gain reduction fill it and stretch the tail - the
        // behaviour is emergent from the ladder, not switched on by a level
        // comparator.
        releaseCoefficient = onePoleCoefficient (sampleRate, 0.15);
        ladderCoefficient = onePoleCoefficient (sampleRate, 0.60);
        reservoirCoefficient = onePoleCoefficient (sampleRate, 3.00);
    }
    else
    {
        releaseCoefficient = onePoleCoefficient (sampleRate, releaseTauSeconds (releaseIndex));
        ladderCoefficient = 0.0f;
        reservoirCoefficient = 0.0f;
    }
}

void GlueCompressor::rebuildTimingNetworks()
{
    const auto T = 1.0 / juce::jmax (1.0, sampleRate);

    for (int position = 0; position < numReleases; ++position)
    {
        const auto& rates = networkRates[position];

        // Continuous-time state matrix.
        const std::array<double, 9> a {
            -(rates.kT + rates.kUT + rates.kVT), rates.kUT, rates.kVT,
            rates.kU,                            -rates.kU, 0.0,
            rates.kV,                            0.0,       -rates.kV
        };

        std::array<double, 9> minus {}; // I - (T/2) A
        std::array<double, 9> plus {};  // I + (T/2) A

        for (int i = 0; i < 9; ++i)
        {
            const auto identity = (i % 4 == 0) ? 1.0 : 0.0;
            minus[static_cast<size_t> (i)] = identity - 0.5 * T * a[static_cast<size_t> (i)];
            plus[static_cast<size_t> (i)] = identity + 0.5 * T * a[static_cast<size_t> (i)];
        }

        std::array<double, 9> inverse {};

        if (! invert3x3 (minus, inverse))
        {
            // Unreachable for any physically-meaningful rate set (see
            // invert3x3's comment); fall back to a pure hold rather than
            // producing NaN coefficients.
            timingNetworks[static_cast<size_t> (position)] = {};
            timingNetworks[static_cast<size_t> (position)].step = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
            continue;
        }

        auto& target = timingNetworks[static_cast<size_t> (position)];
        target.step = multiply3x3 (inverse, plus);

        // Input vector: only C_T is driven, with b = [1/C_T, 0, 0].
        for (int row = 0; row < 3; ++row)
            target.input[static_cast<size_t> (row)] =
                inverse[static_cast<size_t> (row * 3 + 0)] * T * timingInputGain;
    }
}

void GlueCompressor::rebuildGainCellTable()
{
    const auto reference = triodeTransconductance (tubeBiasVolts);
    const auto scale = reference > 0.0 ? 1.0 / reference : 0.0;

    // One extra entry beyond the nominal range on each side so the cubic
    // interpolation below never reads outside the array.
    for (int i = 0; i < gainCellTableSize + 3; ++i)
    {
        const auto volts = gainCellMaxVolts * static_cast<double> (i - 1)
                            / static_cast<double> (gainCellTableSize - 1);
        const auto gm = triodeTransconductance (tubeBiasVolts - std::max (0.0, volts));

        gainCellTable[static_cast<size_t> (i)] = static_cast<float> (juce::jlimit (0.0, 1.0, gm * scale));
    }
}

float GlueCompressor::gainCellGain (double vT) const noexcept
{
    const auto clamped = juce::jlimit (0.0, gainCellMaxVolts, vT);
    const auto position = clamped * static_cast<double> (gainCellTableSize - 1) / gainCellMaxVolts;

    const auto base = static_cast<int> (position);
    const auto fraction = static_cast<float> (position - static_cast<double> (base));

    // Catmull-Rom (cubic Hermite with centripetal-free tangents) over the
    // four samples straddling the read position - the table's leading pad
    // entry means `base` indexes p1 directly.
    const auto p0 = gainCellTable[static_cast<size_t> (base)];
    const auto p1 = gainCellTable[static_cast<size_t> (base + 1)];
    const auto p2 = gainCellTable[static_cast<size_t> (base + 2)];
    const auto p3 = gainCellTable[static_cast<size_t> (juce::jmin (base + 3, gainCellTableSize + 2))];

    const auto a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const auto a1 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const auto a2 = -0.5f * p0 + 0.5f * p2;

    return juce::jlimit (0.0f, 1.0f, ((a0 * fraction + a1) * fraction + a2) * fraction + p1);
}

//==============================================================================
void GlueCompressor::setEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == enabled)
        return;

    enabled = shouldBeEnabled;
    sectionMix.setTargetValue (enabled ? 1.0f : 0.0f);
}

void GlueCompressor::setLaw (Law newLaw)
{
    if (newLaw == activeLaw)
        return;

    outgoingLaw = activeLaw;
    activeLaw = newLaw;

    // Warm-start the incoming law's envelope from the gain reduction the
    // outgoing one is currently applying, so the 30 ms crossfade blends two
    // signals that already agree instead of ramping into a step.
    if (activeLaw == Law::vca)
        warmStartVcaFromGrDb (currentGrDb);
    else
        warmStartVariMuFromGrDb (currentGrDb);

    lawMix.setCurrentAndTargetValue (0.0f);
    lawMix.setTargetValue (1.0f);
}

void GlueCompressor::setThresholdDb (float newThresholdDb)
{
    thresholdDb = newThresholdDb;
    updateThresholdTarget();
}

void GlueCompressor::setRatioIndex (int newIndex)
{
    ratioIndex = juce::jlimit (0, numRatios - 1, newIndex);
}

void GlueCompressor::setAttackIndex (int newIndex)
{
    newIndex = juce::jlimit (0, numAttacks - 1, newIndex);

    if (newIndex == attackIndex)
        return;

    attackIndex = newIndex;
    updateTimingCoefficients();
}

void GlueCompressor::setReleaseIndex (int newIndex)
{
    newIndex = juce::jlimit (0, numReleases - 1, newIndex);

    if (newIndex == releaseIndex)
        return;

    // Deliberately does NOT touch vca/variMu state: the capacitors keep their
    // charge across a switch throw, which is both what the hardware does and
    // what keeps the gain reduction continuous (test 6.7's "< 1 dB
    // discontinuity" assertion).
    releaseIndex = newIndex;
    updateTimingCoefficients();
}

void GlueCompressor::setMakeupDb (float newMakeupDb)
{
    makeupDb = newMakeupDb;
    makeupLinear.setTargetValue (juce::Decibels::decibelsToGain (newMakeupDb));
}

void GlueCompressor::setSidechainHpfHz (float newCutoffHz)
{
    sidechainHpfHz = juce::jmax (20.0f, newCutoffHz);
    sidechainHpfHzSmoothed.setTargetValue (sidechainHpfHz);
}

bool GlueCompressor::isFullyBypassed() const noexcept
{
    return ! enabled && ! sectionMix.isSmoothing() && sectionMix.getCurrentValue() <= 0.0f;
}

//==============================================================================
void GlueCompressor::warmStartVcaFromGrDb (float grDb)
{
    vca.v1 = juce::jlimit (0.0f, 40.0f, grDb);
    vca.v2 = vca.v1;
}

void GlueCompressor::warmStartVariMuFromGrDb (float grDb)
{
    const auto targetGain = juce::Decibels::decibelsToGain (-juce::jmax (0.0f, grDb));

    // Straight scan of the (monotonically decreasing) gain table: bounded,
    // allocation-free, and only ever executed on a law change, never
    // per-sample.
    double volts = 0.0;

    for (int i = 1; i <= gainCellTableSize; ++i)
    {
        if (gainCellTable[static_cast<size_t> (i)] <= targetGain)
        {
            volts = gainCellMaxVolts * static_cast<double> (i - 1)
                     / static_cast<double> (gainCellTableSize - 1);
            break;
        }
    }

    variMu.vT = volts;
    variMu.vU = volts;
    variMu.vV = volts;
}

//==============================================================================
float GlueCompressor::vcaStep (float rectified, float inverseThreshold) noexcept
{
    const auto relative = rectified * inverseThreshold;

    // LINEAR-domain overshoot test feeding a dB-domain target: this asymmetry
    // is exactly where the VCA law's program-dependent attack comes from. A
    // signal 20 dB over threshold does not merely charge the timing capacitor
    // to twice the target of one 10 dB over, it charges it through a much
    // larger initial error.
    const auto target = relative > 1.0f ? ratioK (ratioIndex) * FastMath::gainToDb (relative) : 0.0f;

    if (target > vca.v1)
    {
        vca.v1 += attackCoefficient * (target - vca.v1);
    }
    else
    {
        vca.v1 -= releaseCoefficient * vca.v1 + ladderCoefficient * (vca.v1 - vca.v2);
        vca.v2 += reservoirCoefficient * (vca.v1 - vca.v2);
    }

    // The timing states are in dB, so they decay towards - and must actually
    // REACH - exactly zero, or a silent bus leaves denormals circulating in
    // the loop forever (test 6.18).
    if (vca.v1 < 1.0e-6f)
        vca.v1 = 0.0f;

    if (vca.v2 < 1.0e-6f)
        vca.v2 = 0.0f;

    vca.v1 = juce::jlimit (0.0f, 40.0f, vca.v1);

    return FastMath::dbToGain (-vca.v1);
}

float GlueCompressor::variMuStep (float rectified) noexcept
{
    const auto phiAc = variMuPhiAc (thresholdDb);
    const auto phiDc = variMuPhiDc (ratioIndex);

    // Note what is deliberately NOT here: the rectified signal is not
    // normalised by the threshold the way the VCA law's `relative` is. In
    // this topology the threshold control IS the AC grid-drive coefficient
    // (phiAc) - turning it down lowers the whole sidechain rather than moving
    // a comparison point - which is precisely why threshold, ratio and knee
    // interact instead of being independent. Normalising here would have
    // produced a tidier control surface and the wrong circuit.
    const auto potential = phiAc * rectified * variMuSidechainScale * variMuRatioDrive (ratioIndex);

    // Class-B softplus dead zone (brief Eq. 10): near-zero below the bias,
    // then asymptotically linear. Threshold, ratio and knee all interact
    // through it by construction.
    const auto stage1 = softplus (potential - phiDc) - softplus (-potential - phiDc);

    const auto driven = juce::jlimit (-variMuDriveClamp, variMuDriveClamp, variMuDriveGain * stage1);

    // Soft diode turn-on into the timing capacitor, then a hard current
    // limit. Above the limit the capacitor charges at a fixed volts-per-
    // second, which is the entire reason this law's attack is a slew: a
    // larger overshoot takes proportionally LONGER to reach its own final
    // gain reduction, where a fixed-tau exponential would take exactly as
    // long regardless (test 6.6).
    const auto difference = driven - static_cast<float> (variMu.vT);
    const auto diodeArgument = variMuDiodeLambda * difference / (2.0f * variMuDiodeVolts) - variMuDiodeLambda;
    const auto nominalCurrent = (2.0f * variMuDiodeVolts / (variMuDiodeLambda * variMuOutputOhms))
                                 * softplus (diodeArgument);
    const auto current = nominalCurrent
                          - (variMuMaxAmps / 10.0f) * softplus (10.0f * nominalCurrent / variMuMaxAmps - 10.0f);

    const auto& network = timingNetworks[static_cast<size_t> (releaseIndex)];

    const auto vT = variMu.vT;
    const auto vU = variMu.vU;
    const auto vV = variMu.vV;
    const auto drive = static_cast<double> (current);

    variMu.vT = network.step[0] * vT + network.step[1] * vU + network.step[2] * vV + network.input[0] * drive;
    variMu.vU = network.step[3] * vT + network.step[4] * vU + network.step[5] * vV + network.input[1] * drive;
    variMu.vV = network.step[6] * vT + network.step[7] * vU + network.step[8] * vV + network.input[2] * drive;

    // Physical bound: the control voltage cannot go negative (the rectifier
    // only ever sources current) and cannot exceed the supply.
    variMu.vT = juce::jlimit (0.0, 120.0, variMu.vT);
    variMu.vU = juce::jlimit (0.0, 120.0, variMu.vU);
    variMu.vV = juce::jlimit (0.0, 120.0, variMu.vV);

    if (variMu.vT < 1.0e-9) variMu.vT = 0.0;
    if (variMu.vU < 1.0e-9) variMu.vU = 0.0;
    if (variMu.vV < 1.0e-9) variMu.vV = 0.0;

    return gainCellGain (variMu.vT);
}

//==============================================================================
void GlueCompressor::process (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();
    const auto numChannels = block.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Neutrality gate: fully disabled means fully out of circuit. Nothing is
    // read, nothing is written, and no gain of 1.0 is multiplied through -
    // this is what makes comp_enable=false bit-identical rather than merely
    // numerically transparent.
    if (isFullyBypassed())
    {
        // Leave the loop in a clean, denormal-free state so re-enabling
        // starts from unity rather than from whatever the last block left.
        vca = {};
        variMu = {};
        appliedGain = 1.0f;
        currentGrDb = 0.0f;
        sidechainHpfState = 0.0f;
        return;
    }

    // Block-rate coefficient work only - the trig call below is why the
    // sidechain high-pass corner is smoothed and applied per block, exactly
    // like the engine's IIR stages.
    const auto hpfHz = sidechainHpfHzSmoothed.skip (static_cast<int> (numSamples));
    sidechainHpfActive = hpfHz > 20.5f;

    if (sidechainHpfActive)
    {
        const auto warped = std::tan (juce::MathConstants<float>::pi
                                       * juce::jlimit (10.0f, static_cast<float> (sampleRate) * 0.45f, hpfHz)
                                       / static_cast<float> (sampleRate));
        sidechainHpfG = warped / (1.0f + warped);
    }
    else
    {
        sidechainHpfState = 0.0f;
    }

    const auto monoScale = numChannels > 1 ? 0.5f : 1.0f;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto inverseThreshold = invThresholdLinear.getNextValue();

        // Mono-summed sidechain: one detector for the whole bus, so the image
        // never shifts under gain reduction (test 6.8 asserts the applied
        // gain is bit-identical between channels).
        float sidechain = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
            sidechain += block.getChannelPointer (channel)[sample];

        sidechain *= monoScale;

        if (sidechainHpfActive)
        {
            const auto v = (sidechain - sidechainHpfState) * sidechainHpfG;
            const auto lowPass = v + sidechainHpfState;
            sidechainHpfState = lowPass + v;
            sidechain -= lowPass;
        }

        // THE feedback tap: the detector sees the signal after the gain cell,
        // using the gain computed one sample ago. The one-sample delay is the
        // hardware's own propagation delay, not an approximation to be solved
        // away - and it keeps the loop explicit, so the result is independent
        // of how the host slices the buffer.
        const auto rectified = std::abs (sidechain * appliedGain);

        const auto blend = lawMix.getNextValue();

        float gain;

        if (blend < 1.0f)
        {
            const auto incoming = activeLaw == Law::vca ? vcaStep (rectified, inverseThreshold)
                                                        : variMuStep (rectified);
            const auto outgoing = outgoingLaw == Law::vca ? vcaStep (rectified, inverseThreshold)
                                                          : variMuStep (rectified);
            gain = outgoing + blend * (incoming - outgoing);
        }
        else
        {
            gain = activeLaw == Law::vca ? vcaStep (rectified, inverseThreshold) : variMuStep (rectified);
        }

        appliedGain = gain;

        const auto makeup = makeupLinear.getNextValue();
        const auto mix = sectionMix.getNextValue();

        // out = in + mix * (in * gain * makeup - in): a 10 ms ramp on the
        // section's *contribution*, so enabling never clicks and disabling
        // lands back on exactly the input.
        const auto factor = 1.0f + mix * (gain * makeup - 1.0f);

        for (size_t channel = 0; channel < numChannels; ++channel)
            block.getChannelPointer (channel)[sample] *= factor;
    }

    currentGrDb = juce::jmax (0.0f, -FastMath::gainToDb (juce::jmax (1.0e-6f, appliedGain)));
}
