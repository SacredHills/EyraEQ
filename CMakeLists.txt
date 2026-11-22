cmake_minimum_required(VERSION 3.22)

project(EyraEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

option(EYRA_BUILD_TESTS "Build the EyraEQ test runner" ON)
option(EYRA_COPY_PLUGIN_AFTER_BUILD "Copy the VST3 to the user plug-in folder after building" OFF)

# ---------------------------------------------------------------------------
# JUCE
#   By default JUCE is fetched from GitHub at a pinned tag. To build offline or
#   against a local checkout, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
set(EYRA_JUCE_TAG "9.0.6" CACHE STRING "JUCE git tag")
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${EYRA_JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Factory presets (embedded)
# ---------------------------------------------------------------------------
file(GLOB EYRA_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryPresets/*.xml")
juce_add_binary_data(EyraEQ_BinaryData
    HEADER_NAME EyraBinaryData.h
    NAMESPACE EyraBinaryData
    SOURCES ${EYRA_FACTORY_PRESETS})

# ---------------------------------------------------------------------------
# Sources shared by the plug-in and the test runner
# ---------------------------------------------------------------------------
set(EYRA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/FilterDesign.cpp
    Source/DSP/EQBand.cpp
    Source/DSP/EQEngine.cpp
    Source/Analysis/SpectrumAnalyzer.cpp
    Source/Parameters/Parameters.cpp
    Source/State/PresetManager.cpp
    Source/State/EditHistory.cpp
    Source/State/ABComparison.cpp
    Source/UI/EyraLookAndFeel.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/BandControlPanel.cpp
    Source/UI/TopBar.cpp
)

set(EYRA_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra)

set(EYRA_COMPILE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# ---------------------------------------------------------------------------
# Plug-in
# ---------------------------------------------------------------------------
juce_add_plugin(EyraEQ
    COMPANY_NAME "SacredHills"
    COMPANY_WEBSITE "https://github.com/SacredHills/EyraEQ"
    BUNDLE_ID "com.sacredhills.eyraeq"
    PLUGIN_MANUFACTURER_CODE Shil
    PLUGIN_CODE Eyeq
    PRODUCT_NAME "EyraEQ"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD ${EYRA_COPY_PLUGIN_AFTER_BUILD})

target_sources(EyraEQ PRIVATE ${EYRA_SOURCES})
target_include_directories(EyraEQ PRIVATE Source)
target_compile_definitions(EyraEQ PUBLIC ${EYRA_COMPILE_DEFINITIONS})
target_link_libraries(EyraEQ
    PRIVATE
        EyraEQ_BinaryData
        ${EYRA_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(EYRA_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(EyraEQTests PRODUCT_NAME "EyraEQTests")
    target_sources(EyraEQTests PRIVATE
        ${EYRA_SOURCES}
        Tests/TestMain.cpp
        Tests/FilterDesignTests.cpp
        Tests/EQEngineTests.cpp
        Tests/ProcessorTests.cpp
        Tests/AnalyzerTests.cpp
        Tests/RealtimeTests.cpp)
    target_include_directories(EyraEQTests PRIVATE Source)
    target_compile_definitions(EyraEQTests PRIVATE
        ${EYRA_COMPILE_DEFINITIONS}
        JucePlugin_Name="EyraEQ"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EyraEQTests PRIVATE
        EyraEQ_BinaryData
        ${EYRA_JUCE_MODULES}
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME EyraEQTests COMMAND EyraEQTests)

    # Loads the built VST3 through JUCE's VST3 host, as a DAW would.
    juce_add_console_app(EyraEQHostTest PRODUCT_NAME "EyraEQHostTest")
    target_sources(EyraEQHostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(EyraEQHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(EyraEQHostTest PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_dependencies(EyraEQHostTest EyraEQ_VST3)
    add_test(NAME EyraEQHostTest
        COMMAND EyraEQHostTest "$<TARGET_FILE_DIR:EyraEQ_VST3>/../..")
endif()
