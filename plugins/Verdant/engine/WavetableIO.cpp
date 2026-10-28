#include "WavetableIO.h"

namespace verdant
{

WavImportResult importWavetableWav (const uint8_t*, size_t, const std::string&)
{
    WavImportResult r;
    r.error = "Wavetable import is not implemented yet";
    return r;
}

std::vector<uint8_t> exportWavetableWav (const Wavetable&)
{
    return {};
}

} // namespace verdant
