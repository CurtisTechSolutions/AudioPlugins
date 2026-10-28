ap_add_plugin(Humline
    PRODUCT_NAME    "Humline"
    PLUGIN_CODE     Huml
    DESCRIPTION     "Hum a melody, get a tuned synth line and MIDI out"
    IS_SYNTH
    NEEDS_MIDI_OUTPUT
    VST3_CATEGORIES Instrument Synth
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp)
