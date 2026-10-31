# <plugin>_render: headless console apps that render the editors built on
# ap::ui::EditorBase to PNG (1x and 2x) while a demo signal plays, so the
# design can be reviewed without a DAW or a display:
#
#   VoxNote_render <outDir>   ->  <outDir>/voxnote@1x.png, voxnote@2x.png
#
# Each one compiles its plugin's own top-level sources (not the VST3 target).
# Not part of the release zips. (Verdant has its own, plugins/Verdant/render.)

function(ap_add_editor_render plugin)
    file(GLOB plugin_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/plugins/${plugin}/*.cpp)

    juce_add_console_app(${plugin}_render PRODUCT_NAME "${plugin}_render")
    target_sources(${plugin}_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/RenderEditor.cpp ${plugin_sources})
    target_include_directories(${plugin}_render PRIVATE ${CMAKE_SOURCE_DIR}/plugins/${plugin})

    # Only the plugin's own files need its name; keeping it off the JUCE module sources
    # leaves their compile commands identical across the five tools (compiler-cache hits).
    set_source_files_properties(${plugin_sources} PROPERTIES COMPILE_DEFINITIONS "JucePlugin_Name=\"${plugin}\"")

    target_compile_definitions(${plugin}_render PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

    target_link_libraries(${plugin}_render PRIVATE
        ap_core
        ap_common
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    foreach(extra IN LISTS ARGN)
        target_link_libraries(${plugin}_render PRIVATE ${extra})
    endforeach()
endfunction()

ap_add_editor_render(Strata)
ap_add_editor_render(VoxNote)
ap_add_editor_render(Intone)
ap_add_editor_render(Ducker)
ap_add_editor_render(Frisson frisson_modules)
