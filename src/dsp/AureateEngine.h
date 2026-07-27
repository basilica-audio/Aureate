#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

#include "AdaaShapers.h"
#include "GlueCompressor.h"
#include "IronStage.h"
#include "TapeSaturator.h"

// The complete Aureate signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin (see tests/EngineTests.cpp). Owns all DSP state; every buffer/
// filter/oversampler/delay-line is allocated in prepare() and never
// reallocated on the audio thread.
//
// Signal flow (see docs/architecture.md and docs/design-brief.md for the
// full diagram and the research this v0.2.0 revision is grounded in):
//
//   input -> Wow/Flutter (independent wow/flutter rate+depth, modulated
//            delay) -> Glue compressor (v0.3.0, host rate) -> Drive
//         -> [4x oversampled: Warmth HF-rolloff (tape self-erasure) ->
//            LF head-bump (tape transport resonance) -> saturator
//            (Warmth+Bias, Character-selected model, ADAA in HQ quality) ->
//            Iron transformer stage (v0.3.0) -> tilt-style Tone ->
//            HF/LF Trim -> Hiss (HF-forward shaped noise)]
//         -> downsample -> Auto Gain (v0.3.0) -> Dry/Wet mix -> Output trim
//
// The three v0.3.0 stages all sit at neutral by default and are BRANCH-
// SKIPPED there, not run at a transparent setting: the Glue section returns
// before touching a sample when disabled, the Iron stage is stepped over
// entirely at 0%, Classic quality still runs v0.2.1's own saturator loop, and
// Auto Gain off applies no gain rather than a gain of 1.0f. That is what lets
// an existing session be bit-identical rather than merely inaudibly close
// (tests/EngineTests.cpp's in-process A/B null).
//
// The Glue section runs at the host rate and adds no latency, so
// getLatencySamples() - and therefore the dry-path compensation and the
// Mix-at-0% null - is unchanged from v0.2.1 at every new parameter setting.
//
// The Warmth HF-rolloff filter, the LF head-bump filter, the saturator, the
// Tone tilt shelves, the HF/LF Trim shelves, and the Hiss noise stage
// (including its own dedicated HF-shelf shaping filter) all run inside the
// oversampled domain - this keeps their coefficient updates and the
// nonlinearity's/noise's harmonics all generated/filtered at 4x the host
// sample rate before a single downsample step. Drive (pre-oversampling),
// Wow/Flutter (pre-oversampling), and Output (post-mix) run at the host
// sample rate.
//
// Gain-staging note (docs/design-brief.md §3.7): Drive/Warmth's defaults are
// tuned assuming a nominal -18 dBFS RMS input (the "0 VU" tape-calibration
// convention) - a documented assumption, not a DSP behaviour, but it is why
// the default 6 dB of Drive reads very differently on a hot digital bus than
// on a conservatively gain-staged one, and it is the anchor the M2 factory
// presets use for Drive/Output as a matched pair.
//
// The dry path is delay-compensated against getLatencySamples() via
// juce::dsp::DryWetMixer, so Mix at 0% is a sample-accurate (once shifted by
// getLatencySamples()) passthrough of the input - this is what the plugin's
// null test (tests/EngineTests.cpp) exercises. Output is a final master trim
// applied after the dry/wet mix, so it affects the blended signal as a whole
// rather than only the wet path.
class AureateEngine
{
public:
    AureateEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/oversampler/delay-line state without deallocating.
    // Safe to call from the audio thread (e.g. on playback stop/loop).
    void reset();

    // Processes `block` in place. If `block` exceeds the sample/channel
    // counts declared to prepare(), it is defensively clamped to that
    // capacity - only the leading subset is processed in place, the
    // remainder of `block` is left untouched (see the .cpp for why: the
    // oversampler's internal buffers do not grow past what prepare() gave
    // them, so processing more would be a real out-of-bounds heap write in
    // a Release build - this is a host-contract violation the plugin
    // should never see, but is deliberately not undefined behaviour if it
    // does happen). A zero-sample block is a safe no-op. No allocation
    // occurs here.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units (dB, 0-1/-1-1 proportion, or model
    // index). Safe to call every block from the audio thread - no
    // allocation/locks. Drive and Output are smoothed by the underlying
    // juce::dsp::Gain ramp; Warmth/Tone/Mix/Bias/WowFlutter/Hiss/Trim are
    // smoothed internally and re-applied once per block (see process()).
    void setDriveDb (float newDriveDb);
    void setWarmthProportion (float newProportion01);
    void setToneProportion (float newProportionMinus1To1);
    void setMixProportion (float newProportion01);
    void setOutputDb (float newOutputDb);
    void setBiasProportion (float newProportionMinus1To1);

    // Wow and Flutter (docs/design-brief.md §3.6): split from v0.1.0's
    // single joint "Wow/Flutter" parameter into two independently settable
    // amounts, each still off (0) by default. See the wowFlutter* constants
    // below - only each stage's *depth* scales with its own amount; the
    // fixed base delay both share is always present, so getLatencySamples()
    // never depends on either live amount.
    void setWowProportion (float newProportion01);
    void setFlutterProportion (float newProportion01);

    void setHissProportion (float newProportion01);
    void setCharacter (TapeSaturator::Model newModel);
    void setHfTrimDb (float newTrimDb);
    void setLfTrimDb (float newTrimDb);

    //==========================================================================
    // v0.3.0 parameters. Every one of these is neutral at the value the
    // ParameterLayout defaults to (see src/params/ParameterIds.h).
    void setCompressorEnabled (bool shouldBeEnabled);
    void setCompressorLaw (GlueCompressor::Law newLaw);
    void setCompressorThresholdDb (float newThresholdDb);
    void setCompressorRatioIndex (int newIndex);
    void setCompressorAttackIndex (int newIndex);
    void setCompressorReleaseIndex (int newIndex);
    void setCompressorMakeupDb (float newMakeupDb);
    void setCompressorSidechainHpfHz (float newCutoffHz);

    void setIronProportion (float newProportion01);
    void setHighQuality (bool shouldUseAdaa);
    void setAutoGainEnabled (bool shouldCompensate);

    // Current gain reduction in dB (positive = attenuating), for metering.
    float getCurrentGrDb() const noexcept { return glueCompressor.getCurrentGrDb(); }

    // TEST-ONLY. Forces every v0.3.0 stage out of circuit regardless of its
    // parameter value, so tests/EngineTests.cpp can render the same programme
    // twice through the SAME binary - once at the v0.3.0 defaults, once with
    // the new stages provably skipped - and assert max abs diff == 0.
    //
    // A cross-platform bit-exact golden file could not do this job: std::tanh
    // and std::exp are not bit-identical between Apple libm and the MSVC
    // UCRT, so a pinned golden is green on at most one leg of the CI matrix.
    // An in-process A/B is immune to both that and to codegen shifts from the
    // Classic/HQ restructuring, because both renders run through the same
    // compiled instructions.
    void setNewStagesBypassedForTesting (bool shouldBypass) noexcept
    {
        newStagesBypassedForTesting = shouldBypass;
    }

    // The per-Character Drive compensation exponents used by Auto Gain,
    // exposed so tests assert the shipped constants rather than a copy of
    // them.
    //
    // Measured, not assumed: each was calibrated once against equal-RMS
    // pink-noise renders at Drive 0 versus Drive 18 dB (tests/EngineTests.cpp
    // test 6.17) and then frozen. The brief's starting values of 0.90 / 0.95 /
    // 0.85 left residual errors of -2.08 / -0.63 / -1.82 dB, i.e. they
    // over-compensated, because they did not account for how much of Drive's
    // gain the saturator gives back on its own; the values below are those
    // starting points corrected by the measured error.
    //
    // Honesty note (docs/manual.md): this is a listening/A-B aid, not
    // loudness matching. It compensates Drive, on the wet path only, from a
    // one-point calibration - it does not measure the signal.
    static constexpr float autoGainBeta (TapeSaturator::Model model) noexcept
    {
        switch (model)
        {
            case TapeSaturator::Model::console:
                return 0.915f;
            case TapeSaturator::Model::valve:
                return 0.749f;
            case TapeSaturator::Model::tape:
            default:
                return 0.784f;
        }
    }

    // Total added latency in samples (oversampling + the Wow/Flutter stage's
    // fixed base delay), valid after prepare() has run. This is deliberately
    // independent of the live Wow/Flutter *amounts* (see prepare()), so it
    // never changes except when prepare() itself runs.
    int getLatencySamples() const noexcept { return latencySamples; }

    // The per-Character maximum bias contribution Warmth scales up to at
    // 100% (docs/design-brief.md §3.3's table) - exposed as a public,
    // testable static function (rather than a private constant table) so
    // tests/PresetManagerTests.cpp-adjacent DSP tests can assert the actual
    // production constants directly, closing the loop between the brief's
    // table and the shipped mapping. Values (research-derived, not
    // hardware-measured - see docs/design-brief.md §6's Honesty section):
    // Tape 0.12 (least asymmetric - real tape hysteresis is close to
    // symmetric/odd-dominant), Console 0.10 (moderate - blended
    // even+odd, transparent until pushed), Valve 0.30 (most asymmetric -
    // the authentically single-ended/even-harmonic-forward archetype, kept
    // at v0.1.0's old shared ceiling).
    static constexpr float characterMaxWarmthBias (TapeSaturator::Model model) noexcept
    {
        switch (model)
        {
            case TapeSaturator::Model::console:
                return 0.10f;
            case TapeSaturator::Model::valve:
                return 0.30f;
            case TapeSaturator::Model::tape:
            default:
                return 0.12f;
        }
    }

private:
    // Warmth's bias contribution depends on BOTH the live Warmth proportion
    // AND the current Character model (characterMaxWarmthBias() above) - two
    // independent setters (setWarmthProportion(), setCharacter()) can each
    // change it, so both call this shared helper (also called from
    // prepare()) rather than duplicating the "proportion * characterMaxBias"
    // computation.
    void updateWarmthBiasTarget();

    static constexpr int oversamplingFactorPow2 = 2; // 2^2 = 4x oversampling
    static constexpr double smoothingTimeSeconds = 0.05;

    // Warmth mapping: 0% -> no audible rolloff (cutoff pinned high, near the
    // oversampled Nyquist), no head bump, and a symmetric (unbiased)
    // saturator; 100% -> gentle darkening (cutoff down to 3 kHz), full head
    // bump, and maximally biased/asymmetric saturation (bias ceiling is now
    // Character-dependent - see characterMaxWarmthBias() above, replacing
    // v0.1.0's single shared warmthMaxBias).
    static constexpr float warmthMinLowPassHz = 20000.0f;
    static constexpr float warmthMaxLowPassHz = 3000.0f;

    // LF head-bump (docs/design-brief.md §3.2, new in v0.2.0): a gentle
    // resonant peak modelling tape-transport head-bump resonance (research
    // notes §3 - genuinely tape-specific and speed-dependent, but Aureate
    // has no discrete tape-speed control to key off of, so this is a
    // deliberate single-point approximation, not a measured curve - see the
    // brief's Honesty section §6). Scales linearly from 0 dB at 0% Warmth to
    // headBumpMaxDb at 100% Warmth, at a fixed center/Q.
    static constexpr float headBumpHz = 80.0f;
    static constexpr float headBumpMaxDb = 1.5f;
    static constexpr float headBumpQ = 0.9f;

    // Tilt Tone mapping: +/-100% maps to +/-maxTiltDb at each shelf, in
    // opposite directions, so 0% leaves both shelves at 0 dB (flat/unity).
    // Honesty note (docs/design-brief.md §3.4): this is a dual independent-
    // corner shelf pair, not a textbook single-pivot tilt EQ (canonical tilt
    // topologies pivot around one frequency - research notes §7) - kept
    // as-is (no range/topology change), documented accurately rather than
    // renamed, since it already does what the manual promises.
    static constexpr float maxTiltDb = 6.0f;
    static constexpr float tiltLowShelfHz = 200.0f;
    static constexpr float tiltHighShelfHz = 4000.0f;
    // Butterworth (maximally-flat) Q for the Warmth low-pass and the Tone/
    // HF/LF Trim shelves. The LF head-bump peak uses its own headBumpQ
    // above (a resonant peak, not a maximally-flat shelf).
    static constexpr float filterQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // Bias mapping: +/-100% maps to +/-maxExplicitBias, added directly to
    // Warmth's own bias contribution (see mapWarmthToBias in the .cpp). The
    // combined bias is clamped to +/-maxCombinedBias so automating both
    // controls to their extremes at once can't push the saturator into a
    // fully one-sided (musically broken) operating point.
    static constexpr float maxExplicitBias = 0.3f;
    static constexpr float maxCombinedBias = 0.9f;

    // HF/LF Trim: independent fixed-frequency shelves, deliberately at
    // different corner frequencies than Tone's tilt shelves (further out
    // towards the extremes) so the two controls feel distinct rather than
    // redundant - Tone reshapes the broad midrange balance, Trim tames/adds
    // air or weight at the very top/bottom.
    static constexpr float maxTrimDb = 6.0f;
    static constexpr float hfTrimHz = 8000.0f;
    static constexpr float lfTrimHz = 150.0f;

    // Hiss: linear (not dB-skewed) amount-to-gain mapping, so 0% is exactly
    // silent (no noise floor at all when off) rather than merely very quiet.
    // maxHissLinearGain is the peak per-sample noise amplitude at 100%,
    // chosen so full Hiss is clearly audible (a deliberate "vintage" option)
    // without ever approaching clipping headroom on its own.
    static constexpr float maxHissLinearGain = 0.006f; // ~ -44 dBFS peak at 100%

    // Hiss spectral-tilt correction (docs/design-brief.md §3.5, new in
    // v0.2.0): real tape hiss is HF-forward broadband noise (research notes
    // §6), but generating it pre-downsample (kept for real-time-safety/
    // determinism - see class comment) means it would otherwise inherit the
    // downsampler's own gentle darkening, reading as "muffled static"
    // instead of "hiss". A dedicated high-shelf boost applied to the noise
    // tap specifically (not the rest of the signal) fixes this - see
    // hissShelf below.
    static constexpr float hissShelfHz = 4000.0f;
    static constexpr float hissShelfDb = 4.0f;

    // Wow/Flutter: a small, fixed base delay is always present (even at 0%
    // amount on both Wow and Flutter) so the stage's contribution to
    // getLatencySamples() never depends on either live parameter value -
    // only prepare()-time constants - which is what makes it safe to
    // automate Wow/Flutter during playback without the plugin's reported
    // latency changing underneath the host. v0.2.0 (docs/design-brief.md
    // §3.6): Wow and Flutter are now independently settable amounts (each
    // still scaling only its own LFO's depth, never the shared base delay);
    // Flutter's rate moved from v0.1.0's 6.5 Hz (at the very edge of the
    // sourced 6-100 Hz flutter band, risking a "second wow" read) to 11 Hz,
    // more clearly separated from Wow's 0.7 Hz. Depths/rates are
    // deliberately not extreme: this emulates gentle tape-transport
    // instability, not a chorus/vibrato effect.
    static constexpr float wowFlutterBaseDelayMs = 6.0f;
    static constexpr float wowFlutterMaxWowDepthMs = 2.5f;
    static constexpr float wowFlutterMaxFlutterDepthMs = 0.35f;
    static constexpr float wowFlutterWowRateHz = 0.7f;
    static constexpr float wowFlutterFlutterRateHz = 11.0f;

    double sampleRate = 44100.0;
    double oversampledRate = 44100.0;

    // The sample/channel counts declared to prepare() - everything
    // downstream (most importantly the oversampler's internal per-stage
    // buffers, see juce::dsp::Oversampling::initProcessing()) is sized from
    // these and does not grow later, so process() clamps to them
    // defensively (see process()'s doc comment and issue #13).
    size_t preparedMaximumBlockSize = 0;
    size_t preparedNumChannels = 0;

    // Runs at the host sample rate, before Drive/oversampling (see class
    // comment) - modulates the wet path's transport delay by a sum of two
    // independent sine LFOs ("wow" and "flutter"), each scaled by its own
    // smoothed amount.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> wowFlutterDelayLine;
    double wowFlutterBaseDelaySamples = 0.0;
    double wowFlutterMaxWowDepthSamples = 0.0;
    double wowFlutterMaxFlutterDepthSamples = 0.0;
    double wowFlutterWowPhase = 0.0;
    double wowFlutterFlutterPhase = 0.0;
    double wowFlutterWowPhaseIncrement = 0.0;
    double wowFlutterFlutterPhaseIncrement = 0.0;

    // v0.3.0 Glue section: host rate, ahead of Drive (console insert order).
    // See GlueCompressor.h for why it is not oversampled and why that matters
    // for getLatencySamples().
    GlueCompressor glueCompressor;

    juce::dsp::Gain<float> driveGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // v0.3.0 Iron stage: inside the oversampled region, after the saturator.
    IronStage ironStage;

    // v0.3.0 HQ quality: one ADAA1 state per channel, living here rather than
    // inside TapeSaturator so the Classic path's code is untouched.
    static constexpr size_t maxOversampledChannels = 8;
    std::array<AdaaShapers::Adaa1State, maxOversampledChannels> adaaStates {};

    // These all run inside the oversampled block (see class comment).
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> warmthLowPass;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> headBumpPeak;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tiltLowShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tiltHighShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> hfTrimShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lfTrimShelf;

    // Hiss's dedicated HF-shelf shaping filter (see hissShelfHz/hissShelfDb
    // above) - a fixed filter (not parameter-driven), primed once in
    // prepare() and never recomputed per block. hissScratchBuffer is a
    // pre-allocated (prepare()-time only) scratch buffer sized to the
    // largest oversampled block prepare() declared, used to generate and
    // filter the noise tap separately from the main signal path before
    // adding it in (see process()) - allocated once, never on the audio
    // thread.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> hissShelf;
    juce::AudioBuffer<float> hissScratchBuffer;

    // Deterministic (fixed-seed) so Hiss's contribution is bit-reproducible
    // across runs, which keeps its own tests non-flaky; real-time-safe
    // (juce::Random::nextFloat() does not allocate or lock).
    juce::Random hissRandom { 1 };

    juce::dsp::Gain<float> outputGain;

    // Sized generously above getLatencySamples()'s worst case across the
    // whole supported sample-rate range, so setWetLatency() never exceeds
    // the mixer's internal delay-line capacity. getLatencySamples() is now
    // the sum of the oversampler's latency (tens of samples) and
    // Wow/Flutter's fixed base delay (wowFlutterBaseDelayMs, which scales
    // with sample rate - up to ~1152 samples at 192 kHz); a too-small
    // capacity here was previously observed to silently corrupt (or, in a
    // Debug build, assert inside juce::dsp::DelayLine::setDelay) the dry-
    // path delay compensation at high sample rates once Wow/Flutter's base
    // delay was added to getLatencySamples().
    juce::dsp::DryWetMixer<float> dryWetMixer { 8192 };

    // Warmth's low-pass cutoff uses multiplicative smoothing (appropriate
    // for a quantity perceived logarithmically, like Hz); everything else
    // below uses linear smoothing.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> warmthLowPassHzSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> warmthBiasSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> explicitBiasSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> headBumpDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tiltDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wowAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> flutterAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hissAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfTrimDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lfTrimDbSmoothed;

    // v0.3.0. Iron's amount is smoothed linearly (it drives both a gain-like
    // quantity and two filter targets); Auto Gain's compensation is smoothed
    // multiplicatively because it is a gain.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ironAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> autoGainSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied to the smoothers on every prepare() so re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // default or lets a smoother start from an invalid 0 Hz.
    float lastWarmthProportion01 = 0.35f;
    float lastTiltProportion = 0.0f;
    float lastMixProportion = 1.0f;
    float lastBiasProportion = 0.0f;
    float lastWowProportion01 = 0.0f;
    float lastFlutterProportion01 = 0.0f;
    float lastHissProportion01 = 0.0f;
    float lastHfTrimDb = 0.0f;
    float lastLfTrimDb = 0.0f;

    TapeSaturator::Model character = TapeSaturator::Model::tape;

    // v0.3.0 last-commanded values, re-applied on every prepare() for the same
    // reason the v0.2.0 ones above are.
    float lastIronProportion01 = 0.0f;
    float lastDriveDb = 6.0f;
    bool highQuality = false;
    bool autoGainEnabled = false;
    bool newStagesBypassedForTesting = false;

    // Recomputes Auto Gain's target from the current Drive/Character, called
    // from setDriveDb(), setCharacter(), setAutoGainEnabled() and prepare().
    void updateAutoGainTarget();

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateEngine)
};
