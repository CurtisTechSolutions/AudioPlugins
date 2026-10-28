#pragma once

// LFO with classic shapes, a drawable breakpoint shape, tempo sync and
// retrig / free / one-shot modes. Runs at control rate.
//
// Owner: W-C. FOUNDATION STUB: sine / triangle / saws / square / S&H and the
// custom breakpoints (linear, curves ignored), Retrig behaviour only, fade-in
// and polarity; smoothing ignored.

#include "Common.h"

#include <ap/Tempo.h>

#include <cstdint>
#include <vector>

namespace verdant
{

// CONTRACT DEVIATION: the contract names both the shape enum and the breakpoint
// struct `LfoShape`, which cannot coexist in one namespace. The enum is LfoWave;
// LfoShape is the breakpoint shape (patch extras, Engine::setLfoShape).
enum class LfoWave : uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, Custom, Count };
enum class LfoMode : uint8_t { Retrig, Free, OneShot };

struct LfoShapePoint { float x, y, curve; };   // x, y 0..1, curve -1..1

struct LfoShape
{
    // >= 2 points, x ascending, x[0] = 0, x[last] = 1. Default: a triangle.
    std::vector<LfoShapePoint> points { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
};

struct LfoParams
{
    LfoWave shape = LfoWave::Sine;
    float rateHz = 1;
    bool sync = false;
    ap::NoteDivision division = ap::NoteDivision::Quarter;
    LfoMode mode = LfoMode::Retrig;
    float startPhase = 0;   // 0..1
    float fadeMs = 0;
    float smooth = 0;       // 0..1
    bool unipolar = false;
};

class Lfo
{
public:
    void prepare (double sampleRate);
    void set (const LfoParams&);
    void setCustomShape (const LfoShape*);          // not owned; must outlive its use (HandOff-managed)
    void noteOn (uint32_t seed);
    void syncToTransport (const Transport&);         // call at block start (Free mode phase)
    float next (int numSamples) noexcept;            // advance by numSamples (one control block), returns the value
    float value() const noexcept;
    float phase() const noexcept;

private:
    float shapeAt (double p) noexcept;

    double sr = 44100.0;
    LfoParams params;
    const LfoShape* custom = nullptr;
    Transport transport;
    double ph = 0.0;
    double elapsed = 0.0;                            // samples since note-on (fade-in)
    float out = 0.0f, held = 0.0f;
    uint32_t rng = 0x1234567u;
};

} // namespace verdant
