#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "dsp/AureateEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Permanent audio-thread allocation regression guard (basilica-audio/
// Aureate issue #22): AureateEngine::process() used to unconditionally call
// juce::dsp::IIR::Coefficients<float>::makeLowPass/makeHighShelf/
// makeLowShelf/makePeakFilter for the Warmth low-pass, LF head-bump peak,
// Tone tilt shelf pair, and HF/LF Trim shelves every block - each call
// `new`s a fresh ref-counted Coefficients object (plus its own heap-backed
// Array), so up to 6 allocations/6 deallocations happened per processBlock()
// call. Neither pluginval nor auval do allocation-instrumented profiling,
// and none of the other pre-existing Catch2 tests had an allocation-
// counting mechanism, so this passed CI clean before. This test exercises
// the full plugin with automated Warmth/Tone/HF Trim/LF Trim parameters (so
// the smoothers keep re-deriving coefficients every block, exactly the code
// path issue #22 was in - see src/dsp/AureateEngine.cpp/RealtimeCoefficients.h
// for the fix) and fails if processBlock() ever touches the heap again.
namespace
{
    void setParam (AureateAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("AureateAudioProcessor::processBlock allocates no memory while Warmth/Tone/HF Trim/"
           "LF Trim are moving",
           "[dsp][rt-safety][alloc]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 6.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    // Touch every coefficient-recomputing parameter at least once here,
    // before the guard starts - setValueNotifyingHost()'s very first call
    // for a given parameter can lazily warm up internal JUCE bookkeeping
    // (see sibling plugin overture's AllocationTests.cpp for the same
    // observation), the same reason Drive/Mix above are primed before the
    // loop rather than left at their untouched defaults.
    setParam (processor, ParamIDs::warmth, 10.0f);
    setParam (processor, ParamIDs::tone, -10.0f);
    setParam (processor, ParamIDs::hfTrim, 1.0f);
    setParam (processor, ParamIDs::lfTrim, -1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // Allocation during prepareToPlay()/parameter smoothing settle is
    // expected and allowed - only the steady-state per-block behaviour
    // below is guarded.
    for (int warmup = 0; warmup < 4; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 32; ++block)
    {
        // Continuously move every coefficient-recomputing control every
        // block - this is exactly the "smoothers keep re-deriving
        // coefficients every block" scenario issue #22 was filed against
        // (a fixed/settled parameter wouldn't exercise the bug once its
        // smoother reaches target).
        const auto sweep = static_cast<float> (block) / 32.0f;
        setParam (processor, ParamIDs::warmth, sweep * 100.0f);
        setParam (processor, ParamIDs::tone, -100.0f + sweep * 200.0f);
        setParam (processor, ParamIDs::hfTrim, -6.0f + sweep * 12.0f);
        setParam (processor, ParamIDs::lfTrim, 6.0f - sweep * 12.0f);

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

TEST_CASE ("AureateEngine::process allocates no memory across repeated blocks", "[dsp][engine][rt-safety][alloc]")
{
    // Isolated from PluginProcessor/APVTS so this attributes any regression
    // specifically to AureateEngine's own coefficient recompute (basilica-
    // audio/Aureate issue #22), independent of the processor's parameter
    // plumbing.
    AureateEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    engine.prepare (spec);

    engine.setDriveDb (6.0f);
    engine.setMixProportion (1.0f);
    engine.setWarmthProportion (0.1f);
    engine.setToneProportion (-0.1f);
    engine.setHfTrimDb (1.0f);
    engine.setLfTrimDb (-1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);

    // Warm-up block outside the guard, as above.
    engine.process (block);

    TestAlloc::AllocationGuard guard;

    for (int i = 0; i < 32; ++i)
    {
        // Retarget every coefficient-recomputing control every block so the
        // smoothers stay in motion and process() keeps re-deriving filter
        // coefficients, the same steady-state condition the processor-level
        // test above exercises.
        const auto sweep = static_cast<float> (i) / 32.0f;
        engine.setWarmthProportion (sweep);
        engine.setToneProportion (-1.0f + sweep * 2.0f);
        engine.setHfTrimDb (-6.0f + sweep * 12.0f);
        engine.setLfTrimDb (6.0f - sweep * 12.0f);

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (i) * 512);
        engine.process (block);
    }

    CHECK (guard.count() == 0);
}
