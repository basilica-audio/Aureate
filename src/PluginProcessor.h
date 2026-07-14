#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/AureateEngine.h"

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
    std::atomic<float>* wowFlutterPercent = nullptr;
    std::atomic<float>* hissPercent = nullptr;
    std::atomic<float>* characterIndex = nullptr;
    std::atomic<float>* hfTrimDb = nullptr;
    std::atomic<float>* lfTrimDb = nullptr;

    // Pushes the current APVTS parameter values into `engine`. Shared by
    // prepareToPlay() (seeding the engine before the first block) and
    // processBlock() (every block) so the two can never drift apart.
    void pushParametersToEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AureateAudioProcessor)
};
