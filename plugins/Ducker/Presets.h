#pragma once

#include <ap_plug/Programs.h>

namespace ducker
{

/** The factory bank (host program list). Order is frozen: new presets go at the end. */
std::vector<ap::plug::FactoryPreset> factoryPresets();

} // namespace ducker
