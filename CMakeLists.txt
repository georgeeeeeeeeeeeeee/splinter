cmake_minimum_required(VERSION 3.22)

# macOS: universal binary; must be set before project(). Ignored elsewhere.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Fault VERSION 0.9.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE checkout: -DFAULT_JUCE_PATH=..., else $JUCE_PATH, else ~/JUCE (Mac) or C:/JUCE (Windows).
if(NOT FAULT_JUCE_PATH)
    if(DEFINED ENV{JUCE_PATH})
        set(FAULT_JUCE_PATH "$ENV{JUCE_PATH}")
    elseif(WIN32)
        set(FAULT_JUCE_PATH "C:/JUCE")
    else()
        set(FAULT_JUCE_PATH "$ENV{HOME}/JUCE")
    endif()
endif()
add_subdirectory("${FAULT_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
include(cmake/PluginIds.cmake)

# ---- DSP core: plain C++, no JUCE (WASM route stays open) -------------------
add_subdirectory(source/dsp)

# ---- Plugin -----------------------------------------------------------------
set(FAULT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND FAULT_FORMATS AU)
endif()
juce_add_plugin(Fault
    COMPANY_NAME            "${FAULT_COMPANY}"
    PLUGIN_MANUFACTURER_CODE ${FAULT_MANUFACTURER_CODE}
    PLUGIN_CODE             ${FAULT_PLUGIN_CODE}
    BUNDLE_ID               "${FAULT_BUNDLE_ID}"
    PRODUCT_NAME            "${FAULT_PRODUCT_NAME}"
    FORMATS                 ${FAULT_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE     # gates, CC lanes, note pitch as speed (brief §3.10)
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE    # the install scripts copy + (on Mac) ad-hoc sign
    VST3_CATEGORIES         Fx Delay
    AU_MAIN_TYPE            kAudioUnitType_MusicEffect   # an effect that takes MIDI: AU type 'aumf'
    MICROPHONE_PERMISSION_ENABLED TRUE)

# Factory presets, compiled in (brief §5 / Stage 6). presets/<Category>/*.json
file(GLOB FAULT_PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*/*.json)
juce_add_binary_data(FaultPresetData HEADER_NAME FaultPresetData.h NAMESPACE FaultPresetData SOURCES ${FAULT_PRESET_FILES})

add_library(fault_plugin_code INTERFACE)
target_sources(fault_plugin_code INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/TablesJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/Presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ui/BufferView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ui/ModDrawer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ui/SettingsDrawer.cpp)
target_include_directories(fault_plugin_code INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(fault_plugin_code INTERFACE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(fault_plugin_code INTERFACE fault_dsp FaultPresetData juce::juce_audio_utils)
# Dev builds watch tuning/macro_tables.json and hot-reload it (brief §3.6). Release builds turn this off
# and use the baked tables (fault-render --bake-tables).
option(FAULT_DEV_TABLES "Plug-in hot-reloads tuning/macro_tables.json" ON)
if(FAULT_DEV_TABLES)
    target_compile_definitions(fault_plugin_code INTERFACE FAULT_TABLES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tuning/macro_tables.json")
endif()

target_link_libraries(Fault PRIVATE fault_plugin_code PUBLIC
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- Tools + tests ----------------------------------------------------------
add_subdirectory(tools/render)
add_subdirectory(tools/presets)
add_subdirectory(tools/harden)
option(FAULT_BUILD_TESTS "Build DSP unit tests" ON)
if(FAULT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
