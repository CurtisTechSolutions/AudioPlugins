#pragma once

#include "Math.h"
#include "PitchDetector.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ap
{

/**
    Streams audio through a YinPitchDetector at a fixed hop, producing one
    PitchFrame per hop with frequency, confidence, level and an absolute
    timestamp (in samples, counted from prepare()).

    Includes a light octave-error guard: a 3-frame median on the MIDI value
    (adds one hop of latency to the reported value, timestamp is adjusted).
*/
class PitchTracker
{
public:
    struct Frame
    {
        int64_t time       = 0;      // absolute sample index this estimate describes (window centre)
        float   frequency  = 0.0f;   // Hz, 0 if unvoiced
        float   midi       = 0.0f;   // fractional MIDI note (0 if unvoiced)
        float   confidence = 0.0f;
        float   rmsDb      = -120.0f;
        bool    voiced     = false;
    };

    void prepare (double sampleRate, int hopSize = 256, int windowSize = 1024,
                  float minHz = 50.0f, float maxHz = 1500.0f)
    {
        detector.prepare (sampleRate, windowSize, minHz, maxHz);
        hop = std::max (16, hopSize);
        const int need = detector.requiredSamples();
        fifo.assign ((size_t) need, 0.0f);
        scratch.assign ((size_t) need, 0.0f);
        fifoPos = 0;
        filled = 0;
        sinceLast = 0;
        writeTime = 0;
        rmsAcc = 0.0;
        rmsCount = 0;
        history.fill (Frame{});
        historyCount = 0;
    }

    void setThreshold (float t) noexcept      { detector.setThreshold (t); }
    void setMedianFilter (bool on) noexcept   { useMedian = on; }

    /** Absolute sample index of the newest sample pushed (the frame clock). */
    int64_t currentTime() const noexcept      { return writeTime; }

    /** Restart the stream at an externally supplied clock (e.g. another tracker's
        currentTime() when switching, or 0 after a host reset) so frame timestamps
        line up with whoever consumes them. Clears the analysis buffer. */
    void resync (int64_t time) noexcept
    {
        writeTime = time;
        fifoPos = 0;
        filled = 0;
        sinceLast = 0;
        rmsAcc = 0.0;
        rmsCount = 0;
        historyCount = 0;
    }

    int hopSize() const noexcept              { return hop; }

    /** Samples between the newest input sample and the time a fresh frame describes. */
    int latencySamples() const noexcept
    {
        return detector.requiredSamples() - detector.analysisCentre() + (useMedian ? hop : 0);
    }

    /** Push samples; `onFrame(const Frame&)` is called for each completed hop. */
    template <typename Callback>
    void process (const float* in, int numSamples, Callback&& onFrame)
    {
        const int need = (int) fifo.size();
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = in[i];
            fifo[(size_t) fifoPos] = s;
            fifoPos = (fifoPos + 1) % need;
            if (filled < need) ++filled;
            ++writeTime;

            rmsAcc += (double) s * s;
            ++rmsCount;

            if (++sinceLast >= hop)
            {
                sinceLast = 0;
                if (filled >= need)
                    emit (onFrame);
            }
        }
    }

private:
    template <typename Callback>
    void emit (Callback&& onFrame)
    {
        const int need = (int) fifo.size();
        // Linearise the ring so the oldest sample is first.
        for (int i = 0; i < need; ++i)
            scratch[(size_t) i] = fifo[(size_t) ((fifoPos + i) % need)];

        const auto res = detector.analyze (scratch.data());

        Frame f;
        f.time       = writeTime - need + detector.analysisCentre();
        f.frequency  = res.voiced ? res.frequency : 0.0f;
        f.midi       = res.voiced ? hzToMidi (res.frequency) : 0.0f;
        f.confidence = res.confidence;
        f.voiced     = res.voiced;
        f.rmsDb      = rmsCount > 0 ? gainToDb ((float) std::sqrt (rmsAcc / (double) rmsCount)) : -120.0f;
        rmsAcc = 0.0;
        rmsCount = 0;

        if (! useMedian)
        {
            onFrame (f);
            return;
        }

        // 3-point median on MIDI value; output the middle (previous) frame.
        history[2] = history[1];
        history[1] = history[0];
        history[0] = f;
        if (historyCount < 3) { ++historyCount; if (historyCount < 3) return; }

        Frame out = history[1];
        const int voicedCount = (history[0].voiced ? 1 : 0) + (history[1].voiced ? 1 : 0) + (history[2].voiced ? 1 : 0);
        if (voicedCount == 3)
        {
            const float a = history[0].midi, b = history[1].midi, c = history[2].midi;
            const float med = std::max (std::min (a, b), std::min (std::max (a, b), c));
            if (std::fabs (med - out.midi) > 1.0e-6f)
            {
                out.midi = med;
                out.frequency = midiToHz (med);
            }
        }
        onFrame (out);
    }

    YinPitchDetector detector;
    std::vector<float> fifo, scratch;
    int     hop = 256;
    int     fifoPos = 0, filled = 0, sinceLast = 0;
    int64_t writeTime = 0;
    double  rmsAcc = 0.0;
    int     rmsCount = 0;
    bool    useMedian = true;
    std::array<Frame, 3> history {};
    int     historyCount = 0;
};

/**
    Keeps recent PitchTracker frames so a consumer running behind the input
    (e.g. a pitch shifter with its own delay) can look up the estimate for
    the audio it is currently processing.
*/
class PitchTimeline
{
public:
    void clear() noexcept { count = 0; head = 0; }

    void push (const PitchTracker::Frame& f) noexcept
    {
        frames[(size_t) head] = f;
        head = (head + 1) % kCapacity;
        if (count < kCapacity) ++count;
    }

    /** The frame whose timestamp is closest to `time`, or nullptr when empty. */
    const PitchTracker::Frame* at (int64_t time) const noexcept
    {
        if (count == 0) return nullptr;
        const PitchTracker::Frame* best = nullptr;
        int64_t bestDist = INT64_MAX;
        for (int i = 0; i < count; ++i)
        {
            const auto& f = frames[(size_t) ((head - 1 - i + kCapacity * 2) % kCapacity)];
            const int64_t d = f.time > time ? f.time - time : time - f.time;
            if (d < bestDist) { bestDist = d; best = &f; }
            else if (f.time < time) break;    // frames are chronological; no closer one further back
        }
        return best;
    }

    const PitchTracker::Frame* latest() const noexcept
    {
        return count > 0 ? &frames[(size_t) ((head - 1 + kCapacity) % kCapacity)] : nullptr;
    }

private:
    static constexpr int kCapacity = 64;
    std::array<PitchTracker::Frame, kCapacity> frames {};
    int head = 0, count = 0;
};

} // namespace ap
