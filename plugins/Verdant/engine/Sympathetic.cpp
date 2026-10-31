#include "Sympathetic.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr float kRetuneMs   = 10.0f;    // crossfade between the old and new read
    constexpr float kGlideMs    = 5.0f;     // sub-sample tuning changes (damping / brightness) glide
    constexpr float kGainMs     = 20.0f;    // note-age gain and amount ramps
    constexpr float kPairCents  = 4.0f;     // unison-pair detune
    constexpr float kDamperSeconds = 0.25f; // RT60 of a released string
    constexpr float kPan        = 0.4f;     // strings alternate -0.4 / +0.4
    constexpr float kMinHz      = 20.0f;    // lower notes are raised by octaves (bounded delay memory)
    constexpr float kMaxLoopGain = 0.9985f;  // "loop gain < 0.999 always"
    constexpr float kExcScale   = 2.0f;     // excitation gain over the power normalisation: a string rings ~6 dB over its input
    constexpr float kSumScale   = 0.5f;     // string sum before the soft clip
    constexpr float kSleepLevel = 1.0e-6f;
    const float kWetScale = ap::dbToGain (-2.0f);   // 2 dB of headroom on the wet path

    inline int detuneCode (float cents) noexcept { return cents == 0.0f ? 0 : (cents > 0.0f ? 1 : 2); }
}

float Sympathetic::rt60Seconds (float decay01) noexcept
{
    return ap::lerp (0.3f, 8.0f, ap::clamp (decay01, 0.0f, 1.0f));
}

void Sympathetic::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize = std::max (1, maxBlock);

    const int maxDelay = (int) std::ceil (sr / (double) kMinHz) + 8;
    for (int s = 0; s < kMaxStrings; ++s)
    {
        auto& st = strings[(size_t) s];
        st.line.prepare (maxDelay);
        st.gain.prepare (sr, kGainMs);
        // Constant-power pan, alternating sides.
        const float pan = (s % 2 == 0) ? -kPan : kPan;
        const float theta = (pan + 1.0f) * 0.25f * ap::kPi;
        st.panL = std::cos (theta);
        st.panR = std::sin (theta);
    }
    amount.prepare (sr, kGainMs);
    for (int k = 0; k < kPowerPoints; ++k)
        powerCos[(size_t) k] = std::cos (ap::kPi * ((float) k + 0.5f) / (float) kPowerPoints);

    excitation.assign ((size_t) maxBlockSize, 0.0f);
    amountBuf.assign ((size_t) maxBlockSize, 0.0f);
    wetL.assign ((size_t) maxBlockSize, 0.0f);
    wetR.assign ((size_t) maxBlockSize, 0.0f);

    hp.configure (ap::Biquad::Type::HighPass, sr, 80.0f, 0.7071f);
    configuredBright = -1.0f;
    dcL.prepare (sr);
    dcR.prepare (sr);
    reset();
}

void Sympathetic::reset()
{
    for (int s = 0; s < kMaxStrings; ++s)
    {
        auto& st = strings[(size_t) s];
        st.line.reset();
        st.key = -1;
        st.note = -1;
        st.cents = 0.0f;
        st.released = false;
        st.fade = 1.0f;
        st.lpState = 0.0f;
        st.loopGain = st.targetLoopGain = 0.0f;
        st.excGain = 0.0f;
        st.gain.snap (0.0f);
        st.level = 0.0f;
        st.asleep = true;
        uiNote[(size_t) s].store (-1, std::memory_order_relaxed);
        uiLevel[(size_t) s].store (0.0f, std::memory_order_relaxed);
    }
    noteCount = 0;
    hp.reset();
    shelf.reset();
    dcL.reset();
    dcR.reset();
    amount.snap (0.0f);
    fresh = true;
}

void Sympathetic::setParams (float amount01, int newMemory, float decay01, float bright01, float damp01, int newStrings, bool newSnap, const ap::Scale& newScale)
{
    const float a = std::isfinite (amount01) ? ap::clamp (amount01, 0.0f, 1.0f) : 0.0f;
    newMemory = std::clamp (newMemory, 1, kMaxMemory);
    newStrings = std::clamp (newStrings, 1, kMaxStrings);
    decay01 = std::isfinite (decay01) ? ap::clamp (decay01, 0.0f, 1.0f) : 0.5f;
    bright01 = std::isfinite (bright01) ? ap::clamp (bright01, 0.0f, 1.0f) : 0.5f;
    damp01 = std::isfinite (damp01) ? ap::clamp (damp01, 0.0f, 1.0f) : 0.3f;

    if (fresh) amount.snap (a);
    else       amount.setTarget (a);

    const bool scaleChanged = newScale.root != scale.root || newScale.mask != scale.mask;
    const bool reassign = fresh || newMemory != memory || newStrings != numStrings || newSnap != snap || (newSnap && scaleChanged);
    const bool retune = fresh || decay01 != decay || bright01 != bright || damp01 != damp;

    memory = newMemory;
    numStrings = newStrings;
    snap = newSnap;
    scale = newScale;
    decay = decay01;
    bright = bright01;
    damp = damp01;
    noteCount = std::min (noteCount, memory);

    if (std::fabs (bright - configuredBright) > 1.0e-3f)
    {
        // Brightness: a gentle ±6 dB high shelf on the excitation ...
        shelf.configure (ap::Biquad::Type::HighShelf, sr, 3000.0f, 0.7071f, (bright - 0.5f) * 12.0f);
        configuredBright = bright;
    }
    // ... and the in-loop damping low-pass: damping pulls it down (18 kHz .. 400 Hz), brightness moves it ±1 octave.
    const float lpHz = ap::clamp (400.0f * std::exp2 (5.5f * (1.0f - damp) + 2.0f * (bright - 0.5f)), 100.0f, (float) (sr * 0.45));
    lpCoeff = std::exp (-ap::kTwoPi * lpHz / (float) sr);

    if (reassign)
        updateAssignments();
    else if (retune)
        for (auto& st : strings)
            if (st.key >= 0) updateTuning (st);

    fresh = false;
}

void Sympathetic::noteOn (int midiNote)
{
    midiNote = std::clamp (midiNote, 0, 127);

    int count = noteCount;
    for (int i = 0; i < count; ++i)
        if (notes[(size_t) i] == midiNote)
        {
            for (int j = i; j + 1 < count; ++j) notes[(size_t) j] = notes[(size_t) (j + 1)];
            --count;
            break;
        }
    for (int j = std::min (count, kMaxMemory - 1); j > 0; --j)
        notes[(size_t) j] = notes[(size_t) (j - 1)];
    notes[0] = midiNote;
    noteCount = std::min (count + 1, memory);

    updateAssignments();
}

int Sympathetic::rememberedNotes (int* out, int maxOut) const noexcept
{
    const int n = std::min (noteCount, maxOut);
    for (int i = 0; i < n; ++i) out[i] = notes[(size_t) i];
    return n;
}

int Sympathetic::stringNote (int s) const noexcept
{
    return s >= 0 && s < kMaxStrings ? uiNote[(size_t) s].load (std::memory_order_relaxed) : -1;
}

float Sympathetic::stringLevel (int s) const noexcept
{
    return s >= 0 && s < kMaxStrings ? uiLevel[(size_t) s].load (std::memory_order_relaxed) : 0.0f;
}

void Sympathetic::updateAssignments() noexcept
{
    struct Want { int key, note; float cents, gain; bool done; };
    std::array<Want, kMaxStrings> want {};
    int numWant = 0;

    const int m = std::min (numStrings, memory);
    for (int j = 0; j < numStrings; ++j)
    {
        int k = j;
        float cents = 0.0f;
        if (j >= m)
        {
            // Unison pairs: extra strings revisit the notes, most recent first, +4, -4, +4 ... cents.
            k = (j - m) % m;
            const int round = 1 + (j - m) / m;
            cents = (round % 2 == 1) ? kPairCents : -kPairCents;
        }
        if (k >= noteCount)
            continue;

        const int raw = notes[(size_t) k];
        const int note = snap ? std::clamp (scale.nearest ((float) raw), 0, 127) : raw;
        want[(size_t) numWant++] = { note * 4 + detuneCode (cents), note, cents, 1.0f - (float) k / (float) memory, false };
    }

    // Strings sharing a note ring nearly in phase (same excitation): keep their sum at about
    // one string's level so Strings > Memory adds shimmer, not loudness.
    for (int w = 0; w < numWant; ++w)
    {
        int copies = 0;
        for (int o = 0; o < numWant; ++o) copies += want[(size_t) o].note == want[(size_t) w].note ? 1 : 0;
        want[(size_t) w].gain /= std::sqrt ((float) copies);
    }

    std::array<bool, kMaxStrings> taken {};

    // Keep strings that already ring the wanted (note, detune).
    for (int w = 0; w < numWant; ++w)
        for (int s = 0; s < numStrings; ++s)
            if (! taken[(size_t) s] && strings[(size_t) s].key == want[(size_t) w].key)
            {
                taken[(size_t) s] = true;
                want[(size_t) w].done = true;
                strings[(size_t) s].gain.setTarget (want[(size_t) w].gain);
                break;
            }

    // New (note, detune) pairs take the quietest free string among the first `strings`
    // (a silent one when there is one; otherwise a ringing one retunes with a crossfade).
    for (int w = 0; w < numWant; ++w)
    {
        if (want[(size_t) w].done) continue;
        int best = -1;
        float bestLevel = 1.0e30f;
        for (int s = 0; s < numStrings; ++s)
        {
            if (taken[(size_t) s]) continue;
            const auto& st = strings[(size_t) s];
            const float lvl = st.asleep ? -1.0f : st.level * std::max (st.gain.value(), 1.0e-3f);
            if (lvl < bestLevel) { bestLevel = lvl; best = s; }
        }
        if (best < 0) break;

        auto& st = strings[(size_t) best];
        taken[(size_t) best] = true;
        st.key = want[(size_t) w].key;
        st.note = want[(size_t) w].note;
        st.cents = want[(size_t) w].cents;
        st.released = false;
        st.gain.setTarget (want[(size_t) w].gain);
        updateTuning (st);
        uiNote[(size_t) best].store (st.note, std::memory_order_relaxed);
    }

    // Everything else is released: no more excitation, and a damper (short RT60) lets
    // whatever still rings die away naturally at its own pitch.
    for (int s = 0; s < kMaxStrings; ++s)
    {
        if (taken[(size_t) s]) continue;
        auto& st = strings[(size_t) s];
        uiNote[(size_t) s].store (-1, std::memory_order_relaxed);
        if (st.key < 0) continue;
        st.key = -1;
        st.released = true;
        updateTuning (st);
    }
}

void Sympathetic::updateTuning (String& st) noexcept
{
    float hz = ap::midiToHz ((float) st.note + st.cents * 0.01f);
    while (hz < kMinHz) hz *= 2.0f;
    hz = std::min (hz, (float) (sr / 2.5));
    st.hz = hz;

    // Loop delay = one period minus the damping low-pass's phase delay at the fundamental.
    const float w = ap::kTwoPi * hz / (float) sr;
    const float tau = std::atan2 (lpCoeff * std::sin (w), 1.0f - lpCoeff * std::cos (w)) / w;
    const float maxD = (float) (st.line.maxDelay() - 2);
    const float d = ap::clamp ((float) sr / hz - tau, 2.0f, maxD);

    // Per-period loop gain for a -60 dB decay of the fundamental after RT60 seconds: the damping
    // low-pass already loses |H (f0)| per period, so the gain makes up for it (capped below 0.999).
    const float a = lpCoeff;
    const float h0 = (1.0f - a) / std::sqrt (std::max (1.0e-12f, 1.0f - 2.0f * a * std::cos (w) + a * a));
    const float rt60 = st.released ? kDamperSeconds : rt60Seconds (decay);
    const float g = std::min (kMaxLoopGain, std::pow (10.0f, -3.0f / (hz * rt60)) / std::max (h0, 1.0e-3f));
    st.targetLoopGain = g;

    // Excitation gain: normalise the string's white-noise power gain, the average over frequency
    // of 1 / (1 - r²) with r = g |H (w)| (a comb's mean power gain), so every string rings at
    // about the level of what excites it whatever its pitch, decay and damping.
    float power = 0.0f;
    for (int k = 0; k < kPowerPoints; ++k)
    {
        const float h = (1.0f - a) / std::sqrt (std::max (1.0e-12f, 1.0f - 2.0f * a * powerCos[(size_t) k] + a * a));
        const float r = std::min (0.9999f, g * h);
        power += 1.0f / (1.0f - r * r);
    }
    st.excGain = st.released ? 0.0f : kExcScale / std::sqrt (power / (float) kPowerPoints);
    st.targetDelay = d;

    if (st.asleep)
    {
        // Nothing audible in the loop: jump straight to the new tuning.
        st.delay = st.oldDelay = d;
        st.fade = 1.0f;
        st.loopGain = g;
        st.lpState = 0.0f;
    }
}

void Sympathetic::process (float* L, float* R, int numSamples) noexcept
{
    if (L == nullptr || R == nullptr)
        return;

    if (amount.isSettledAt (0.0f))
    {
        bool anyAwake = false;
        for (auto& st : strings) anyAwake = anyAwake || ! st.asleep;
        if (! anyAwake)
            return;   // exact bypass, nothing ringing
    }

    for (int pos = 0; pos < numSamples; pos += maxBlockSize)
        processChunk (L + pos, R + pos, std::min (maxBlockSize, numSamples - pos));
}

void Sympathetic::processChunk (float* L, float* R, int n) noexcept
{
    const bool silent = amount.isSettledAt (0.0f);

    // Excitation: the dry input (mono), brightness shelf, 80 Hz high-pass, scaled by amount.
    float excPeak = 0.0f;
    if (silent)
    {
        std::fill (excitation.begin(), excitation.begin() + n, 0.0f);
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = amount.next();
            amountBuf[(size_t) i] = a;
            const float e = hp.process (shelf.process (0.5f * (L[i] + R[i]))) * a;
            excitation[(size_t) i] = fxdetail::sanitise (e);
            excPeak = std::max (excPeak, std::fabs (e));
        }
    }

    std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
    std::fill (wetR.begin(), wetR.begin() + n, 0.0f);

    const float fadeStep = 1.0f / (0.001f * kRetuneMs * (float) sr);
    const float glide = 1.0f - ap::onePoleCoeff (kGlideMs, sr);
    const float a = lpCoeff;

    for (int s = 0; s < kMaxStrings; ++s)
    {
        auto& st = strings[(size_t) s];
        const bool excited = st.key >= 0 && excPeak * st.excGain > 1.0e-9f;
        if (st.asleep && ! excited)
            continue;
        if (st.asleep)
        {
            st.asleep = false;
            st.gain.snap (st.gain.targetValue());   // a silent string can take its level at once
        }

        const float gStep = (st.targetLoopGain - st.loopGain) / (float) n;
        const float exc = st.key >= 0 ? st.excGain : 0.0f;
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float o;
            if (st.fade < 1.0f)
            {
                // Retune: crossfade from the read at the old length to the read at the new one.
                const float oldRead = st.line.readFractional (st.oldDelay - 1.0f);
                const float newRead = st.line.readFractional (st.delay - 1.0f);
                o = oldRead + st.fade * (newRead - oldRead);
                st.fade = std::min (1.0f, st.fade + fadeStep);
            }
            else
            {
                if (std::fabs (st.targetDelay - st.delay) >= 1.0f)
                {
                    st.oldDelay = st.delay;
                    st.delay = st.targetDelay;
                    st.fade = 0.0f;
                    o = st.line.readFractional (st.oldDelay - 1.0f);
                    st.fade = fadeStep;
                }
                else
                {
                    st.delay += (st.targetDelay - st.delay) * glide;
                    o = st.line.readFractional (st.delay - 1.0f);
                }
            }

            // Damping low-pass in the loop, then the loop gain (< 0.999).
            st.lpState = fxdetail::sanitise (o + a * (st.lpState - o));
            st.loopGain += gStep;
            st.line.push (fxdetail::sanitise (excitation[(size_t) i] * exc + st.loopGain * st.lpState));

            const float y = o * st.gain.next();
            wetL[(size_t) i] += y * st.panL;
            wetR[(size_t) i] += y * st.panR;
            peak = std::max (peak, std::fabs (o));
        }
        st.loopGain = st.targetLoopGain;
        st.level = peak;
        uiLevel[(size_t) s].store (peak * st.gain.value(), std::memory_order_relaxed);

        // Sleep once the string is inaudible and nothing excites it.
        if (peak < kSleepLevel && ! excited)
        {
            st.asleep = true;
            st.lpState = 0.0f;
            uiLevel[(size_t) s].store (0.0f, std::memory_order_relaxed);
        }
    }

    if (silent)
        return;   // strings keep running (so re-enabling is smooth) but the output stays exact

    for (int i = 0; i < n; ++i)
    {
        const float wet = amountBuf[(size_t) i] * kWetScale;
        const float l = ap::softClip (dcL.process (wetL[(size_t) i]) * kSumScale, 1.0f);
        const float r = ap::softClip (dcR.process (wetR[(size_t) i]) * kSumScale, 1.0f);
        L[i] += wet * l;
        R[i] += wet * r;
    }
}

} // namespace verdant
