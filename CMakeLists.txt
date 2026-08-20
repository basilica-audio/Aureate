cmake_minimum_required(VERSION 3.24)

# Must be set before the first project() call to take effect.
# CI overrides CMAKE_OSX_ARCHITECTURES explicitly for release/universal builds;
# local builds are left at the native architecture.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target" FORCE)

# JUCE's build tooling (juceaide) and some CMake find-modules it triggers
# (e.g. FindThreads) expect the C toolchain to be available; declaring it
# here at the top level avoids it being enabled implicitly deeper inside
# JUCE's CMake helpers, which has been observed to break Ninja's generate
# step (missing CMAKE_C_COMPILE_OBJECT rule variable).
project(Aureate VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)

# CPMAddPackage automatically respects the CPM_SOURCE_CACHE environment
# variable if it is set, reusing a shared package cache across builds/checkouts.
if(DEFINED ENV{CPM_SOURCE_CACHE})
    message(STATUS "Aureate: using CPM_SOURCE_CACHE at $ENV{CPM_SOURCE_CACHE}")
endif()

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)

# ==============================================================================
# Plugin target
# ==============================================================================

juce_add_plugin(Aureate
    COMPANY_NAME "Yves Vogl"
    BUNDLE_ID com.yvesvogl.aureate
    PLUGIN_MANUFACTURER_CODE Yvsv
    PLUGIN_CODE Aurt
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Aureate"
    ICON_BIG "docs/assets/icon.png"
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

# ==============================================================================
# Embedded assets: M2 preset system factory presets (presets/factory/*.json)
# and the M2 i18n frame's German translation (resources/i18n/de.txt). See
# src/presets/PresetManager.h and src/presets/Localisation.h - both consume
# these purely via BinaryData:: symbols passed in from PluginProcessor.cpp/
# PluginEditor.cpp, never by including BinaryData.h themselves, so they stay
# portable to sibling plugins (see sibling basilica-audio/nave's
# docs/preset-system-notes.md, the pilot this was copied from).
# ==============================================================================

juce_add_binary_data(AureateBinaryData SOURCES
    presets/factory/default.json
    presets/factory/stringSectionGlue.json
    presets/factory/brassBloom.json
    presets/factory/choirWarmth.json
    presets/factory/orchestralSubmixCohesion.json
    presets/factory/masterGlueSubtle.json
    presets/factory/vintageTapePad.json
    presets/factory/valvePush.json
    presets/factory/parallelGritNewYork.json
    presets/factory/consoleSummingSheen.json
    presets/factory/airAndWeight.json
    presets/factory/orchestralBusGlue.json
    presets/factory/softTubeGlue.json
    presets/factory/ironBusWeight.json
    resources/i18n/de.txt
    # M3 photoreal GUI ("tubecomp" faceplate design, the pilot for the
    # requiem/tenebrae/apotheosis wave) - copied verbatim from
    # brand/mocks/tubecomp/ (repo-relative to the suite root, one level
    # above this repo; see docs/gui-mapping.md for the full provenance and
    # parameter-mapping table). Exactly one baked master faceplate plus its
    # small set of live-overlay source assets:
    #   - master_tubecomp.png: the SOLE faceplate baseline (copied from
    #     master-03-clean-base.png) - steel plate, wooden cheeks, handle,
    #     empty VU dial, all 10 knobs at 12 o'clock, all 4 toggles UP, tube
    #     vents at full glow, 3 blank brass nameplates.
    #   - needle_tubecomp.png: the master-extracted, pivot-centred VU-needle
    #     sprite (components/needle.png) - rotated live by src/gui/HubNeedle.
    #   - toggle_{1..4}_down.png: per-toggle pre-cropped "lever down" pose
    #     (components/toggle-N-down.png) - blitted over the baseline by
    #     src/gui/ToggleZoneSwap when that toggle's own parameter is away
    #     from its default (see ToggleZoneSwap.h's UP==default convention).
    #   - vent_glow_tubecomp.png: the tube-vent glow diff layer
    #     (components/vent-glow.png) - src/gui/SubtractiveGlow builds a
    #     "fully dim" frame from this once at construction and cross-blends
    #     it against the baseline live (see that class's own runtime-model
    #     docs).
    resources/gui/master_tubecomp.png
    resources/gui/needle_tubecomp.png
    resources/gui/toggle_1_down.png
    resources/gui/toggle_2_down.png
    resources/gui/toggle_3_down.png
    resources/gui/toggle_4_down.png
    resources/gui/vent_glow_tubecomp.png
)

# ==============================================================================
# SharedCode: an INTERFACE target carrying the plugin's sources, definitions,
# and library links so that both the plugin targets (AU/VST3/Standalone) and
# the Tests executable compile the exact same code (Pamplejuce pattern, see
# https://github.com/sudara/pamplejuce), avoiding ODR violations between them.
# ==============================================================================

add_library(SharedCode INTERFACE)

target_compile_features(SharedCode INTERFACE cxx_std_20)

file(GLOB_RECURSE AureateSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

target_sources(SharedCode INTERFACE ${AureateSources})

target_include_directories(SharedCode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(SharedCode INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # AGPLv3 usage permits disabling the splash screen.
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Add any extra juce:: modules a specific plugin needs (e.g. juce_audio_formats
# for reading impulse-response files) to this list.
target_link_libraries(SharedCode INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    AureateBinaryData
)

target_link_libraries(Aureate PRIVATE SharedCode)

# ==============================================================================
# Tests
# ==============================================================================

enable_testing()
set(CTEST_OUTPUT_ON_FAILURE ON)

CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.15.2
)

file(GLOB_RECURSE AureateTestSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")

add_executable(Tests ${AureateTestSources})

target_compile_features(Tests PRIVATE cxx_std_20)

target_link_libraries(Tests PRIVATE SharedCode Catch2::Catch2)

# Copy over the plugin target's compile definitions (JucePlugin_* macros etc.)
# so plugin code compiles identically when built as part of the Tests binary.
target_compile_definitions(Tests PRIVATE
    $<TARGET_PROPERTY:Aureate,COMPILE_DEFINITIONS>
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Absolute path to tests/data, so binary test fixtures (currently the
    # v0.2.1 reference render used by the v0.3.0 neutrality null, brief
    # section 6 test 6.1c) can be located from the build directory without
    # copying them around or depending on the working directory.
    AUREATE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    # ...and to presets/factory, so the SHA-256 pinning test reads the same
    # files juce_add_binary_data() embedded rather than a copy of them.
    AUREATE_PRESET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/factory"
    # Absolute path to docs/, so tests/gui/EditorSnapshotTests.cpp can write
    # the committed docs/gui-preview.png directly rather than a copy step.
    AUREATE_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
)

set_target_properties(Tests PROPERTIES XCODE_GENERATE_SCHEME ON)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(AureateDiscoveryMode PRE_TEST)
else()
    set(AureateDiscoveryMode POST_BUILD)
endif()

catch_discover_tests(Tests DISCOVERY_MODE ${AureateDiscoveryMode})
