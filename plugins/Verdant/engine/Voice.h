#pragma once

// One synth voice (CONTRACT §2):
//
//   Sub -> Noise -> Osc B -> Osc A  (A's FM Osc = B's current block, B's FM Osc =
//   A's previous block, FM Sub = the sub) -> per-source routing into Filter 1 /
//   Filter 2 / direct (serial or parallel) -> filter dry/wet mix -> sum
//   -> x Env 1 x lerp (0.25, 1, velocity) x 0.35 -> voice pan (balance law)
//
// with its own 3 envelopes, 4 LFOs (Free-mode LFOs come from the engine's
// shared global LFOs), physics sources, living offsets, glide and a per-voice
// mod-matrix pass at control rate. Everything is allocation-free and noexcept
// after prepare(). The Voice API is internal to the engine (Engine owns it).
//
// Owner: W-D.

#include "Common.h"
#include "Envelope.h"
#include "Filter.h"
#include "Lfo.h"
#include "Living.h"
#include "ModMatrix.h"
#include "Params.h"
#include "Physics.h"
#include "SubNoise.h"
#include "WavetableOscillator.h"

#include <ap/Scales.h>

#include <array>
#include <cstdint>

namespace verdant
{

/** What the engine hands every voice for one control block (and for note starts). */
struct VoiceBlockContext
{
    const ParamValues* base = nullptr;                    // un-modulated params (plain units)
    const ModRouting* routing = nullptr;
    ModSourceValues globalSources;                        // macros, wheel, bend, channel aftertouch, seq mod
    std::array<float, kNumLfos> freeLfoValues {};         // the engine's shared Free-mode LFOs
    std::array<const Wavetable*, 2> tables {};            // resolved table per oscillator (never null in practice)
    std::array<const LfoShape*, kNumLfos> lfoShapes {};   // custom LFO shapes (may be null)
    Transport transport;                                  // advanced to this control block's first sample
    float bendSemitones = 0.0f;                           // pitch bend x bend_range
    ap::Scale scale;                                      // global key / scale
    bool pitchSnap = false;                               // pitch_snap: snap each osc's pitch (before fine / living / unison) to the scale
};

/** Everything a note start needs besides the block context. */
struct NoteStart
{
    int note = 60;
    float velocity = 0.8f;          // 0..1
    uint64_t order = 0;             // allocation age (larger = newer)
    uint32_t globalSeed = 0;        // the engine's living seed
    uint32_t noteCounter = 0;       // per-engine note-on counter (seeds random phase, Random, noise, living)
    bool glide = false;             // glide from glideFrom (semitones) to note over glideMs
    float glideFrom = 60.0f;
    float glideMs = 0.0f;
};

class Voice
{
public:
    static constexpr float kStealFadeMs = 2.0f;   // fade applied before a stolen voice restarts

    void prepare (double sampleRate);

    /** Starts a fresh note on an idle voice: resets oscillator phases, envelopes from their current level (0). */
    void start (const NoteStart&, const VoiceBlockContext&) noexcept;

    /** Steals a busy voice: fades the current sound out over kStealFadeMs, then starts `s` (sample-accurately inside renderBlock). */
    void steal (const NoteStart&) noexcept;

    /** Mono / Legato: moves the sounding note to s.note (glide applies); retrigger = restart envelopes / Retrig LFOs / physics. */
    void retarget (const NoteStart&, const VoiceBlockContext&, bool retrigger) noexcept;

    void release() noexcept;        // note-off: envelopes enter release
    void fadeOut() noexcept;        // short fade to silence, then idle (voice limit shrank)
    void kill() noexcept;           // immediate silence (reset)
    void setPolyAftertouch (float value) noexcept;

    bool isActive() const noexcept   { return active; }
    bool isReleased() const noexcept { return hasPending ? pendingRelease : released; }
    bool isFading() const noexcept   { return fadeLeft > 0; }
    int note() const noexcept        { return hasPending ? pending.note : midiNote; }
    uint64_t order() const noexcept  { return hasPending ? pending.order : startOrder; }
    float currentPitch() const noexcept { return glide.current; }   // glided note pitch in semitones (no bend / offsets)
    bool sustained = false;          // note-off arrived while the sustain pedal was down

    /** Renders up to kControlInterval samples, adding into L / R. offsets (numModDestinations() + 1 floats)
        receives this voice's summed mod offsets when not null. */
    void renderBlock (float* L, float* R, int numSamples, const VoiceBlockContext&, float* offsets) noexcept;

    float envValue (int e) const noexcept    { return envs[(size_t) e].value(); }
    float lfoValue (int l) const noexcept    { return sources.v[(size_t) ModSource::Lfo1 + (size_t) l]; }
    float oscPosition (int o) const noexcept { return oscs[(size_t) o].currentPosition(); }
    float physicsValue (int i) const noexcept;               // 0 ball height, 1 spring, 2 orbit x, 3 orbit y
    const ModSourceValues& lastSources() const noexcept { return sources; }   // this voice's sources of the last block
    const ParamValues& modulatedParams() const noexcept { return modulated; }

private:
    struct Glide
    {
        float from = 60.0f, to = 60.0f, current = 60.0f;
        double elapsed = 0.0, length = 0.0;   // samples
        void jump (float pitch) noexcept;
        void begin (float fromPitch, float toPitch, double lengthSamples) noexcept;
        void advance (int numSamples) noexcept;
    };

    /** A linear per-sample ramp from the previous control block's value to this one's. */
    struct Ramp
    {
        float previous = 0.0f, target = 0.0f;
        void snap (float v) noexcept  { previous = target = v; }
        void next (float v) noexcept  { previous = target; target = v; }
        bool silent() const noexcept  { return previous <= 0.0f && target <= 0.0f; }
    };

    void beginNote (const NoteStart&, const VoiceBlockContext&) noexcept;
    void renderCore (float* L, float* R, int numSamples, const VoiceBlockContext&, float* offsets) noexcept;
    void noteOnSources (const VoiceBlockContext&, ModSourceValues&) noexcept;
    void configureEnvelopes (const ParamValues&) noexcept;
    void configureLfos (const ParamValues&) noexcept;
    void triggerModulators (uint32_t seed, const VoiceBlockContext&) noexcept;
    void setupGlide (const NoteStart&) noexcept;
    float pitchToHz (float semitones) const noexcept;
    static OscParams oscParamsFrom (const ParamValues&, int osc) noexcept;

    double sr = 44100.0;
    bool active = false, released = false;
    int midiNote = 60;
    float velocity = 0.0f, polyAftertouch = 0.0f, randomValue = 0.0f;
    bool hasPolyAftertouch = false;
    uint64_t startOrder = 0;

    // Steal fade: the old sound fades over fadeTotal samples, then `pending` starts.
    int fadeTotal = 96, fadeLeft = 0;
    bool hasPending = false, pendingRelease = false;
    NoteStart pending;

    std::array<WavetableOscillator, 2> oscs;
    SubOscillator sub;
    NoiseGenerator noise;
    std::array<VoiceFilter, 2> filters;
    std::array<bool, 2> filterWasOn {};
    std::array<Envelope, kNumEnvs> envs;
    std::array<Lfo, kNumLfos> lfos;
    LivingVoice living;
    BouncingBall ball;
    Spring spring;
    Orbit orbit;
    Glide glide;

    Ramp subLevel, noiseLevel, voicePan, velocityGain;
    std::array<Ramp, 2> filterMix;

    ModSourceValues sources;
    ParamValues modulated {};

    // Scratch (one control block)
    std::array<float, kControlInterval> oscL {}, oscR {}, subBuf {}, noiseBuf {}, fmBuf {}, monoB {}, prevA {};
    std::array<float, kControlInterval> f1L {}, f1R {}, f2L {}, f2R {}, dirL {}, dirR {}, dryL {}, dryR {};
    int prevALength = 0;
};

} // namespace verdant
