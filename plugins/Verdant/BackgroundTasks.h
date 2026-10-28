#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "engine/SampleMatch.h"
#include "engine/SoundMap.h"
#include "engine/SpectralPaint.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace verdant
{

class Processor;

/**
    Off-thread jobs on one worker thread (FIFO): wavetable import, sample-to-patch
    analysis, paint -> wavetable conversion and the sound map. Every callback runs
    on the message thread (juce::MessageManager::callAsync) and never after this
    object is destroyed or after cancelAll().
*/
class BackgroundTasks : private juce::Thread
{
public:
    explicit BackgroundTasks (Processor&);
    ~BackgroundTasks() override;

    /** Imports a WAV wavetable (incl. `clm ` chunks) into oscillator `osc` (0 = A, 1 = B) and selects "User".
        `error` is empty on success. */
    void importWavetable (const juce::File&, int osc, std::function<void (juce::String error)> done);

    /** Decodes an audio file (WAV / AIFF / FLAC / Ogg, plus the platform codecs, e.g. MP3 on macOS / Windows)
        to mono and runs sample-to-patch on it. Nothing is applied: the caller decides (Apply / Discard).
        `progress` (0..1) is throttled to ~1 % steps. */
    void analyzeSample (const juce::File&, std::function<void (float progress)> progress,
                        std::function<void (const verdant::SampleMatchResult&, juce::String error)> done);

    /** Converts a painted canvas into oscillator `osc`'s user table (kept in the patch) and selects "User". */
    void setPaintCanvas (int osc, const verdant::PaintCanvas&, std::function<void()> done);

    /** Renders a short note for every preset (factory + user, in PresetManager::entries() order, so
        SoundMapPoint::presetIndex is an entry index) and lays them out on the 2D map. */
    void buildSoundMap (std::function<void (std::vector<verdant::SoundMapPoint>)> done);

    /** Drops queued jobs, stops the running one where it can and suppresses every pending callback. */
    void cancelAll();

private:
    void run() override;
    void enqueue (std::function<void()> job);
    void deliver (int generation, std::function<void()> callback);

    Processor& processor;
    juce::CriticalSection queueLock;
    std::deque<std::function<void()>> queue;
    juce::WaitableEvent wake;
    std::atomic<int> generation { 0 };
    std::shared_ptr<std::atomic<bool>> alive;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackgroundTasks)
};

} // namespace verdant
