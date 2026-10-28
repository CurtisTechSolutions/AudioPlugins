#pragma once

// The FX rack: 8 reorderable slots (fx{n}_type), run in order 1..8; an effect
// type appearing in more than one slot is processed only in its first slot.
// The individual effect classes (Distortion, Chorus, Phaser, Flanger,
// StereoDelay, Reverb, ThreeBandEq, Compressor) are public too, each with
// prepare / set / process, so tests can target them directly (added by W-G).
//
// Owner: W-G. FOUNDATION STUB: the rack passes audio through, tail 0 s.

#include "Common.h"
#include "Params.h"

#include <cstdint>

namespace verdant
{

enum class FxType : uint8_t { None, Distortion, Chorus, Phaser, Flanger, Delay, Reverb, Eq, Compressor, Count };

class FxRack
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const ParamValues& modulated, const Transport&);   // per block
    void process (float* L, float* R, int numSamples);
    float tailSeconds() const;

private:
    double sr = 44100.0;
    int maxBlockSize = 0;
};

} // namespace verdant
