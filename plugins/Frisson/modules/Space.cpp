#include "modules/Space.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace frisson
{

namespace
{
    // R2 width.
    constexpr float kWidthPerAmount = 0.8f;
    constexpr float kMaxWidth       = 2.2f;
    constexpr float kWidthMulMin    = 0.5f;
    constexpr float kWidthMulMax    = 2.0f;

    // R3 side brightness.
    constexpr float kSideShelfHz    = 8000.0f;
    constexpr float kSideShelfQ     = 0.70710678f;
    constexpr float kSideShelfDb    = 2.0f;

    // R4 room. Delays in ms: [channel][tap]; tap 2 is polarity-inverted.
    constexpr float kTapMs[kMaxChannels][2] = { { 11.3f, 17.9f }, { 13.7f, 23.1f } };
    constexpr float kTapLevelDb     = -18.0f;
    constexpr float kTapShelfHz     = 5000.0f;
    constexpr float kTapShelfQ      = 0.70710678f;
    constexpr float kTapShelfDb     = -6.0f;
    // Opposite-side bias of the tap feed: L taps hear M - b S = 0.25 L + 0.75 R
    // (R taps the mirror). For dual mono both feeds are exactly M.
    constexpr float kOppositeBias   = 0.5f;

    // R9 split.
    constexpr float kMonoHzMin      = 60.0f;
    constexpr float kMonoHzMax      = 300.0f;

    constexpr float kAmountSmoothMs = 20.0f;
    constexpr float kWidthSmoothMs  = 30.0f;
    constexpr float kRoomSmoothMs   = 30.0f;
    constexpr float kMonoHzSmoothMs = 50.0f;

    constexpr float kShelfEpsilon   = 1.0e-4f;   // amount change that re-designs the side shelf
    constexpr float kMonoHzEpsilon  = 0.01f;     // Hz change that re-designs the split

    inline float widthFor (float amount, float widthMul) noexcept
    {
        return std::min (kMaxWidth, 1.0f + kWidthPerAmount * amount * widthMul);
    }
}

//==============================================================================
void SpaceModule::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double controlRate = sr / (double) kControlInterval;

    amountSm.prepare (sr, kAmountSmoothMs);
    widthMulSm.prepare (sr, kWidthSmoothMs);
    roomSm.prepare (sr, kRoomSmoothMs);
    monoHzSm.prepare (controlRate, kMonoHzSmoothMs);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        int longest = 1;
        for (int t = 0; t < 2; ++t)
        {
            tapDelay[ch][t] = std::max (1, (int) std::lround ((double) kTapMs[ch][t] * 0.001 * sr));
            longest = std::max (longest, tapDelay[ch][t]);
        }
        roomLine[ch].prepare (longest + 1);
        tapShelf[ch].configure (ap::Biquad::Type::HighShelf, sr, kTapShelfHz, kTapShelfQ, kTapShelfDb);
    }

    splitter.prepare (sr, monoHzTarget);
    appliedMonoHz = monoHzTarget;

    reset();
}

void SpaceModule::reset()
{
    amountSm.snap (util::isOff (amountTarget) ? 0.0f : amountTarget);
    snapSecondary();
    clearState();
    applySideShelf (amountSm.getCurrent());
    controlCountdown = 0;
    bypassed = util::isOff (amountTarget);
    publishActivity (amountSm.getCurrent(), widthMulSm.getCurrent());
}

//==============================================================================
void SpaceModule::setAmount (float amount)
{
    amountTarget = util::clamp01 (amount);
    amountSm.setTarget (util::isOff (amountTarget) ? 0.0f : amountTarget);
}

void SpaceModule::setModulation (const Modulation& m)
{
    widthMulTarget = ap::clamp (std::isfinite (m.widthMul) ? m.widthMul : 1.0f, kWidthMulMin, kWidthMulMax);
    widthMulSm.setTarget (widthMulTarget);
}

void SpaceModule::setMonoBelowHz (float hz)
{
    monoHzTarget = ap::clamp (hz, kMonoHzMin, kMonoHzMax);
    monoHzSm.setTarget (monoHzTarget);
}

void SpaceModule::setRoom (float room)
{
    roomTarget = util::clamp01 (room);
    roomSm.setTarget (roomTarget);
}

//==============================================================================
void SpaceModule::clearState() noexcept
{
    splitter.reset();
    sideShelf.reset();
    for (auto& f : tapShelf)
        f.reset();
    for (auto& line : roomLine)
        line.reset();
}

void SpaceModule::snapSecondary() noexcept
{
    widthMulSm.snap (widthMulTarget);
    roomSm.snap (roomTarget);
    monoHzSm.snap (monoHzTarget);
    if (std::fabs (monoHzTarget - appliedMonoHz) > kMonoHzEpsilon)
    {
        splitter.setFrequency (monoHzTarget);
        appliedMonoHz = monoHzTarget;
    }
}

void SpaceModule::applySideShelf (float amount) noexcept
{
    sideShelf.configure (ap::Biquad::Type::HighShelf, sr, kSideShelfHz, kSideShelfQ, kSideShelfDb * amount);
    appliedShelfAmount = amount;
}

void SpaceModule::updateControl() noexcept
{
    const float hz = monoHzSm.next();
    if (std::fabs (hz - appliedMonoHz) > kMonoHzEpsilon)
    {
        splitter.setFrequency (hz);
        appliedMonoHz = hz;
    }

    const float amount = amountSm.getCurrent();
    if (std::fabs (amount - appliedShelfAmount) > kShelfEpsilon)
        applySideShelf (amount);
}

void SpaceModule::publishActivity (float amount, float widthMul) noexcept
{
    const float width = widthFor (amount, widthMul);
    activityValue.store (util::clamp01 ((width - 1.0f) / (kMaxWidth - 1.0f)), std::memory_order_relaxed);
}

//==============================================================================
void SpaceModule::process (float* left, float* right, int numSamples)
{
    if (numSamples <= 0 || left == nullptr || right == nullptr)
        return;

    if (bypassed)
    {
        if (util::isOff (amountTarget))
            return;                                   // exact bypass: buffers untouched

        // Engage: start from clean filters/delays and the current secondary
        // settings; the amount ramps up from 0, so nothing clicks.
        snapSecondary();
        clearState();
        applySideShelf (amountSm.getCurrent());
        controlCountdown = 0;
        bypassed = false;
    }
    else if (util::isOff (amountTarget) && ! amountSm.isSmoothing())
    {
        amountSm.snap (0.0f);
        bypassed = true;
        publishActivity (0.0f, 1.0f);
        return;
    }

    const bool  monoBuffer = (left == right);
    const float tapLevel   = ap::dbToGain (kTapLevelDb);
    const int   dL1 = tapDelay[0][0], dL2 = tapDelay[0][1];
    const int   dR1 = tapDelay[1][0], dR2 = tapDelay[1][1];

    float amount = amountSm.getCurrent();
    float widthMul = widthMulSm.getCurrent();

    int pos = 0;
    while (pos < numSamples)
    {
        if (controlCountdown <= 0)
        {
            updateControl();
            controlCountdown = kControlInterval;
        }

        const int end = pos + std::min (numSamples - pos, controlCountdown);
        for (int i = pos; i < end; ++i)
        {
            amount   = amountSm.next();
            widthMul = widthMulSm.next();
            const float room  = roomSm.next();
            const float width = widthFor (amount, widthMul);
            const float gain  = tapLevel * amount * room;

            const float l = left[i];
            const float r = right[i];
            const float m = 0.5f * (l + r);
            const float s = 0.5f * (l - r);

            // Width: complementary split, S_low = S - S_high (LR4's own low band is unused).
            float sLowUnused, sHigh;
            splitter.process (s, sLowUnused, sHigh);
            const float sOut = (s - sHigh) + width * sideShelf.process (sHigh);

            // Room: opposite-side-biased mono feeds, two taps per side, tap 2 inverted.
            roomLine[0].push (m - kOppositeBias * s);
            roomLine[1].push (m + kOppositeBias * s);
            const float tapL = tapShelf[0].process (roomLine[0].read (dL1) - roomLine[0].read (dL2));
            const float tapR = tapShelf[1].process (roomLine[1].read (dR1) - roomLine[1].read (dR2));

            const float outL = m + sOut + gain * tapL;
            const float outR = m - sOut + gain * tapR;

            if (monoBuffer)
            {
                left[i] = 0.5f * (outL + outR);       // one shared buffer: write the mono fold-down
            }
            else
            {
                left[i]  = outL;
                right[i] = outR;
            }
        }

        controlCountdown -= end - pos;
        pos = end;
    }

    publishActivity (amount, widthMul);
}

} // namespace frisson
