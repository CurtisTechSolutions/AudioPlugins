# VST3HostTest: a tiny JUCE host that loads each built .vst3, runs audio and
# MIDI through it and checks the behaviour end to end (through the real VST3
# wrapper). Registered with ctest, one test per plugin. Uses the headless
# hosting module so it needs no display.

juce_add_console_app(VST3HostTest PRODUCT_NAME "VST3HostTest")

target_sources(VST3HostTest PRIVATE VST3HostTest.cpp)

target_compile_definitions(VST3HostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(VST3HostTest PRIVATE
    ap_core
    juce::juce_audio_processors_headless
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

set(ap_host_plugins Strata VoxNote Intone Ducker Frisson)
foreach(plugin IN LISTS ap_host_plugins)
    add_test(NAME host_${plugin}
             COMMAND VST3HostTest "$<GENEX_EVAL:$<TARGET_PROPERTY:${plugin}_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>" ${plugin})
    set_tests_properties(host_${plugin} PROPERTIES TIMEOUT 300)
endforeach()
