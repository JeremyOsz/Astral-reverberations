cmake_minimum_required(VERSION 3.22)

project(AstralReverberations
    VERSION 0.1.0
    DESCRIPTION "A modulated astro-inspired space reverb and tape delay"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASTRAL_BUILD_PLUGIN "Build the JUCE VST3/standalone plugin targets" ON)
option(ASTRAL_BUILD_TESTS "Build the portable unit test executable" ON)

add_library(AstralCore STATIC
    Source/astro/AstrologyEngine.cpp
    Source/astro/HistoricalPresets.cpp
    Source/astro/AstroMapper.cpp
    Source/dsp/AstralReverbDelay.cpp
    Source/dsp/PlanetaryDrone.cpp)

target_include_directories(AstralCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_features(AstralCore PUBLIC cxx_std_20)

if (ASTRAL_BUILD_TESTS)
    enable_testing()

    add_executable(AstralCoreTests
        Source/plugin/MacroMapping.cpp
        tests/AstroTests.cpp
        tests/DspTests.cpp
        tests/MacroMappingTests.cpp
        tests/TestMain.cpp)

    target_link_libraries(AstralCoreTests PRIVATE AstralCore)

    add_test(NAME AstralCoreTests COMMAND AstralCoreTests)
endif()

if (ASTRAL_BUILD_PLUGIN)
    include(FetchContent)

    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)

    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(AstralReverberationsAssets
        SOURCES
            assets/astral-orbit-texture.png)

    juce_add_plugin(AstralReverberationsPlugin
        COMPANY_NAME "Astral Reverberations"
        PRODUCT_NAME "Astral Reverberations"
        BUNDLE_ID com.astralreverberations.plugin
        PLUGIN_MANUFACTURER_CODE Astr
        PLUGIN_CODE Arvb
        FORMATS VST3 Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Astral Reverberations needs audio input for capture, orbit tails, and live delay/reverb processing."
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(AstralReverberationsPlugin
        PRIVATE
            Source/plugin/MacroMapping.cpp
            Source/plugin/PluginProcessor.cpp
            Source/plugin/PluginEditor.cpp)

    target_link_libraries(AstralReverberationsPlugin
        PRIVATE
            AstralCore
            AstralReverberationsAssets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(AstralReverberationsPlugin
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)
endif()
