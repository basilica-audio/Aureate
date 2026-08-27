#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

namespace
{
    // The small, Aureate-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable to sibling plugins (see
    // nave's docs/preset-system-notes.md, the M2 pilot this was copied
    // from).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. This is always
        // "com.yvesvogl.aureate" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every
        // presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Basilica Audio";
        // Presets saved before the suite adopted its trading name still live
        // under the old folder. PresetManager copies them across on first
        // construction - copies, never moves, so an older build still finds
        // its own. See docs/branding.md.
        config.legacyManufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::stringSectionGlue_json, BinaryData::stringSectionGlue_jsonSize },
            { BinaryData::brassBloom_json, BinaryData::brassBloom_jsonSize },
            { BinaryData::choirWarmth_json, BinaryData::choirWarmth_jsonSize },
            { BinaryData::orchestralSubmixCohesion_json, BinaryData::orchestralSubmixCohesion_jsonSize },
            { BinaryData::masterGlueSubtle_json, BinaryData::masterGlueSubtle_jsonSize },
            { BinaryData::vintageTapePad_json, BinaryData::vintageTapePad_jsonSize },
            { BinaryData::valvePush_json, BinaryData::valvePush_jsonSize },
            { BinaryData::parallelGritNewYork_json, BinaryData::parallelGritNewYork_jsonSize },
            { BinaryData::consoleSummingSheen_json, BinaryData::consoleSummingSheen_jsonSize },
            { BinaryData::airAndWeight_json, BinaryData::airAndWeight_jsonSize },
            // v0.3.0: three presets showcasing the Glue section and the Iron
            // stage. Registered additively - the eleven above are unchanged,
            // byte for byte (pinned by SHA-256 in tests/PresetManagerTests.cpp).
            { BinaryData::orchestralBusGlue_json, BinaryData::orchestralBusGlue_jsonSize },
            { BinaryData::softTubeGlue_json, BinaryData::softTubeGlue_jsonSize },
            { BinaryData::ironBusWeight_json, BinaryData::ironBusWeight_jsonSize },
        };
    }

    // v0.1.0 state migration helper (see setStateInformation() below): finds
    // the <PARAM id="..." value="..."/> child element for a given parameter
    // ID inside an APVTS state XmlElement, or nullptr if none exists. APVTS
    // serialises its state as a "PARAMETERS" root with direct "PARAM"
    // children carrying "id"/"value" attributes (JUCE 8.0.14,
    // juce_AudioProcessorValueTreeState.cpp's updateParameterConnectionsToChildTrees()/
    // idPropertyID/valuePropertyID) - this walks that exact shape.
    juce::XmlElement* findParamElement (juce::XmlElement& stateXml, const juce::String& paramId)
    {
        for (auto* child : stateXml.getChildIterator())
            if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == paramId)
                return child;

        return nullptr;
    }

    //==========================================================================
    // State schema versioning (v0.3.0). The attribute is written on the APVTS
    // state XML's root element by getStateInformation().
    //
    // Schema history, defined retroactively so the two pre-v0.3.0 shapes stay
    // addressable even though neither ever carried the attribute:
    //   schema 1 = v0.1.0  - has a <PARAM id="wow_flutter"> entry
    //   schema 2 = v0.2.x  - has wow/flutter, no stateSchema attribute
    //   schema 3 = v0.3.0  - stateSchema="3", the eleven Glue/Iron/Quality/
    //                        Auto Gain parameters exist
    constexpr int currentStateSchema = 3;
    constexpr auto stateSchemaAttribute = "stateSchema";

    // Every APVTS parameter ID introduced in schema 3. A state written by an
    // older schema carries none of them, and juce::AudioProcessorValueTreeState::
    // replaceState() only *updates* parameters it finds in the incoming tree -
    // it leaves anything absent sitting at whatever the instance happened to
    // hold. For eleven parameters whose entire contract is "neutral at
    // default", inheriting a previous session's leftovers instead of the
    // neutral default would silently break the release's central promise, so
    // setStateInformation() below injects each missing entry explicitly at its
    // own default rather than relying on replaceState()'s behaviour.
    const char* const schema3ParameterIds[] = {
        ParamIDs::compEnable, ParamIDs::compModel,   ParamIDs::compThreshold,
        ParamIDs::compRatio,  ParamIDs::compAttack,  ParamIDs::compRelease,
        ParamIDs::compMakeup, ParamIDs::compScHpf,   ParamIDs::iron,
        ParamIDs::quality,    ParamIDs::autoGain
    };
}

//==============================================================================
AureateAudioProcessor::AureateAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    driveDb = apvts.getRawParameterValue (ParamIDs::drive);
    warmthPercent = apvts.getRawParameterValue (ParamIDs::warmth);
    tonePercent = apvts.getRawParameterValue (ParamIDs::tone);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);
    outputDb = apvts.getRawParameterValue (ParamIDs::output);
    biasPercent = apvts.getRawParameterValue (ParamIDs::bias);
    wowPercent = apvts.getRawParameterValue (ParamIDs::wow);
    flutterPercent = apvts.getRawParameterValue (ParamIDs::flutter);
    hissPercent = apvts.getRawParameterValue (ParamIDs::hiss);
    characterIndex = apvts.getRawParameterValue (ParamIDs::character);
    hfTrimDb = apvts.getRawParameterValue (ParamIDs::hfTrim);
    lfTrimDb = apvts.getRawParameterValue (ParamIDs::lfTrim);

    compEnable = apvts.getRawParameterValue (ParamIDs::compEnable);
    compModel = apvts.getRawParameterValue (ParamIDs::compModel);
    compThresholdDb = apvts.getRawParameterValue (ParamIDs::compThreshold);
    compRatioIndex = apvts.getRawParameterValue (ParamIDs::compRatio);
    compAttackIndex = apvts.getRawParameterValue (ParamIDs::compAttack);
    compReleaseIndex = apvts.getRawParameterValue (ParamIDs::compRelease);
    compMakeupDb = apvts.getRawParameterValue (ParamIDs::compMakeup);
    compScHpfHz = apvts.getRawParameterValue (ParamIDs::compScHpf);
    ironPercent = apvts.getRawParameterValue (ParamIDs::iron);
    qualityIndex = apvts.getRawParameterValue (ParamIDs::quality);
    autoGainEnable = apvts.getRawParameterValue (ParamIDs::autoGain);

    jassert (driveDb != nullptr);
    jassert (warmthPercent != nullptr);
    jassert (tonePercent != nullptr);
    jassert (mixPercent != nullptr);
    jassert (outputDb != nullptr);
    jassert (biasPercent != nullptr);
    jassert (wowPercent != nullptr);
    jassert (flutterPercent != nullptr);
    jassert (hissPercent != nullptr);
    jassert (characterIndex != nullptr);
    jassert (hfTrimDb != nullptr);
    jassert (lfTrimDb != nullptr);
    jassert (compEnable != nullptr);
    jassert (compModel != nullptr);
    jassert (compThresholdDb != nullptr);
    jassert (compRatioIndex != nullptr);
    jassert (compAttackIndex != nullptr);
    jassert (compReleaseIndex != nullptr);
    jassert (compMakeupDb != nullptr);
    jassert (compScHpfHz != nullptr);
    jassert (ironPercent != nullptr);
    jassert (qualityIndex != nullptr);
    jassert (autoGainEnable != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed with
    // above (see PresetManager::applyStartupDefault()'s docs). Aureate's
    // factory "Default" preset (presets/factory/default.json, category
    // "Init") is the certified passthrough state - every parameter at its
    // off/neutral position - so a fresh instance's out-of-the-box sound is
    // unchanged from what it always was, now reachable as an explicit,
    // one-click preset too (see docs/presets.md).
    presetManager.applyStartupDefault();
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

    // Idle-rest fix (same rationale as basilica-audio/silentium's reset()):
    // many hosts call reset() on transport stop/suspend, after which
    // processBlock() may not fire again for an arbitrary amount of time.
    // Re-parking the output-level atomic to its own floor converges the
    // needle back toward the dial's low end as soon as the editor's next
    // timer tick reads it, rather than holding the last loud reading
    // indefinitely.
    currentOutputLevelDb.store (-100.0f, std::memory_order_relaxed);
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

    // Publish the meter readings once per block. Relaxed ordering: the
    // editor reads them from a timer callback and a stale-by-one-block value
    // is exactly as correct as a fresh one at 30 Hz refresh.
    currentGrDb.store (engine.getCurrentGrDb(), std::memory_order_relaxed);

    // Output-level meter: the block's peak AFTER the full chain (Output trim
    // included), i.e. exactly what leaves the plugin - see
    // getCurrentOutputLevelDb()'s docs. getMagnitude() is a simple
    // allocation-free scan; skipped for zero-sample/zero-channel blocks so
    // the last real level holds rather than collapsing to the floor.
    const auto numSamples = buffer.getNumSamples();

    if (numSamples > 0 && buffer.getNumChannels() > 0)
        currentOutputLevelDb.store (juce::Decibels::gainToDecibels (buffer.getMagnitude (0, numSamples), -100.0f),
                                     std::memory_order_relaxed);
}

void AureateAudioProcessor::pushParametersToEngine()
{
    engine.setDriveDb (driveDb->load (std::memory_order_relaxed));
    engine.setWarmthProportion (warmthPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setToneProportion (tonePercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setMixProportion (mixPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setOutputDb (outputDb->load (std::memory_order_relaxed));
    engine.setBiasProportion (biasPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setWowProportion (wowPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setFlutterProportion (flutterPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setHissProportion (hissPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setCharacter (static_cast<TapeSaturator::Model> (juce::roundToInt (
        characterIndex->load (std::memory_order_relaxed))));
    engine.setHfTrimDb (hfTrimDb->load (std::memory_order_relaxed));
    engine.setLfTrimDb (lfTrimDb->load (std::memory_order_relaxed));

    // v0.3.0. juce::AudioParameterBool's raw value is 0.0f/1.0f and the
    // choice parameters' is the index as a float, so both go through the same
    // relaxed-atomic read the twelve above use.
    engine.setCompressorEnabled (compEnable->load (std::memory_order_relaxed) >= 0.5f);
    engine.setCompressorLaw (static_cast<GlueCompressor::Law> (juce::roundToInt (
        compModel->load (std::memory_order_relaxed))));
    engine.setCompressorThresholdDb (compThresholdDb->load (std::memory_order_relaxed));
    engine.setCompressorRatioIndex (juce::roundToInt (compRatioIndex->load (std::memory_order_relaxed)));
    engine.setCompressorAttackIndex (juce::roundToInt (compAttackIndex->load (std::memory_order_relaxed)));
    engine.setCompressorReleaseIndex (juce::roundToInt (compReleaseIndex->load (std::memory_order_relaxed)));
    engine.setCompressorMakeupDb (compMakeupDb->load (std::memory_order_relaxed));
    engine.setCompressorSidechainHpfHz (compScHpfHz->load (std::memory_order_relaxed));
    engine.setIronProportion (ironPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setHighQuality (qualityIndex->load (std::memory_order_relaxed) >= 0.5f);
    engine.setAutoGainEnabled (autoGainEnable->load (std::memory_order_relaxed) >= 0.5f);
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

    // Stamp the schema version so a future build can tell a v0.3.0 state apart
    // from the two older shapes without having to infer it from which PARAM
    // entries happen to be present (which is how v0.1 -> v0.2 had to be
    // detected, below - workable for one retired parameter, not a policy).
    xml->setAttribute (stateSchemaAttribute, currentStateSchema);

    copyXmlToBinary (*xml, destData);
}

void AureateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    // v0.1.0 -> v0.2.0 state migration (docs/design-brief.md §7, binding
    // "tolerant import" policy): v0.1.0 saved a single "wow_flutter"
    // parameter, which v0.2.0 splits into independent "wow"/"flutter"
    // parameters (ParamIDs::wow/ParamIDs::flutter). A pre-split state still
    // carries a <PARAM id="wow_flutter" .../> child and none for "wow"/
    // "flutter" (which didn't exist yet in v0.1.0's layout) - detect that
    // shape and add both new PARAM entries at the legacy value before
    // handing the tree to APVTS, so an old session retains a recognisable
    // (if not identical - see the brief's honesty note on Character's bias
    // ceilings changing too) character rather than silently resetting
    // Wow/Flutter to 0% on load. A state that already has "wow"/"flutter"
    // entries (i.e. already v0.2.0-shaped) is left untouched.
    //
    // v0.3.0: the migration below is now explicitly gated on the schema
    // attribute. A state that already declares schema 3 or newer cannot be a
    // v0.1.0 state, so the legacy check is skipped outright rather than
    // relying on the (still true, but incidental) fact that no such state
    // carries a "wow_flutter" entry. An unknown *newer* schema is loaded
    // tolerantly - APVTS keeps whatever parses and ignores parameter IDs this
    // build doesn't know about - rather than refused.
    const auto incomingSchema = xmlState->hasAttribute (stateSchemaAttribute)
                                    ? xmlState->getIntAttribute (stateSchemaAttribute)
                                    : 0; // 0 = "no attribute": schema 1 or 2

    if (incomingSchema < currentStateSchema)
    {
        // Fill in every schema-3 parameter the incoming state does not carry,
        // at that parameter's own default, so an older session/preset always
        // lands on the neutral - and therefore bit-identical - configuration
        // rather than inheriting the previous instance state (see
        // schema3ParameterIds' comment for why replaceState() alone is not
        // enough here).
        for (const auto* id : schema3ParameterIds)
        {
            if (findParamElement (*xmlState, id) != nullptr)
                continue;

            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id));
            jassert (parameter != nullptr);

            if (parameter == nullptr)
                continue;

            auto* injected = xmlState->createNewChildElement ("PARAM");
            injected->setAttribute ("id", id);
            // APVTS persists *plain* (denormalised) parameter values, not
            // normalised 0-1 ones, so the default has to be converted back.
            injected->setAttribute ("value", parameter->convertFrom0to1 (parameter->getDefaultValue()));
        }
    }

    auto* legacyWowFlutterParam = incomingSchema < currentStateSchema
                                      ? findParamElement (*xmlState, ParamIDs::legacyWowFlutter)
                                      : nullptr;

    if (legacyWowFlutterParam != nullptr)
    {
        const auto legacyValue = legacyWowFlutterParam->getStringAttribute ("value");

        if (findParamElement (*xmlState, ParamIDs::wow) == nullptr)
        {
            auto* migratedWow = xmlState->createNewChildElement ("PARAM");
            migratedWow->setAttribute ("id", ParamIDs::wow);
            migratedWow->setAttribute ("value", legacyValue);
        }

        if (findParamElement (*xmlState, ParamIDs::flutter) == nullptr)
        {
            auto* migratedFlutter = xmlState->createNewChildElement ("PARAM");
            migratedFlutter->setAttribute ("id", ParamIDs::flutter);
            migratedFlutter->setAttribute ("value", legacyValue);
        }
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AureateAudioProcessor();
}
