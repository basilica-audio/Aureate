#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "dsp/AureateEngine.h"
#include "dsp/GlueCompressor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <new>

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

//==============================================================================
// 6.15 - the allocation guard, extended over every v0.3.0 path
//==============================================================================
TEST_CASE ("6.15 processBlock allocates nothing with the Glue section running, in either law and every release position",
           "[dsp][rt-safety][alloc][glue]")
{
    // The Vari-Mu law's release network is the interesting case: its
    // coefficients are precomputed per switch position at prepare() time
    // precisely so that throwing the switch on the audio thread cannot touch
    // the heap. Its gain cell is a fixed-size table for the same reason.
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 6.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    setParam (processor, ParamIDs::compEnable, 1.0f);
    setParam (processor, ParamIDs::compThreshold, -12.0f);
    setParam (processor, ParamIDs::compMakeup, 4.0f);
    setParam (processor, ParamIDs::compScHpf, 150.0f);
    setParam (processor, ParamIDs::compModel, 0.0f);
    setParam (processor, ParamIDs::compRatio, 1.0f);
    setParam (processor, ParamIDs::compAttack, 2.0f);
    setParam (processor, ParamIDs::compRelease, 0.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 8; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.5f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 64; ++block)
    {
        // Walk through both laws and every release position, including Auto,
        // while the threshold and makeup sweep continuously underneath.
        setParam (processor, ParamIDs::compModel, static_cast<float> (block % 2));
        setParam (processor, ParamIDs::compRelease, static_cast<float> (block % GlueCompressor::numReleases));
        setParam (processor, ParamIDs::compRatio, static_cast<float> (block % GlueCompressor::numRatios));
        setParam (processor, ParamIDs::compAttack, static_cast<float> (block % GlueCompressor::numAttacks));
        setParam (processor, ParamIDs::compThreshold, -30.0f + static_cast<float> (block) * 0.5f);
        setParam (processor, ParamIDs::compScHpf, 20.0f + static_cast<float> (block) * 5.0f);

        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.5f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

TEST_CASE ("6.15 processBlock allocates nothing with Iron, HQ quality and Auto Gain engaged and toggling",
           "[dsp][rt-safety][alloc]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 12.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    setParam (processor, ParamIDs::iron, 60.0f);
    setParam (processor, ParamIDs::quality, 1.0f);
    setParam (processor, ParamIDs::autoGain, 1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 8; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.5f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 64; ++block)
    {
        // Iron sweeps continuously (so its two filters' coefficients are
        // re-derived every block, the path that would allocate if it went
        // back through juce::dsp::IIR::Coefficients::make*), and all three
        // switches toggle underneath it.
        setParam (processor, ParamIDs::iron, static_cast<float> (block % 64) * 1.5f);
        setParam (processor, ParamIDs::quality, static_cast<float> (block % 2));
        setParam (processor, ParamIDs::autoGain, static_cast<float> ((block / 3) % 2));
        setParam (processor, ParamIDs::character, static_cast<float> (block % 3));

        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.5f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

TEST_CASE ("6.15 Toggling the Glue enable on the audio thread allocates nothing", "[dsp][rt-safety][alloc][glue]")
{
    // The enable is the one switch that can take process() from "returns
    // immediately" to "runs the whole section" between two consecutive
    // blocks, so it is the most likely place for a lazily-allocated
    // something to hide.
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 6.0f);
    setParam (processor, ParamIDs::compEnable, 1.0f);
    setParam (processor, ParamIDs::compThreshold, -15.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 8; ++warmup)
    {
        setParam (processor, ParamIDs::compEnable, static_cast<float> (warmup % 2));
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.6f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 64; ++block)
    {
        setParam (processor, ParamIDs::compEnable, static_cast<float> (block % 2));
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.6f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

//==============================================================================
// Suite-wide hardening wave: the allocation guard itself works.
//
// Every CHECK (guard.count() == 0) above is only as trustworthy as the guard
// that produced the count. A guard that never fires would make all of them
// vacuously true. This test proves it does fire - but the obvious way to
// write it, `new float[64]; delete[] ...;`, is NOT reliable: [expr.new]
// explicitly permits an implementation to elide a new-expression's
// allocation when its storage is never observably used, and Clang does
// exactly that at -O2, which would make this self-test pass while silently
// testing nothing. To defeat the elision, the storage is obtained through a
// direct call to the replaced `::operator new` (a plain function call, not a
// new-expression, so the elision permission in [expr.new] does not apply)
// and then written through a volatile pointer, which makes the allocation
// observably used and therefore impossible to optimise away. Same technique
// as sibling plugins requiem (tests/EngineTests.cpp, "6.12 The allocation
// guard itself works") and triptych (tests/RobustnessTests.cpp, "The
// allocation guard itself works").
TEST_CASE ("The allocation guard itself works", "[dsp][rt-safety][alloc]")
{
    {
        const TestAlloc::AllocationGuard guard;
        auto* deliberate = static_cast<float*> (::operator new (64 * sizeof (float)));
        *static_cast<volatile float*> (deliberate) = 1.0f;
        ::operator delete (deliberate);
        CHECK (guard.count() > 0);
    }

    {
        const TestAlloc::AllocationGuard guard;
        volatile auto sum = 0.0f;

        for (int i = 0; i < 1000; ++i)
            sum = sum + static_cast<float> (i);

        CHECK (guard.count() == 0);
    }
}
