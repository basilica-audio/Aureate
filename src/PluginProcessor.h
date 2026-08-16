#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/AureateEngine.h"
#include "presets/PresetManager.h"

// Aureate: tape/console saturation "glue" for orchestral material. Signal
// flow lives in AureateEngine (src/dsp) so it stays unit-testable
// independent of this AudioProcessor; this class is just APVTS + host
// plumbing around it.
class AureateAudioProcessor final : public juce::AudioProcessor
{
public:
    AureateAudioProcessor();
    ~AureateAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // AureateAudioProcessorEditor's PresetBar can talk to it directly - the
    // same "processor owns it, editor references it" pattern apvts itself
    // already uses.
    basilica::presets::PresetManager presetManager;

private:
    AureateEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* driveDb = nullptr;
    std::atomic<float>* warmthPercent = nullptr;
    std::atomic<float>* tonePercent = nullptr;
    std::atomic<float>* mixPercent = nullptr;
    std::atomic<float>* outputDb = nullptr;
    std::atomic<float>* biasPercent = nullptr;
    std::atomic<float>* wowPercent = nullptr;
    std::atomic<float>* flutterPercent = nullptr;
    std::atomic<float>* hissPercent = nullptr;
    std::atomic<float>* characterIndex = nullptr;
    std::atomic<float>* hfTrimDb = nullptr;
    std::atomic<float>* lfTrimDb = nullptr;

    // v0.3.0 additions. Same relaxed-atomics pattern as above: resolved once
    // at construction, read (never searched for) every block.
    std::atomic<float>* compEnable = nullptr;
    std::atomic<float>* compModel = nullptr;
    std::atomic<float>* compThresholdDb = nullptr;
    std::atomic<float>* compRatioIndex = nullptr;
    std::atomic<float>* compAttackIndex = nullptr;
    std::atomic<float>* compReleaseIndex = nullptr;
    std::atomic<float>* compMakeupDb = nullptr;
    std::atomic<float>* compScHpfHz = nullptr;
    std::atomic<float>* ironPercent = nullptr;
    std::atomic<float>* qualityIndex = nullptr;
    std::atomic<float>* autoGainEnable = nullptr;

public:
    // Current gain reduction in dB (positive = attenuating), published by the
    // audio thread once per block and polled by the editor's timer. Not an
    // APVTS parameter: it is a *measurement*, not a setting - making it a
    // parameter would expose it to host automation, undo history and preset
    // serialisation, none of which mean anything for a meter.
    float getCurrentGrDb() const noexcept { return currentGrDb.load (std::memory_order_relaxed); }

    // M3 GUI metering: the current block's peak level in dBFS, measured on
    // the main bus AFTER the full engine chain (including Output trim) -
    // i.e. exactly what leaves the plugin. Same pattern as sibling
    // basilica-audio/silentium's getInputLevelDb(): a plain relaxed atomic
    // store from processBlock(), no locks/allocation, polled from the
    // editor's timer. Floored at -100 dB for silent/empty blocks. This is
    // what the VU needle now reads (see PluginEditor.cpp's
    // vuDbFromOutputLevelDb()) - the classic tape/console-glue "driving the
    // meter into the red" metaphor requires an actual signal-level reading,
    // which gain reduction (never positive) could not provide.
    float getCurrentOutputLevelDb() const noexcept { return currentOutputLevelDb.load (std::memory_order_relaxed); }

private:
    std::atomic<float> currentGrDb { 0.0f };

    // Floored at -100 dB, matching basilica-audio/silentium's
    // meterInputLevelDb convention - see reset()'s own idle-rest docs.
    std::atomic<float> currentOutputLevelDb { -100.0f };

    // Pushes the current APVTS parameter values into `engine`. Shared by
    // prepareToPlay() (seeding the engine before the first block) and
    // processBlock() (every block) so the two can never drift apart.
    void pushParametersToEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateAudioProcessor)
};
