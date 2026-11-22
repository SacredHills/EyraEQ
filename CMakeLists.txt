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
option(EYRA_BUNDLE_INSTRUMENT_RECOGNITION "Bundle ONNX Runtime and the instrument model with the plug-in" ON)
set(EYRA_ONNXRUNTIME_VERSION "1.32.0" CACHE STRING "ONNX Runtime release to bundle")
set(EYRA_ONNXRUNTIME_LIBRARY "" CACHE FILEPATH "ONNX Runtime shared library to bundle (downloaded from the official release when empty)")
option(EYRA_REQUIRE_ONNXRUNTIME "Fail the configure step if ONNX Runtime cannot be bundled (CI)" OFF)

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
    Source/Analysis/AudioAnalysis.cpp
    Source/Analysis/SpectralTools.cpp
    Source/Analysis/TransientAnalyzer.cpp
    Source/Analysis/PitchEstimator.cpp
    Source/Analysis/TonalBalanceAnalyzer.cpp
    Source/Analysis/ResonanceDetector.cpp
    Source/Analysis/AnalysisController.cpp
    Source/Parameters/Parameters.cpp
    Source/State/PresetManager.cpp
    Source/State/EditHistory.cpp
    Source/State/ABComparison.cpp
    Source/UI/EyraLookAndFeel.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/BandControlPanel.cpp
    Source/UI/TopBar.cpp
    Source/UI/AnalysisPanel.cpp
    Source/Intelligence/OnnxRuntime.cpp
    Source/Intelligence/FeatureExtractor.cpp
    Source/Intelligence/ModelManager.cpp
    Source/Intelligence/InstrumentClassifier.cpp
)

set(EYRA_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_cryptography)

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
target_include_directories(EyraEQ PRIVATE Source ThirdParty/onnxruntime/include)
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
# Instrument recognition: ONNX Runtime (loaded at run time, never linked) and the model files.
# Both are copied into the VST3 bundle and next to the standalone app.
# ---------------------------------------------------------------------------
if(EYRA_BUNDLE_INSTRUMENT_RECOGNITION AND NOT EYRA_ONNXRUNTIME_LIBRARY)
    # Microsoft's official ONNX Runtime build, taken from its PyPI wheel (which contains the C API
    # library), verified against the SHA-256 published by PyPI. macOS: arm64 only (Microsoft ships no
    # Intel build of 1.32); on Intel Macs instrument recognition reports itself as unavailable.
    set(_ort_v ${EYRA_ONNXRUNTIME_VERSION})
    if(WIN32)
        set(_ort_tag win_amd64)
        set(_ort_member onnxruntime/capi/onnxruntime.dll)
    elseif(APPLE)
        set(_ort_tag macosx_14_0_arm64)
        set(_ort_member onnxruntime/capi/libonnxruntime.${_ort_v}.dylib)
    else()
        set(_ort_tag manylinux_2_28_x86_64)
        set(_ort_member onnxruntime/capi/libonnxruntime.so.${_ort_v})
    endif()
    set(_ort_dir ${CMAKE_BINARY_DIR}/_deps/onnxruntime-${_ort_v})
    set(_ort_lib ${_ort_dir}/wheel/${_ort_member})

    if(NOT EXISTS ${_ort_lib})
        file(DOWNLOAD https://pypi.org/pypi/onnxruntime/${_ort_v}/json ${_ort_dir}/pypi.json STATUS _ort_status TLS_VERIFY ON)
        list(GET _ort_status 0 _ort_code)
        set(_ort_url "")
        if(_ort_code EQUAL 0)
            file(READ ${_ort_dir}/pypi.json _ort_json)
            string(JSON _ort_count LENGTH "${_ort_json}" urls)
            math(EXPR _ort_last "${_ort_count} - 1")
            foreach(_i RANGE ${_ort_last})
                string(JSON _ort_file GET "${_ort_json}" urls ${_i} filename)
                if(_ort_file MATCHES "-cp312-cp312-${_ort_tag}\\.whl$")
                    string(JSON _ort_url GET "${_ort_json}" urls ${_i} url)
                    string(JSON _ort_sha GET "${_ort_json}" urls ${_i} digests sha256)
                    set(_ort_wheel ${_ort_file})
                endif()
            endforeach()
        endif()

        if(_ort_url)
            message(STATUS "Downloading ONNX Runtime ${_ort_v} (${_ort_wheel})")
            file(DOWNLOAD ${_ort_url} ${_ort_dir}/${_ort_wheel} EXPECTED_HASH SHA256=${_ort_sha} STATUS _ort_status TLS_VERIFY ON)
            list(GET _ort_status 0 _ort_code)
            if(_ort_code EQUAL 0)
                file(ARCHIVE_EXTRACT INPUT ${_ort_dir}/${_ort_wheel} DESTINATION ${_ort_dir}/wheel PATTERNS ${_ort_member})
            endif()
        endif()
    endif()

    if(EXISTS ${_ort_lib})
        set(EYRA_ONNXRUNTIME_LIBRARY ${_ort_lib} CACHE FILEPATH "" FORCE)
    else()
        message(WARNING "Could not obtain ONNX Runtime ${_ort_v}. The plug-in will build without instrument recognition. Set EYRA_ONNXRUNTIME_LIBRARY to bundle a local copy.")
    endif()
endif()

if(EYRA_REQUIRE_ONNXRUNTIME AND NOT EYRA_ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "ONNX Runtime is required (EYRA_REQUIRE_ONNXRUNTIME) but could not be obtained.")
endif()

if(EYRA_BUNDLE_INSTRUMENT_RECOGNITION)
    set(_eyra_bin_vst3 "$<TARGET_FILE_DIR:EyraEQ_VST3>")
    set(_eyra_bin_app "$<TARGET_FILE_DIR:EyraEQ_Standalone>")
    if(APPLE)
        set(_eyra_res_app "${_eyra_bin_app}/../Resources")
    else()
        set(_eyra_res_app "${_eyra_bin_app}")
    endif()

    foreach(_pair "EyraEQ_VST3|${_eyra_bin_vst3}|${_eyra_bin_vst3}/../Resources" "EyraEQ_Standalone|${_eyra_bin_app}|${_eyra_res_app}")
        string(REPLACE "|" ";" _parts "${_pair}")
        list(GET _parts 0 _target)
        list(GET _parts 1 _bin)
        list(GET _parts 2 _res)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_res}/Models"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Models" "${_res}/Models"
            VERBATIM)
        if(EYRA_ONNXRUNTIME_LIBRARY)
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EYRA_ONNXRUNTIME_LIBRARY}" "${_bin}/"
                VERBATIM)
        endif()
    endforeach()
endif()

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
        Tests/RealtimeTests.cpp
        Tests/AnalysisTests.cpp
        Tests/IntelligenceTests.cpp)
    target_include_directories(EyraEQTests PRIVATE Source ThirdParty/onnxruntime/include)
    target_compile_definitions(EyraEQTests PRIVATE
        ${EYRA_COMPILE_DEFINITIONS}
        JucePlugin_Name="EyraEQ"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        EYRA_TEST_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Models")
    target_link_libraries(EyraEQTests PRIVATE
        EyraEQ_BinaryData
        ${EYRA_JUCE_MODULES}
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME EyraEQTests COMMAND EyraEQTests)
    set(_eyra_test_env "EYRA_MODEL_DIR=${CMAKE_CURRENT_SOURCE_DIR}/Models")
    if(EYRA_ONNXRUNTIME_LIBRARY AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Models/yamnet/manifest.json")
        # Runtime and model are present: the instrument-recognition tests must really run.
        list(APPEND _eyra_test_env "EYRA_ONNXRUNTIME_LIBRARY=${EYRA_ONNXRUNTIME_LIBRARY}" "EYRA_REQUIRE_INSTRUMENT_RECOGNITION=1")
    endif()
    set_tests_properties(EyraEQTests PROPERTIES ENVIRONMENT "${_eyra_test_env}")

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
