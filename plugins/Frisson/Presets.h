#pragma once

#include <ap_plug/Programs.h>

namespace frisson
{

/** The factory bank (host program list). Order is frozen: new presets go at the end. */
std::vector<ap::plug::FactoryPreset> factoryPresets();

} // namespace frisson
