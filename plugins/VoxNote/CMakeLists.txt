ap_add_plugin(VoxNote
    PRODUCT_NAME    "VoxNote"
    PLUGIN_CODE     Vxnt
    DESCRIPTION     "Sing or hum into the mic, get MIDI out"
    NEEDS_MIDI_OUTPUT
    VST3_CATEGORIES Fx Tools
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        MidiPortOut.cpp)

# MidiPortOut sends straight to a system MIDI port (loopMIDI / IAC / ALSA) for
# hosts that do not route MIDI out of an audio effect.
target_link_libraries(VoxNote PRIVATE juce::juce_audio_devices)
