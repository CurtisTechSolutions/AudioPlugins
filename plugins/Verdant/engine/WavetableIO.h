#pragma once

// JUCE-free WAV reader / writer for wavetables.
//
// Owner: W-B. FOUNDATION STUB: import reports "not implemented", export
// returns an empty vector.

#include "Wavetable.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace verdant
{

struct WavImportResult
{
    std::shared_ptr<Wavetable> table;
    int frameSize = 2048;
    bool hadClmChunk = false;
    std::string error;            // empty on success
};

/** PCM 8/16/24/32 and float 32/64, mono or first channel; a `clm ` chunk "<!>2048 ..." sets the frame size (else 2048);
    frame sizes != 2048 are resampled to 2048 per frame; frame count clamped to kMaxFrames; a partial last frame is dropped. */
WavImportResult importWavetableWav (const uint8_t* bytes, size_t numBytes, const std::string& name);

/** 32-bit float WAV with a clm chunk. */
std::vector<uint8_t> exportWavetableWav (const Wavetable&);

} // namespace verdant
