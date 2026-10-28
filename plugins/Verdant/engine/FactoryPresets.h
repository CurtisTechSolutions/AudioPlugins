#pragma once

// The factory library (built once, in code). It is listed first by the preset
// browser and exposed to hosts as the program list, so its order is fixed:
// preset 0 "Verdant Keys" (the default sound) and preset 1 "Glass Piano", then
// the categories in factoryCategories() order. New presets go at the end of
// their category file only when the bank is re-released; never reorder.
//
// Every preset: a unique name of at most 24 characters, one of the nine
// categories, 2-4 tags from factoryTags(), author "Curtis Tech Solutions",
// loudness-matched (about -26 dBFS RMS on its preview note, see
// factoryPreviewNote) and unclipped. Most name and route Macros 1-4 so the
// sound is playable from the macros alone. "Init" is initPatch() and is not in
// the bank.
//
// The recipes live in FactoryPresets_<Category>.cpp (helpers in the private
// FactoryPresets_Recipes.h); this file's .cpp assembles them.
//
// Owner: W-F.

#include "Patch.h"

#include <string>
#include <vector>

namespace verdant
{

const std::vector<Patch>& factoryPresets();

/** The nine factory categories in browser order: Bass, Lead, Pad, Pluck, Keys, FX, Texture, Sequence,
    Drums/Perc. Addition. ("Drums/Perc" contains a '/': file-system users must make it a legal name.) */
const std::vector<std::string>& factoryCategories();

/** The controlled tag vocabulary every factory preset draws its 2-4 tags from (lower case). Addition. */
const std::vector<std::string>& factoryTags();

/** The MIDI note a preset of this category is previewed and loudness-matched on: C2 (36) for Bass,
    C3 (48) for everything else. Addition. */
int factoryPreviewNote (const std::string& category);

} // namespace verdant
