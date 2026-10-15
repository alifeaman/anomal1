cmake_minimum_required(VERSION 3.22)

project(ANOMAL01 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon (M1/M2/M3) için yerel arm64. Barındırılan pluginler de arm64 olmalı.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# Yerelde derlerken plugin'i otomatik ~/Library/Audio/Plug-Ins altına kopyalar.
# CI'da -DANOMAL_COPY_AFTER_BUILD=FALSE ile kapatılır.
if(NOT DEFINED ANOMAL_COPY_AFTER_BUILD)
    set(ANOMAL_COPY_AFTER_BUILD TRUE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ANOMAL01
    COMPANY_NAME "Anomal"
    PLUGIN_MANUFACTURER_CODE Anml
    PLUGIN_CODE An01
    FORMATS VST3 AU
    PRODUCT_NAME "ANOMAL-01"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${ANOMAL_COPY_AFTER_BUILD}
    PLUGIN_NAME "ANOMAL-01")

target_sources(ANOMAL01 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ANOMAL01 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(ANOMAL01
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
