cmake_minimum_required(VERSION 3.22)
project(AutoMaster VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically on first configure (needs internet + git).
# To use a local copy instead: cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(AM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_plugin(AutoMaster
    COMPANY_NAME "TitiAudio"
    PLUGIN_MANUFACTURER_CODE Titi
    PLUGIN_CODE Amst
    FORMATS VST3 Standalone
    PRODUCT_NAME "AutoMaster"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(AutoMaster PRIVATE ${AM_SOURCES})

target_compile_definitions(AutoMaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AutoMaster
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- optional: headless DSP test (renders audio to raw float files) -------------------
option(AUTOMASTER_BUILD_TESTS "Build the headless DSP test" OFF)
if(AUTOMASTER_BUILD_TESTS)
    juce_add_console_app(AutoMasterTest PRODUCT_NAME "AutoMasterTest")
    target_sources(AutoMasterTest PRIVATE Tests/DspTest.cpp ${AM_SOURCES})
    target_compile_definitions(AutoMasterTest PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(AutoMasterTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
