#pragma once

// JUCE-free WAV reader / writer for wavetables (CONTRACT §5).
//
// Reader: RIFF/WAVE with `fmt `, `data` and the optional `clm ` chunk that
// wavetable synths write ("<!>2048 01000000 wavetable (...)": the integer after
// "<!>" is the single-cycle frame size). Unknown chunks are skipped, odd chunk
// sizes are padded, malformed files are rejected with a readable `error`.
// Formats: PCM 8 (unsigned) / 16 / 24 / 32-bit, IEEE float 32 / 64, and
// WAVE_FORMAT_EXTENSIBLE (the sub-format GUID's first two bytes give the
// format). Multi-channel files use the first channel.
//
// Frames: frame size = clm value if present, else 2048. Each frame of another
// size is resampled to kFrameSize spectrally (forward transform at the source
// size, harmonics above 1023 / the source Nyquist dropped, inverse at 2048),
// which keeps harmonic k at harmonic k. Frame count = floor (samples /
// frameSize); a file shorter than one frame becomes a single frame (the whole
// file is taken as one cycle); more than kMaxFrames frames are thinned to
// kMaxFrames evenly spaced ones.
//
// Runs off the audio thread (allocates).

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
    std::shared_ptr<Wavetable> table;   // null on error
    int frameSize = 2048;               // the file's frame size (before resampling to kFrameSize)
    bool hadClmChunk = false;           // a parsable `clm ` chunk set the frame size
    std::string error;                  // empty on success
};

/** PCM 8/16/24/32 and float 32/64, mono or first channel; a `clm ` chunk "<!>2048 ..." sets the frame size (else 2048);
    frame sizes != 2048 are resampled to 2048 per frame; frame count clamped to kMaxFrames; a partial last frame is dropped. */
WavImportResult importWavetableWav (const uint8_t* bytes, size_t numBytes, const std::string& name);

/** 32-bit float mono WAV (44.1 kHz header rate), frames back to back, with a
    `clm ` chunk "<!>2048 01000000 wavetable (verdant)". importWavetableWav
    reproduces the frames bit-exactly. */
std::vector<uint8_t> exportWavetableWav (const Wavetable&);

} // namespace verdant
