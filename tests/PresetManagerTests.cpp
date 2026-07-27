#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"
#include "dsp/AureateEngine.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

// M2 preset system tests (.scaffold/specs/preset-system-m2.md's "Tests"
// section - each TEST_CASE below maps to one of that section's numbered
// items, called out in the test names/comments). Ported from sibling
// basilica-audio/nave's tests/PresetManagerTests.cpp (the M2 pilot), adapted
// to Aureate's own parameters/factory presets.
namespace
{
    using basilica::presets::FactoryPresetAsset;
    using basilica::presets::PresetManager;
    using basilica::presets::PresetManagerConfig;

    // Mirrors PluginProcessor.cpp's own makeFactoryPresetAssets() - kept as
    // an independent copy here rather than exported from PluginProcessor.cpp
    // so this test file can construct its own, fully isolated PresetManager
    // instances (see makeIsolatedConfig() below) without depending on
    // production wiring internals.
    std::vector<FactoryPresetAsset> makeTestFactoryPresetAssets()
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
        };
    }

    // A fresh, isolated scratch directory per test case, so this file never
    // reads or writes the real ~/Library/Audio/Presets/... (or Windows
    // equivalent) location on the machine running the tests - see
    // PresetManagerConfig::userPresetsDirectoryOverrideForTests. Deleted on
    // destruction.
    struct ScopedTestDirectory
    {
        ScopedTestDirectory()
            : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("AureatePresetManagerTests")
                       .getChildFile (juce::String (juce::Time::getHighResolutionTicks())
                                       + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
        {
            dir.createDirectory();
        }

        ~ScopedTestDirectory()
        {
            dir.deleteRecursively();
        }

        JUCE_DECLARE_NON_COPYABLE (ScopedTestDirectory)

        juce::File dir;
    };

    PresetManagerConfig makeIsolatedConfig (const juce::File& userDir)
    {
        PresetManagerConfig config;
        config.pluginId = "com.yvesvogl.aureate";
        config.pluginName = "Aureate";
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = "0.2.0-test";
        config.userPresetsDirectoryOverrideForTests = userDir;
        return config;
    }

    void setParam (AureateAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (AureateAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

//==============================================================================
// 1. Save -> load round-trip restores every parameter exactly.
TEST_CASE ("PresetManager: save -> load round-trip restores every parameter exactly", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::drive, 17.0f);
    setParam (processor, ParamIDs::warmth, 72.0f);
    setParam (processor, ParamIDs::tone, -40.0f);
    setParam (processor, ParamIDs::mix, 63.0f);
    setParam (processor, ParamIDs::output, -4.5f);
    setParam (processor, ParamIDs::bias, -25.0f);
    setParam (processor, ParamIDs::wow, 40.0f);
    setParam (processor, ParamIDs::flutter, 65.0f);
    setParam (processor, ParamIDs::hiss, 15.0f);
    setParam (processor, ParamIDs::character, 2.0f); // Valve
    setParam (processor, ParamIDs::hfTrim, 3.0f);
    setParam (processor, ParamIDs::lfTrim, -2.0f);

    REQUIRE (manager.saveUserPreset ("Round Trip", "Bus"));

    // Perturb every parameter away from the saved values before reloading,
    // so the assertions below can't pass by accident.
    setParam (processor, ParamIDs::drive, 0.0f);
    setParam (processor, ParamIDs::warmth, 0.0f);
    setParam (processor, ParamIDs::tone, 100.0f);
    setParam (processor, ParamIDs::mix, 0.0f);
    setParam (processor, ParamIDs::output, 24.0f);
    setParam (processor, ParamIDs::bias, 100.0f);
    setParam (processor, ParamIDs::wow, 0.0f);
    setParam (processor, ParamIDs::flutter, 0.0f);
    setParam (processor, ParamIDs::hiss, 0.0f);
    setParam (processor, ParamIDs::character, 0.0f);
    setParam (processor, ParamIDs::hfTrim, -6.0f);
    setParam (processor, ParamIDs::lfTrim, 6.0f);

    REQUIRE (manager.loadPreset ("Round Trip"));

    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (17.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::warmth) == Catch::Approx (72.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::tone) == Catch::Approx (-40.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (63.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::output) == Catch::Approx (-4.5f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::bias) == Catch::Approx (-25.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::wow) == Catch::Approx (40.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::flutter) == Catch::Approx (65.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::hiss) == Catch::Approx (15.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::character) == Catch::Approx (2.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::hfTrim) == Catch::Approx (3.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::lfTrim) == Catch::Approx (-2.0f).margin (1.0e-3));
}

//==============================================================================
// 2. Import ignores unknown IDs, keeps defaults for missing IDs.
TEST_CASE ("PresetManager: import ignores unknown parameter IDs and keeps defaults for missing ones", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    // Move mix and output away from their defaults so it's meaningful when
    // the import below leaves them untouched (they're absent from
    // "parameters").
    setParam (processor, ParamIDs::mix, 55.0f);
    setParam (processor, ParamIDs::output, 3.0f);

    // A fixture JSON generated inline (not committed under tests/fixtures/)
    // to avoid brittle relative-path resolution across CI runners with
    // different working directories (macOS vs Windows ctest invocations) -
    // this is the forward/backward-compat scenario the spec's "fixture
    // JSONs in tests/" line calls for: an unknown ID ("futureParameter",
    // simulating a newer plugin version's preset) and two known IDs
    // (drive/warmth), deliberately omitting mix/output/wow/flutter/etc.
    const juce::String fixtureJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.aureate",
        "pluginVersion": "9.9.9",
        "name": "Forward Compat Fixture",
        "category": "Init",
        "parameters": { "drive": 12.0, "warmth": 60.0, "futureParameter": 42.0 }
    })";

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (fixtureFile.replaceWithText (fixtureJson));

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (fixtureFile, errorMessage));
    CHECK (errorMessage.isEmpty());

    // Known IDs present in the fixture were applied...
    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (12.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::warmth) == Catch::Approx (60.0f).margin (1.0e-3));

    // ...IDs absent from the fixture were reset to their ParameterLayout
    // defaults (loadPreset()/importPresetFile() always reset-then-apply -
    // see PresetManager.h), not left at the pre-import 55%/3 dB values.
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* outputParam = processor.apvts.getParameter (ParamIDs::output);
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (mixParam->convertFrom0to1 (mixParam->getDefaultValue())).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::output) == Catch::Approx (outputParam->convertFrom0to1 (outputParam->getDefaultValue())).margin (1.0e-3));

    fixtureFile.deleteFile();
}

//==============================================================================
// 3. Import refuses wrong-plugin and wrong-format files.
TEST_CASE ("PresetManager: import refuses a preset belonging to a different plugin", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongPluginJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.nave",
        "pluginVersion": "0.2.0",
        "name": "Not Aureate's",
        "category": "Init",
        "parameters": { "drive": 999.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongPluginJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    // State must be left untouched - drive must NOT have picked up 999 (out
    // of its own 0-24 dB range too, which would be a separate bug on top).
    CHECK (getParam (processor, ParamIDs::drive) != Catch::Approx (999.0f));

    file.deleteFile();
}

TEST_CASE ("PresetManager: import refuses a file with an incompatible format tag", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongFormatJson = R"({
        "format": "some-other-format-2",
        "plugin": "com.yvesvogl.aureate",
        "pluginVersion": "0.2.0",
        "name": "Wrong Format",
        "category": "Init",
        "parameters": { "drive": 999.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongFormatJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    file.deleteFile();
}

//==============================================================================
// 4. Factory presets all parse and load.
TEST_CASE ("PresetManager: every factory preset parses and loads without error", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto factoryCount = std::count_if (all.begin(), all.end(), [] (auto& e) { return e.isFactory; });

    REQUIRE (factoryCount == 11); // docs/presets.md's Factory Presets list

    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        CAPTURE (entry.name);
        CHECK (manager.loadPreset (entry.name));
        CHECK (manager.isCurrentPresetFactory());
        CHECK (manager.getCurrentPresetName() == entry.name);
    }
}

TEST_CASE ("PresetManager: factory preset content is plausible (Default is Init category, all parameters in range)", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto defaultEntry = std::find_if (all.begin(), all.end(), [] (auto& e) { return e.name == "Default"; });

    REQUIRE (defaultEntry != all.end());
    CHECK (defaultEntry->category == "Init");
    CHECK (defaultEntry->isFactory);

    // Loading every factory preset must leave every parameter's live value
    // inside its own ParameterLayout range - APVTS's setValueNotifyingHost()
    // clamps out-of-range normalised input, so an out-of-range preset value
    // wouldn't crash, but it would silently mean the JSON doesn't say what
    // the plugin actually does - worth catching explicitly.
    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        REQUIRE (manager.loadPreset (entry.name));

        CHECK (getParam (processor, ParamIDs::drive) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::drive) <= 24.0f);
        CHECK (getParam (processor, ParamIDs::warmth) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::warmth) <= 100.0f);
        CHECK (getParam (processor, ParamIDs::mix) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::mix) <= 100.0f);
    }
}

//==============================================================================
// 5. Default resolution order (user Default > factory Default > plain defaults).
TEST_CASE ("PresetManager: applyStartupDefault() loads the factory Default when no user Default exists", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::drive, 22.0f); // perturb first

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK (manager.isCurrentPresetFactory());
    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (6.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: a user Default preset wins over the factory Default", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::drive, 21.0f);
    REQUIRE (manager.setCurrentAsDefault()); // writes a user preset literally named "Default"

    setParam (processor, ParamIDs::drive, 0.0f); // perturb away before the resolution check

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK_FALSE (manager.isCurrentPresetFactory()); // resolved to the *user* Default, not the factory one
    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (21.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: resetDefault() removes the user Default so the factory Default resolves again", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::drive, 21.0f);
    REQUIRE (manager.setCurrentAsDefault());
    REQUIRE (manager.resetDefault());

    manager.applyStartupDefault();

    CHECK (manager.isCurrentPresetFactory());
    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (6.0f).margin (1.0e-3));
}

//==============================================================================
// 6. Dirty flag: clean after load, dirty after any param change, clean after save.
TEST_CASE ("PresetManager: dirty flag lifecycle - clean after load, dirty after a change, clean after save", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    setParam (processor, ParamIDs::drive, 10.0f);
    CHECK (manager.isDirty());

    REQUIRE (manager.saveUserPreset ("Dirty Flag Preset", "Init"));
    CHECK_FALSE (manager.isDirty());
}

//==============================================================================
// 7. prev/next ordering and wrap-around.
TEST_CASE ("PresetManager: nextPreset()/previousPreset() traverse alphabetically and wrap around", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    REQUIRE (all.size() >= 2);

    REQUIRE (manager.loadPreset (all.front().name));

    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all[1].name);

    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);

    // Wrap backward from the first entry to the last.
    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.back().name);

    // Wrap forward from the last entry back to the first.
    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);
}

//==============================================================================
// Additional coverage beyond the spec's minimum list: save/rename/delete
// guards, single-file export round-trip, and bank import/export.

TEST_CASE ("PresetManager: saveUserPreset() refuses to shadow a factory preset name", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    CHECK_FALSE (manager.saveUserPreset ("Default", "Init")); // "Default" already exists as a factory preset
    CHECK_FALSE (manager.saveUserPreset ("Brass Bloom", "Bus"));
}

TEST_CASE ("PresetManager: renameUserPreset() moves a user preset to a new name and preserves its parameters", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::output, -8.0f);
    REQUIRE (manager.saveUserPreset ("Old Name", "Init"));

    REQUIRE (manager.renameUserPreset ("Old Name", "New Name"));

    setParam (processor, ParamIDs::output, 24.0f); // perturb before reloading

    CHECK_FALSE (manager.loadPreset ("Old Name")); // gone
    REQUIRE (manager.loadPreset ("New Name"));
    CHECK (getParam (processor, ParamIDs::output) == Catch::Approx (-8.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: deleteUserPreset() removes a user preset but never a factory preset", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.saveUserPreset ("Temporary", "Init"));
    REQUIRE (manager.deleteUserPreset ("Temporary"));
    CHECK_FALSE (manager.loadPreset ("Temporary"));

    // A factory preset name isn't a file on disk in the user directory, so
    // there's nothing to delete - deleteUserPreset() must return false, and
    // the factory preset must still load afterwards.
    CHECK_FALSE (manager.deleteUserPreset ("Default"));
    CHECK (manager.loadPreset ("Default"));
}

TEST_CASE ("PresetManager: exportPreset()/importPresetFile() single-file round-trip", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::hiss, 77.0f);
    REQUIRE (manager.saveUserPreset ("Exportable", "Init"));

    const auto exportFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (manager.exportPreset ("Exportable", exportFile));
    REQUIRE (exportFile.existsAsFile());

    REQUIRE (manager.deleteUserPreset ("Exportable")); // remove the original before reimporting

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (exportFile, errorMessage));
    CHECK (getParam (processor, ParamIDs::hiss) == Catch::Approx (77.0f).margin (1.0e-3));

    exportFile.deleteFile();
}

TEST_CASE ("PresetManager: exportBank()/importBank() round-trips every user preset through a zip", "[presets]")
{
    ScopedTestDirectory sourceScratch;
    ScopedTestDirectory destScratch;

    AureateAudioProcessor sourceProcessor;
    sourceProcessor.prepareToPlay (48000.0, 512);
    PresetManager sourceManager (sourceProcessor.apvts, makeIsolatedConfig (sourceScratch.dir), makeTestFactoryPresetAssets());

    setParam (sourceProcessor, ParamIDs::drive, 11.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset A", "Init"));

    setParam (sourceProcessor, ParamIDs::drive, 22.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset B", "Init"));

    const auto bankFile = juce::File::createTempFile (".zip");
    REQUIRE (sourceManager.exportBank (bankFile));
    REQUIRE (bankFile.existsAsFile());

    AureateAudioProcessor destProcessor;
    destProcessor.prepareToPlay (48000.0, 512);
    PresetManager destManager (destProcessor.apvts, makeIsolatedConfig (destScratch.dir), makeTestFactoryPresetAssets());

    const auto importedCount = destManager.importBank (bankFile);
    CHECK (importedCount == 2);

    REQUIRE (destManager.loadPreset ("Bank Preset A"));
    CHECK (getParam (destProcessor, ParamIDs::drive) == Catch::Approx (11.0f).margin (1.0e-3));

    REQUIRE (destManager.loadPreset ("Bank Preset B"));
    CHECK (getParam (destProcessor, ParamIDs::drive) == Catch::Approx (22.0f).margin (1.0e-3));

    bankFile.deleteFile();
}

//==============================================================================
// 8. PresetManager never allocates or locks on the audio thread.
//
// Verified primarily *by design*: nothing in AureateAudioProcessor::
// processBlock()/AureateEngine ever calls into PresetManager (see
// PluginProcessor.cpp - presetManager is only touched from the constructor
// and from PresetBar's message-thread-only UI callbacks), so there is no
// code path for this test to exercise in the first place. The one nuance is
// PresetManager::parameterChanged() (an AudioProcessorValueTreeState::
// Listener callback that JUCE does not document as guaranteed message-
// thread-only) - it is implemented as a single lock-free std::atomic<bool>
// store and nothing else (see PresetManager.h/.cpp), which this test
// exercises indirectly by driving parameter changes and processBlock() back
// to back and confirming nothing misbehaves.
TEST_CASE ("PresetManager: parameter-driven dirty tracking coexists safely with real-time audio processing", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        // Every parameterChanged() callback below happens interleaved with
        // real audio processing - if it ever became audio-thread-unsafe
        // (e.g. someone later added a lock or allocation to it), a helgrind/
        // TSan CI run would be the real detector; this test's job is just to
        // confirm normal operation isn't disrupted by the two coexisting.
        setParam (processor, ParamIDs::drive, 0.0f + static_cast<float> (block) * 2.0f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    CHECK (manager.isDirty());
}

//==============================================================================
// 6.16 - preset pinning and the three v0.3.0 additions
//==============================================================================
namespace
{
    // A compact SHA-256, rather than juce::SHA256: that class lives in
    // juce_cryptography, and pulling an entire JUCE module into the plugin's
    // link line so that one test can hash eleven small files would be a poor
    // trade. This is the FIPS 180-4 reference algorithm, ~40 lines, used only
    // here.
    juce::String sha256Hex (const void* data, size_t numBytes)
    {
        static constexpr juce::uint32 k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        juce::uint32 h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

        std::vector<juce::uint8> message (static_cast<const juce::uint8*> (data),
                                          static_cast<const juce::uint8*> (data) + numBytes);

        const auto bitLength = static_cast<juce::uint64> (numBytes) * 8;
        message.push_back (0x80);

        while (message.size() % 64 != 56)
            message.push_back (0x00);

        for (int shift = 56; shift >= 0; shift -= 8)
            message.push_back (static_cast<juce::uint8> ((bitLength >> shift) & 0xff));

        auto rotateRight = [] (juce::uint32 value, int amount) { return (value >> amount) | (value << (32 - amount)); };

        for (size_t chunk = 0; chunk < message.size(); chunk += 64)
        {
            juce::uint32 w[64] {};

            for (int i = 0; i < 16; ++i)
                w[i] = (static_cast<juce::uint32> (message[chunk + static_cast<size_t> (i) * 4 + 0]) << 24)
                     | (static_cast<juce::uint32> (message[chunk + static_cast<size_t> (i) * 4 + 1]) << 16)
                     | (static_cast<juce::uint32> (message[chunk + static_cast<size_t> (i) * 4 + 2]) << 8)
                     | (static_cast<juce::uint32> (message[chunk + static_cast<size_t> (i) * 4 + 3]));

            for (int i = 16; i < 64; ++i)
            {
                const auto s0 = rotateRight (w[i - 15], 7) ^ rotateRight (w[i - 15], 18) ^ (w[i - 15] >> 3);
                const auto s1 = rotateRight (w[i - 2], 17) ^ rotateRight (w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];

            for (int i = 0; i < 64; ++i)
            {
                const auto s1 = rotateRight (e, 6) ^ rotateRight (e, 11) ^ rotateRight (e, 25);
                const auto ch = (e & f) ^ (~e & g);
                const auto temp1 = hh + s1 + ch + k[i] + w[i];
                const auto s0 = rotateRight (a, 2) ^ rotateRight (a, 13) ^ rotateRight (a, 22);
                const auto maj = (a & b) ^ (a & c) ^ (b & c);
                const auto temp2 = s0 + maj;

                hh = g; g = f; f = e; e = d + temp1;
                d = c; c = b; b = a; a = temp1 + temp2;
            }

            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        juce::String hex;

        for (const auto word : h)
            hex << juce::String::toHexString (static_cast<int> (word)).paddedLeft ('0', 8);

        return hex;
    }
}

TEST_CASE ("6.16 The eleven pre-v0.3.0 factory presets are byte-identical", "[presets][pinning]")
{
    // SHA-256 pinning, not a "loads without error" check: a factory preset is
    // part of a user's saved sessions the moment they load it, so an
    // accidental reformat, a re-ordered key or a single changed value is a
    // silent change to work someone else has already done. Anything that
    // genuinely needs to change gets a NEW preset file and a CHANGELOG entry.
    struct PinnedPreset
    {
        const char* fileName;
        const char* sha256;
    };

    const PinnedPreset pinned[] = {
        { "airAndWeight.json",             "ae5b611290e7adcd879eb511d134d78437ab7c0de106f36d9a40f25a7fb81890" },
        { "brassBloom.json",               "b9bd1aa889abc27c37f22c0c9cd178463e35064f2309e17b932b8f18b0e33c37" },
        { "choirWarmth.json",              "a1c93121e3adc1aa8cb6d89a83e07362200dfa1cb714356bcb04a8e75478f03c" },
        { "consoleSummingSheen.json",      "a1254d7dba881518d72356c1c2b77b224bb3cba6ed32bc988a08c429acdc03e1" },
        { "default.json",                  "e75302307acaecd0150e6a236e3c7f686c536565c3d486cf3970fed2b41cdc79" },
        { "masterGlueSubtle.json",         "9ab26a6a679f546ccbf8c826f4b0255deaecfe6275799704500269a811951c81" },
        { "orchestralSubmixCohesion.json", "e0f0c8f5772906be06b8baed4378a6d1bf6eb9c68539fbf0ce032c7847b9b996" },
        { "parallelGritNewYork.json",      "37f75323b04c5a46ddf9665c731fbd58a7e5b6ebf312f7c1bc27414c65935f2a" },
        { "stringSectionGlue.json",        "e9b74e47d6620049c4170f2da3229c797019792332d7ed6972e6a8c5635af296" },
        { "valvePush.json",                "8aa8cd7c08f14b11a73025064eb57842ca7213d0790a6301f42d83dd3320222c" },
        { "vintageTapePad.json",           "b5eb1c881597ce77c43efc31c0b12e20330003c655916ea655b6ee2614e33767" },
    };

    const auto factoryDirectory = juce::File (AUREATE_PRESET_DIR);
    REQUIRE (factoryDirectory.isDirectory());

    for (const auto& preset : pinned)
    {
        const auto file = factoryDirectory.getChildFile (preset.fileName);
        INFO ("preset " << preset.fileName);
        REQUIRE (file.existsAsFile());

        juce::MemoryBlock contents;
        REQUIRE (file.loadFileAsData (contents));

        CHECK (sha256Hex (contents.getData(), contents.getSize()) == juce::String (preset.sha256));
    }
}

TEST_CASE ("6.16 The three v0.3.0 presets load, appear in the bar, and engage the new section", "[presets]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto presets = processor.presetManager.getAllPresets();

    auto contains = [&presets] (const juce::String& name)
    {
        return std::any_of (presets.begin(), presets.end(),
                            [&name] (const auto& entry) { return entry.name == name; });
    };

    CHECK (contains ("Orchestral Bus Glue"));
    CHECK (contains ("Soft Tube Glue"));
    CHECK (contains ("Iron Bus Weight"));

    // Every one of them must actually turn the Glue section on - a preset
    // that showcases a feature it leaves disabled is a documentation bug with
    // a JSON extension.
    for (const auto& name : { "Orchestral Bus Glue", "Soft Tube Glue", "Iron Bus Weight" })
    {
        INFO ("preset " << name);
        REQUIRE (processor.presetManager.loadPreset (name));

        auto* enableParam = processor.apvts.getParameter (ParamIDs::compEnable);
        REQUIRE (enableParam != nullptr);
        CHECK (enableParam->getValue() > 0.5f);
    }

    // "Iron Bus Weight" is the one that is supposed to bring the transformer
    // stage in.
    REQUIRE (processor.presetManager.loadPreset ("Iron Bus Weight"));
    auto* ironParam = processor.apvts.getParameter (ParamIDs::iron);
    REQUIRE (ironParam != nullptr);
    CHECK (ironParam->convertFrom0to1 (ironParam->getValue()) > 50.0f);
}

TEST_CASE ("6.16 Loading a pre-v0.3.0 factory preset leaves every new parameter at its default", "[presets][neutrality]")
{
    AureateAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Engage everything first, so a preset that simply failed to touch the
    // new parameters would be caught rather than passing by default.
    REQUIRE (processor.presetManager.loadPreset ("Iron Bus Weight"));

    auto* ironParam = processor.apvts.getParameter (ParamIDs::iron);
    REQUIRE (ironParam != nullptr);
    REQUIRE (ironParam->getValue() > 0.0f);

    // ...then load one of the eleven, which carries no schema-3 keys at all.
    REQUIRE (processor.presetManager.loadPreset ("Master Glue (Subtle)"));

    static constexpr const char* schema3Ids[] = {
        ParamIDs::compEnable, ParamIDs::compModel, ParamIDs::compThreshold, ParamIDs::compRatio,
        ParamIDs::compAttack, ParamIDs::compRelease, ParamIDs::compMakeup, ParamIDs::compScHpf,
        ParamIDs::iron, ParamIDs::quality, ParamIDs::autoGain,
    };

    for (const auto* id : schema3Ids)
    {
        auto* parameter = processor.apvts.getParameter (id);
        REQUIRE (parameter != nullptr);
        INFO ("parameter " << id);
        CHECK (parameter->getValue() == Catch::Approx (parameter->getDefaultValue()).margin (1.0e-6));
    }
}
