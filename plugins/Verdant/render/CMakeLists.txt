# verdant_render: headless console app that renders every editor page to PNG
# at 1x and 2x, so the design can be reviewed without a DAW:
#
#   verdant_render <outDir>      ->  <outDir>/<page>@1x.png, <page>@2x.png
#
# It compiles the plugin's own sources directly (not the VST3 target) and is
# not part of the release zip.

juce_add_console_app(verdant_render PRODUCT_NAME "verdant_render")

target_sources(verdant_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderPages.cpp
    ${VERDANT_PLUGIN_SOURCES})

target_include_directories(verdant_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(verdant_render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(verdant_render PRIVATE
    verdant_engine
    ap_core
    ap_common
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
