#pragma once

// The FX rack: 8 reorderable slots (fx{n}_type), run in order 1..8; an effect
// type appearing in more than one slot is processed only in its first slot.
// The individual effect classes (Distortion, Chorus, Phaser, Flanger,
// StereoDelay, Reverb, ThreeBandEq, Compressor) live in FxUnits.h and are
// public too, each with prepare / set / process, so tests target them directly.
//
// Slot changes never click: a unit that enters the chain fades in from an
// exact bypass, one that leaves fades out (10 ms). A reorder that changes the
// relative order of running units first fades every unit out, swaps the order
// while the chain is a pure bypass, then fades the new chain in.
//
// Owner: W-G.

#include "Common.h"
#include "FxUnits.h"
#include "Params.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace verdant
{

enum class FxType : uint8_t { None, Distortion, Chorus, Phaser, Flanger, Delay, Reverb, Eq, Compressor, Count };

// Effect parameters from the (modulated) parameter values, plain units → unit structs.
DistortionParams distortionParamsFrom (const ParamValues&) noexcept;
ChorusParams     chorusParamsFrom (const ParamValues&) noexcept;
PhaserParams     phaserParamsFrom (const ParamValues&) noexcept;
FlangerParams    flangerParamsFrom (const ParamValues&) noexcept;
DelayParams      delayParamsFrom (const ParamValues&) noexcept;
ReverbParams     reverbParamsFrom (const ParamValues&) noexcept;
EqParams         eqParamsFrom (const ParamValues&) noexcept;
CompressorParams compressorParamsFrom (const ParamValues&) noexcept;

class FxRack
{
public:
    static constexpr int kMaxChain = (int) FxType::Count - 1;   // each type at most once

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const ParamValues& modulated, const Transport&);   // per block
    void process (float* L, float* R, int numSamples) noexcept;
    float tailSeconds() const;

    /** The types the slots ask for, in processing order, duplicates and None removed. Returns the count. */
    static int resolveChain (const ParamValues&, FxType* out) noexcept;

    /** The types currently processing (including ones fading out), in order. For the UI / tests. */
    int currentChain (FxType* out) const noexcept;

private:
    void processChunk (float* L, float* R, int numSamples) noexcept;
    void adoptPendingIfReady() noexcept;
    void processUnit (FxType, float* L, float* R, int numSamples) noexcept;
    void applyDesired (const FxType* desired, int count) noexcept;
    void computeTail() noexcept;

    double sr = 44100.0;
    int maxBlockSize = 0;
    bool fresh = true;

    Distortion distortion;
    Chorus chorus;
    Phaser phaser;
    Flanger flanger;
    StereoDelay delay;
    Reverb reverb;
    ThreeBandEq eq;
    Compressor compressor;

    std::array<fxdetail::LinearRamp, (size_t) FxType::Count> engage;
    std::array<FxType, kMaxChain> chain {};
    int chainSize = 0;
    std::array<FxType, kMaxChain> pending {};
    int pendingSize = 0;
    bool hasPending = false;

    std::vector<float> dryL, dryR;
    std::atomic<float> tail { 0.0f };
};

} // namespace verdant
