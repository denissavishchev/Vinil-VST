cmake_minimum_required(VERSION 3.22)
project(DevisVinil VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

add_subdirectory(JUCE)

juce_add_plugin(DevisVinil
    COMPANY_NAME "Devis"
    PLUGIN_MANUFACTURER_CODE Devs
    PLUGIN_CODE Dvnl
    FORMATS VST3 Standalone
    PRODUCT_NAME "Devis Vinil"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DevisVinil PRIVATE
    Source/Curve.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(DevisVinil PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DevisVinil PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# offline DSP test: cmake -B build-test -DVINIL_BUILD_TESTS=ON -DCMAKE_OSX_ARCHITECTURES=arm64
option(VINIL_BUILD_TESTS "Build the offline DSP test" OFF)
if(VINIL_BUILD_TESTS)
    juce_add_console_app(VinilTest PRODUCT_NAME "VinilTest")
    target_sources(VinilTest PRIVATE
        Tests/DspTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(VinilTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Devis Vinil")
    target_link_libraries(VinilTest PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
