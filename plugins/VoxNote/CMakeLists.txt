ap_add_plugin(VoxNote
    PRODUCT_NAME    "VoxNote"
    PLUGIN_CODE     Vxnt
    DESCRIPTION     "Sing or hum into the mic, get MIDI out"
    NEEDS_MIDI_OUTPUT
    VST3_CATEGORIES Fx Tools
    SOURCES
        PluginProcessor.cpp
        Presets.cpp
        PluginEditor.cpp)
