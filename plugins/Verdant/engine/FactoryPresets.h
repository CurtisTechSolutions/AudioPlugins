#pragma once

// The factory bank (built in code, listed first by the preset browser and
// exposed to hosts as the program list). Categories: Bass, Lead, Pad, Pluck,
// Keys, FX, Texture (+ Init); each preset renders non-silent and unclipped
// (peak < 1) at velocity 100.
//
// Owner: W-F. FOUNDATION STUB: "Init", "Soft Pad", "Saw Lead".

#include "Patch.h"

#include <vector>

namespace verdant
{

const std::vector<Patch>& factoryPresets();

} // namespace verdant
