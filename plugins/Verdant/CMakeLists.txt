# Verdant: flagship wavetable synth (working title). The DSP lives in the
# JUCE-free verdant_engine library (engine/, added from the root CMakeLists);
# this folder is the JUCE wrapper: processor, presets, background jobs and the
# editor. ui/*.cpp is globbed so pages and visualisers need no CMake edits.

file(GLOB VERDANT_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)

# Shared with the verdant_render tool, which compiles the same sources.
set(VERDANT_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BackgroundTasks.cpp
    ${VERDANT_UI_SOURCES})

ap_add_plugin(Verdant
    PRODUCT_NAME    "Verdant"
    PLUGIN_CODE     Vrdt
    DESCRIPTION     "Living wavetable synthesizer"
    IS_SYNTH
    NEEDS_MIDI_INPUT
    SOURCES
        ${VERDANT_PLUGIN_SOURCES})

target_link_libraries(Verdant PRIVATE verdant_engine juce::juce_audio_formats)
target_include_directories(Verdant PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(AP_BUILD_RENDER)
    add_subdirectory(render)
endif()
