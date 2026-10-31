ap_add_plugin(Intone
    PRODUCT_NAME    "Intone"
    PLUGIN_CODE     Intn
    DESCRIPTION     "Pitch correction and vocal enhancement"
    VST3_CATEGORIES Fx Pitch\ Shift
    SOURCES
        PluginProcessor.cpp
        Presets.cpp
        PluginEditor.cpp)
