#pragma once

// The factory bank (built in code, listed first by the preset browser and
// exposed to hosts as the program list, so the order is frozen: new presets go
// at the end). Categories: Bass, Lead, Pad, Pluck, Keys, FX, Texture; 2-4 tags
// each; author "Curtis Tech Solutions". Preset 0 ("Verdant Keys") is the
// default sound; "Init" is initPatch() and is not in the bank. Each preset
// renders non-silent and unclipped (peak < 1) at velocity 100 on C3.
//
// Owner: W-F.

#include "Patch.h"

#include <vector>

namespace verdant
{

const std::vector<Patch>& factoryPresets();

/** The seven factory categories in browser order. Addition. */
const std::vector<std::string>& factoryCategories();

} // namespace verdant
