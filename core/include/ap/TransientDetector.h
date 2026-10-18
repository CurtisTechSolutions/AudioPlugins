#pragma once

#include "Dynamics.h"
#include "Math.h"

#include <cmath>

namespace ap
{

/**
    Fast-vs-slow envelope transient detector.

    attackAmount() is >0 while the signal is rising faster than its recent
    average (an onset); sustainAmount() is >0 while it is sitting below it.
    Both are 0..1-ish, smoothed, and level independent.
*/
class TransientDetector
{
public:
    struct Params
    {
        float fastAttackMs  = 0.5f;
        float fastReleaseMs = 25.0f;
        float slowAttackMs  = 20.0f;
        float slowReleaseMs = 250.0f;
        float onsetThresholdDb = 4.0f;   // fast must exceed slow by this to count as an onset
    };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fast.prepare (sampleRate);
        slow.prepare (sampleRate);
        setParams (params);
        reset();
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        fast.setTimes (p.fastAttackMs, p.fastReleaseMs);
        slow.setTimes (p.slowAttackMs, p.slowReleaseMs);
    }

    void reset() noexcept
    {
        fast.reset();
        slow.reset();
        attack = sustain = 0.0f;
        onsetFlag = false;
        armed = true;
    }

    /** Feed one (mono) sample. Returns true on the first sample of a new onset. */
    inline bool process (float x) noexcept
    {
        const float f = fast.process (x);
        const float s = slow.process (x);
        const float ratioDb = gainToDb (f + 1.0e-6f) - gainToDb (s + 1.0e-6f);

        // 0 at the threshold, 1 at +12 dB above the slow envelope.
        attack  = clamp ((ratioDb - params.onsetThresholdDb) / 12.0f, 0.0f, 1.0f);
        // 0 when at the slow level, 1 when 12 dB under it.
        sustain = clamp (-ratioDb / 12.0f, 0.0f, 1.0f);

        onsetFlag = false;
        if (ratioDb > params.onsetThresholdDb)
        {
            if (armed) { onsetFlag = true; armed = false; }
        }
        else if (ratioDb < params.onsetThresholdDb * 0.5f)
        {
            armed = true;   // re-arm once the fast envelope has fallen back
        }
        return onsetFlag;
    }

    float attackAmount() const noexcept  { return attack; }
    float sustainAmount() const noexcept { return sustain; }
    bool  isOnset() const noexcept       { return onsetFlag; }

private:
    double sr = 44100.0;
    Params params;
    EnvelopeFollower fast, slow;
    float attack = 0.0f, sustain = 0.0f;
    bool onsetFlag = false, armed = true;
};

} // namespace ap
