#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <ap/Filters.h>
#include <ap/Math.h>
#include <ap/Oscillator.h>

#include <array>

namespace strata
{

/** Parameter snapshot the processor refreshes once per block; voices read it. */
struct Settings
{
    struct Osc
    {
        ap::Waveform wave = ap::Waveform::Saw;
        int   octave = 0;
        int   semi = 0;
        float fineCents = 0.0f;
        float level = 1.0f;          // 0..1
        float pulseWidth = 0.5f;
    };

    Osc oscA, oscB;
    int   unisonVoices = 1;          // 1..4 (osc A only)
    float unisonDetuneCents = 12.0f;
    float unisonSpread = 0.5f;       // 0..1 stereo spread

    float subLevel = 0.0f;
    bool  subSquare = false;
    int   subOctave = -1;            // -1 or -2
    float noiseLevel = 0.0f;

    int   filterType = 0;            // 0 LP, 1 BP, 2 HP
    float cutoffHz = 4000.0f;
    float resonance = 0.2f;          // 0..1
    float filterEnvAmount = 0.0f;    // -1..1 (octaves scaled below)
    float keyTrack = 0.0f;           // 0..1
    float filterLfoAmount = 0.0f;    // -1..1
    float drive = 0.0f;              // 0..1

    juce::ADSR::Parameters ampEnv { 0.005f, 0.2f, 0.8f, 0.3f };
    juce::ADSR::Parameters filterEnv { 0.005f, 0.3f, 0.2f, 0.3f };

    ap::Lfo::Shape lfo1Shape = ap::Lfo::Shape::Sine;
    float lfo1RateHz = 5.0f;
    float lfo1DepthCents = 0.0f;
    float lfo1FadeMs = 0.0f;

    ap::Lfo::Shape lfo2Shape = ap::Lfo::Shape::Sine;
    float lfo2RateHz = 1.0f;

    float glideMs = 0.0f;
    float velocityToAmp = 0.7f;      // 0..1
    float velocityToFilter = 0.5f;   // 0..1
    float pitchBendRange = 2.0f;     // semitones
};

class Sound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class Voice : public juce::SynthesiserVoice
{
public:
    explicit Voice (const Settings& s) : settings (s) {}

    using juce::SynthesiserVoice::renderNextBlock;

    void setGlideSource (float* lastNoteHz) noexcept { glideSource = lastNoteHz; }
    void setModWheel (float amount) noexcept         { modWheel = amount; }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<Sound*> (s) != nullptr; }

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& o : unison) o.prepare (sampleRate);
        oscB.prepare (sampleRate);
        sub.prepare (sampleRate);
        lfo1.prepare (sampleRate);
        lfo2.prepare (sampleRate);
        ampEnv.setSampleRate (sampleRate);
        filterEnv.setSampleRate (sampleRate);
        juce::dsp::ProcessSpec spec { sampleRate, 64, 1 };
        for (auto& f : filters) { f.prepare (spec); f.reset(); }
        glide.prepare (sampleRate, 1.0f);
    }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheelPos) override
    {
        noteNumber = midiNoteNumber;
        vel = velocity;
        pitchWheel = (float) (pitchWheelPos - 8192) / 8192.0f;

        const float targetHz = ap::midiToHz ((float) midiNoteNumber);
        glide.prepare (sr, settings.glideMs);
        if (glideSource != nullptr && *glideSource > 0.0f && settings.glideMs > 0.0f)
        {
            glide.snap (std::log2 (*glideSource));
            glide.setTarget (std::log2 (targetHz));
        }
        else
        {
            glide.snap (std::log2 (targetHz));
        }
        if (glideSource != nullptr) *glideSource = targetHz;

        for (int i = 0; i < 4; ++i)
            unison[(size_t) i].reset (settings.oscA.wave == ap::Waveform::Triangle ? 0.25f : rng.nextUnipolar() * 0.3f);
        oscB.reset (settings.oscB.wave == ap::Waveform::Triangle ? 0.25f : 0.0f);
        sub.reset (0.0f);
        lfo1.reset();
        lfo2.reset();
        lfo1Fade = 0.0f;

        ampEnv.setParameters (settings.ampEnv);
        filterEnv.setParameters (settings.filterEnv);
        ampEnv.noteOn();
        filterEnv.noteOn();
        for (auto& f : filters) f.reset();
        sampleCounter = 0;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            filterEnv.noteOff();
        }
        else
        {
            ampEnv.reset();
            filterEnv.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newValue) override { pitchWheel = (float) (newValue - 8192) / 8192.0f; }
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! ampEnv.isActive())
        {
            clearCurrentNote();
            return;
        }

        const auto& s = settings;
        configureOscillators();

        float* left  = output.getWritePointer (0, startSample);
        float* right = output.getNumChannels() > 1 ? output.getWritePointer (1, startSample) : nullptr;

        const float ampVel = ap::lerp (1.0f, vel, s.velocityToAmp);
        const float filtVel = ap::lerp (1.0f, vel, s.velocityToFilter);
        const float lfo1FadeInc = s.lfo1FadeMs > 0.0f ? (float) (1000.0 / (s.lfo1FadeMs * sr)) : 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Pitch: glide (log2 Hz), bend, vibrato.
            const float log2Hz = glide.next();
            lfo1Fade = std::min (1.0f, lfo1Fade + lfo1FadeInc);
            const float vibratoCents = lfo1.next() * (s.lfo1DepthCents + modWheel * 50.0f) * lfo1Fade;
            const float bendSemis = pitchWheel * s.pitchBendRange;
            const float baseHz = std::exp2 (log2Hz + (bendSemis + vibratoCents * 0.01f) / 12.0f);

            if ((sampleCounter & 7) == 0)
                updateFrequencies (baseHz);

            // Oscillators.
            float l = 0.0f, r = 0.0f;
            for (int u = 0; u < s.unisonVoices; ++u)
            {
                const float v = unison[(size_t) u].next() * unisonGain;
                l += v * unisonPanL[(size_t) u];
                r += v * unisonPanR[(size_t) u];
            }
            l *= s.oscA.level; r *= s.oscA.level;
            const float b = oscB.next() * s.oscB.level;
            const float sb = sub.next() * s.subLevel;
            const float n = rng.nextBipolar() * s.noiseLevel;
            l += b + sb + n;
            r += b + sb + n;

            // Filter modulation.
            const float fEnv = filterEnv.getNextSample() * filtVel;
            const float lfo2Value = lfo2.next();
            if ((sampleCounter & 15) == 0)
            {
                const float keyOffsetOct = s.keyTrack * ((float) noteNumber - 60.0f) / 12.0f;
                const float octaves = s.filterEnvAmount * 6.0f * fEnv + s.filterLfoAmount * 3.0f * lfo2Value + keyOffsetOct;
                const float cutoff = ap::clamp (s.cutoffHz * std::exp2 (octaves), 20.0f, (float) (sr * 0.45));
                for (auto& f : filters)
                {
                    f.setCutoffFrequency (cutoff);
                    f.setResonance (ap::lerp (0.5f, 8.0f, s.resonance * s.resonance));
                }
            }

            if (s.drive > 0.0f)
            {
                const float d = 1.0f + s.drive * 6.0f;
                l = ap::fastTanh (l * d) / std::sqrt (d);
                r = ap::fastTanh (r * d) / std::sqrt (d);
            }

            l = filters[0].processSample (0, l);
            r = filters[1].processSample (0, r);

            // Amplitude.
            const float env = ampEnv.getNextSample() * ampVel * 0.35f;
            if (right != nullptr)
            {
                left[i]  += l * env;
                right[i] += r * env;
            }
            else
            {
                left[i] += 0.5f * (l + r) * env;
            }

            ++sampleCounter;

            if (! ampEnv.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    void configureOscillators()
    {
        const auto& s = settings;
        for (auto& o : unison)
        {
            o.setWaveform (s.oscA.wave);
            o.setPulseWidth (s.oscA.pulseWidth);
        }
        oscB.setWaveform (s.oscB.wave);
        oscB.setPulseWidth (s.oscB.pulseWidth);
        sub.setWaveform (s.subSquare ? ap::Waveform::Square : ap::Waveform::Sine);
        lfo1.setShape (s.lfo1Shape);
        lfo1.setRate (s.lfo1RateHz);
        lfo2.setShape (s.lfo2Shape);
        lfo2.setRate (s.lfo2RateHz);

        const int uv = ap::clamp (s.unisonVoices, 1, 4);
        unisonGain = 1.0f / std::sqrt ((float) uv);
        for (int u = 0; u < uv; ++u)
        {
            // Spread detune symmetrically around the centre, pan likewise.
            const float t = uv == 1 ? 0.0f : ((float) u / (float) (uv - 1)) * 2.0f - 1.0f;
            unisonDetune[(size_t) u] = t * s.unisonDetuneCents;
            const float pan = t * s.unisonSpread;    // -1..1
            unisonPanL[(size_t) u] = std::cos ((pan + 1.0f) * 0.25f * ap::kPi) * 1.4142f;
            unisonPanR[(size_t) u] = std::sin ((pan + 1.0f) * 0.25f * ap::kPi) * 1.4142f;
        }

        const auto& t = s.filterType == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                      : s.filterType == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                          : juce::dsp::StateVariableTPTFilterType::lowpass;
        for (auto& f : filters) f.setType (t);
    }

    void updateFrequencies (float baseHz)
    {
        const auto& s = settings;
        const float aRatio = ap::semitonesToRatio ((float) (s.oscA.octave * 12 + s.oscA.semi) + s.oscA.fineCents * 0.01f);
        const float bRatio = ap::semitonesToRatio ((float) (s.oscB.octave * 12 + s.oscB.semi) + s.oscB.fineCents * 0.01f);
        for (int u = 0; u < s.unisonVoices; ++u)
            unison[(size_t) u].setFrequency (baseHz * aRatio * ap::semitonesToRatio (unisonDetune[(size_t) u] * 0.01f));
        oscB.setFrequency (baseHz * bRatio);
        sub.setFrequency (baseHz * (s.subOctave <= -2 ? 0.25f : 0.5f));
    }

    const Settings& settings;
    double sr = 44100.0;
    int    noteNumber = 60;
    float  vel = 1.0f, pitchWheel = 0.0f, modWheel = 0.0f;
    float* glideSource = nullptr;
    ap::Smoother glide;
    int    sampleCounter = 0;

    std::array<ap::PolyBlepOscillator, 4> unison;
    std::array<float, 4> unisonDetune {}, unisonPanL {}, unisonPanR {};
    float unisonGain = 1.0f;
    ap::PolyBlepOscillator oscB, sub;
    ap::Rng rng;
    ap::Lfo lfo1, lfo2;
    float lfo1Fade = 0.0f;

    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filters;
    juce::ADSR ampEnv, filterEnv;
};

/** juce::Synthesiser with a run-time polyphony limit (voices stay allocated). */
class PolySynth : public juce::Synthesiser
{
public:
    void setPolyphony (int n) noexcept { limit = juce::jmax (1, n); }

    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* sound, int midiChannel,
                                           int midiNote, bool stealIfNoneAvailable) const override
    {
        const int n = juce::jmin (limit, getNumVoices());
        for (int i = 0; i < n; ++i)
        {
            auto* v = getVoice (i);
            if (! v->isVoiceActive() && v->canPlaySound (sound))
                return v;
        }
        if (stealIfNoneAvailable)
            return findVoiceToSteal (sound, midiChannel, midiNote);
        return nullptr;
    }

private:
    int limit = 16;
};

} // namespace strata
