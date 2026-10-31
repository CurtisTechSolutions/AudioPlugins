#include "PluginEditor.h"

namespace voxnote
{

Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "VoxNote", "Hum or sing in, MIDI out", juce::Colour (0xff34c759), 560, 1),
      processor (p),
      readout (p.detectedHz),
      meter (p.inputLevel)
{
    addPresetBar (p.programs);
    setHeaderComponent (&readout, 200, 46);

    auto& det = addSection ("Detection", { ids::tracking, ids::gate, ids::sensitivity, ids::onset, ids::release });
    det.setTrailingComponent (&meter, 14);
    addSection ("Notes",      { ids::retrigger, ids::transpose, ids::snap, ids::key, ids::scale });
    addSection ("Pitch Bend", { ids::bend, ids::bendRange });
    addSection ("Velocity",   { ids::velDynamic, ids::velFixed, ids::velMin, ids::velMax });
    addSection ("Output",     { ids::channel, ids::audioThru, ids::monitor, ids::monitorLevel });

    finishLayout();
}

} // namespace voxnote
