#pragma once

// One synth voice: Osc A / Osc B -> Filter 1 -> Filter 2 -> x Env 1 -> pan,
// with its own envelopes, LFOs, living offsets and mod-matrix evaluation.
//
// Owner: W-D (the Voice API is internal to the engine). FOUNDATION STUB:
// osc A (+ osc B if on) -> serial filters -> x Env 1 x 0.35; sub, noise,
// routing, glide, filter mix / key tracking and per-sample ramps are left out.

#include "Common.h"
#include "Envelope.h"
#include "Filter.h"
#include "Lfo.h"
#include "Living.h"
#include "ModMatrix.h"
#include "Params.h"
#include "WavetableOscillator.h"

#include <ap/Scales.h>

#include <array>
#include <cstdint>

namespace verdant
{

/** What the engine hands every voice for one control block. */
struct VoiceBlockContext
{
    const ParamValues* base = nullptr;          // un-modulated params
    const ModRouting* routing = nullptr;
    ModSourceValues globalSources;              // macros, wheel, bend, aftertouch, ...
    std::array<const Wavetable*, 2> tables {};  // resolved table per oscillator
    std::array<const LfoShape*, kNumLfos> lfoShapes {};   // custom LFO shapes (may be null)
    float pitchOffset = 0.0f;                   // semitones: global pitch + bend
    ap::Scale scale;                            // global key / scale
    bool pitchSnap = false;                     // pitch_snap: snap each osc's pitch (before fine / detune) to the scale
};

class Voice
{
public:
    void prepare (double sampleRate);
    void start (int midiNote, float velocity, uint64_t order, uint32_t seed, const VoiceBlockContext&);
    void release();
    void kill();
    void setPolyAftertouch (float value) noexcept { polyAftertouch = value; }

    bool isActive() const noexcept   { return active; }
    bool isReleased() const noexcept { return released; }
    int note() const noexcept        { return midiNote; }
    uint64_t order() const noexcept  { return startOrder; }
    bool sustained = false;          // note-off arrived while the sustain pedal was down

    /** Renders one control block (<= kControlInterval samples), adding into L / R.
        offsets (numModDestinations() + 1 floats) receives this voice's summed mod offsets when not null. */
    void renderBlock (float* L, float* R, int numSamples, const VoiceBlockContext&, float* offsets) noexcept;

    float envValue (int e) const noexcept    { return envs[(size_t) e].value(); }
    float lfoValue (int l) const noexcept    { return lfos[(size_t) l].value(); }
    float oscPosition (int o) const noexcept { return oscs[(size_t) o].currentPosition(); }

private:
    void configureEnvelopes (const ParamValues&);
    void configureLfos (const ParamValues&);
    static OscParams oscParamsFrom (const ParamValues&, int osc) noexcept;

    double sr = 44100.0;
    bool active = false, released = false;
    int midiNote = 60;
    float velocity = 0.0f, polyAftertouch = 0.0f, randomValue = 0.0f;
    uint64_t startOrder = 0;

    std::array<WavetableOscillator, 2> oscs;
    std::array<VoiceFilter, 2> filters;
    std::array<Envelope, kNumEnvs> envs;
    std::array<Lfo, kNumLfos> lfos;
    LivingVoice living;
    ParamValues modulated {};
    std::array<float, kControlInterval> bufL {}, bufR {}, oscL {}, oscR {};
};

} // namespace verdant
