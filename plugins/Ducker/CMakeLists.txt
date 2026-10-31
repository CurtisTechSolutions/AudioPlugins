ap_add_plugin(Ducker
    PRODUCT_NAME    "Ducker"
    PLUGIN_CODE     Duck
    DESCRIPTION     "Sidechain compressor and tempo-synced ghost ducker"
    VST3_CATEGORIES Fx Dynamics
    SOURCES
        PluginProcessor.cpp
        Presets.cpp
        PluginEditor.cpp)
