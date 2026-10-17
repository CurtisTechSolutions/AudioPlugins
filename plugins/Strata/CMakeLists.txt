ap_add_plugin(Strata
    PRODUCT_NAME    "Strata"
    PLUGIN_CODE     Strt
    DESCRIPTION     "Polyphonic subtractive synthesizer"
    IS_SYNTH
    NEEDS_MIDI_INPUT
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp)
