#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>
#include <ap_ui/Meters.h>

namespace voxnote
{

class Editor : public ap::ui::EditorBase
{
public:
    explicit Editor (Processor&);
    ~Editor() override = default;

private:
    Processor& processor;
    ap::ui::PitchReadout readout;
    ap::ui::LevelMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace voxnote
