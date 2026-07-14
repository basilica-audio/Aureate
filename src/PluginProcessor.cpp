#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

//==============================================================================
AureateAudioProcessor::AureateAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    driveDb = apvts.getRawParameterValue (ParamIDs::drive);
    warmthPercent = apvts.getRawParameterValue (ParamIDs::warmth);
    tonePercent = apvts.getRawParameterValue (ParamIDs::tone);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);
    outputDb = apvts.getRawParameterValue (ParamIDs::output);
    biasPercent = apvts.getRawParameterValue (ParamIDs::bias);
    wowFlutterPercent = apvts.getRawParameterValue (ParamIDs::wowFlutter);
    hissPercent = apvts.getRawParameterValue (ParamIDs::hiss);
    characterIndex = apvts.getRawParameterValue (ParamIDs::character);
    hfTrimDb = apvts.getRawParameterValue (ParamIDs::hfTrim);
    lfTrimDb = apvts.getRawParameterValue (ParamIDs::lfTrim);

    jassert (driveDb != nullptr);
    jassert (warmthPercent != nullptr);
    jassert (tonePercent != nullptr);
    jassert (mixPercent != nullptr);
    jassert (outputDb != nullptr);
    jassert (biasPercent != nullptr);
    jassert (wowFlutterPercent != nullptr);
    jassert (hissPercent != nullptr);
    jassert (characterIndex != nullptr);
    jassert (hfTrimDb != nullptr);
    jassert (lfTrimDb != nullptr);
}

AureateAudioProcessor::~AureateAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AureateAudioProcessor::createParameterLayout()
{
    return aur::createParameterLayout();
}

//==============================================================================
const juce::String AureateAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AureateAudioProcessor::acceptsMidi() const
{
    return false;
}

bool AureateAudioProcessor::producesMidi() const
{
    return false;
}

bool AureateAudioProcessor::isMidiEffect() const
{
    return false;
}

double AureateAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AureateAudioProcessor::getNumPrograms()
{
    return 1;
}

int AureateAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AureateAudioProcessor::setCurrentProgram (int)
{
}

const juce::String AureateAudioProcessor::getProgramName (int)
{
    return {};
}

void AureateAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void AureateAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() primes the filter coefficients, so the very first block
    // after prepareToPlay() already reflects the host/session's actual
    // parameter values rather than the engine's built-in defaults.
    pushParametersToEngine();

    engine.prepare (spec);

    // Oversampling (4x, applied around the tape saturator and tilt Tone
    // stage) is the only source of reported latency; the dry path is
    // compensated against it internally by AureateEngine's DryWetMixer (see
    // docs/architecture.md).
    setLatencySamples (engine.getLatencySamples());
}

void AureateAudioProcessor::releaseResources()
{
}

void AureateAudioProcessor::reset()
{
    engine.reset();
}

bool AureateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void AureateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    pushParametersToEngine();

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);
}

void AureateAudioProcessor::pushParametersToEngine()
{
    engine.setDriveDb (driveDb->load (std::memory_order_relaxed));
    engine.setWarmthProportion (warmthPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setToneProportion (tonePercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setMixProportion (mixPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setOutputDb (outputDb->load (std::memory_order_relaxed));
    engine.setBiasProportion (biasPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setWowFlutterProportion (wowFlutterPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setHissProportion (hissPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setCharacter (static_cast<TapeSaturator::Model> (juce::roundToInt (
        characterIndex->load (std::memory_order_relaxed))));
    engine.setHfTrimDb (hfTrimDb->load (std::memory_order_relaxed));
    engine.setLfTrimDb (lfTrimDb->load (std::memory_order_relaxed));
}

//==============================================================================
bool AureateAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AureateAudioProcessor::createEditor()
{
    return new AureateAudioProcessorEditor (*this);
}

//==============================================================================
void AureateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void AureateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AureateAudioProcessor();
}
