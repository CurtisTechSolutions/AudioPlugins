#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "engine/Patch.h"
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
    analysis, paint -> wavetable conversion and the sound map.

    Every callback runs on the message thread and never after this object is
    destroyed or after cancelAll() (callbacks of jobs queued before cancelAll()
    are dropped; the running job is told to stop where it can). Progress
    callbacks are throttled to at most 20 per second. The destructor cancels
    everything and waits at most kStopTimeoutMs for the worker.
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
        to mono (at most the first 60 s) and runs sample-to-patch on it. Nothing is applied: the caller decides
        (Apply = applyPatch (patchFromSampleMatch (result, processor.currentPatch()), false); Discard). */
    void analyzeSample (const juce::File&, std::function<void (float progress)> progress,
                        std::function<void (const verdant::SampleMatchResult&, juce::String error)> done);

    /** Converts a painted canvas into oscillator `osc`'s user table (source Painted, canvas kept in the patch)
        and selects "User". */
    void setPaintCanvas (int osc, const verdant::PaintCanvas&, std::function<void()> done);

    /** Renders a C3 note (1 s on, 2 s total, 44.1 kHz) for every preset (factory + user, in
        PresetManager::entries() order, so SoundMapPoint::presetIndex is an entry index) and lays them out on
        the 2D map. The result is cached in soundMapCacheFile(); a matching cache is loaded instead of
        rendering. */
    void buildSoundMap (std::function<void (std::vector<verdant::SoundMapPoint>)> done);

    /** As above, with progress (0..1) and an option to ignore (and replace) the cache. */
    void buildSoundMap (std::function<void (float progress)> progress,
                        std::function<void (std::vector<verdant::SoundMapPoint>)> done,
                        bool ignoreCache = false);

    /** True when a sound-map cache for the current preset list exists (message thread). */
    bool hasCachedSoundMap() const;

    /** <userApplicationData>/Curtis Tech Solutions/Verdant/soundmap-<hash>.json for the current preset list
        (hash of a format version, the factory bank and the user presets' names / sizes / dates). */
    juce::File soundMapCacheFile() const;

    /** Drops queued jobs, stops the running one where it can and suppresses every pending callback. */
    void cancelAll();

    /** True while a job is queued or running. */
    bool isBusy() const noexcept;

    static constexpr int kStopTimeoutMs = 4000;

private:
    struct Shared
    {
        std::atomic<bool> alive { true };
        std::atomic<int> generation { 0 };
    };

    void run() override;
    void enqueue (std::function<void()> job);
    void deliver (int generation, std::function<void()> callback) const;
    bool cancelled (int generation) const;

    Processor& processor;
    juce::CriticalSection queueLock;
    std::deque<std::function<void()>> queue;
    juce::WaitableEvent wake;
    std::atomic<int> pending { 0 };          // queued + running jobs
    std::shared_ptr<Shared> shared;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackgroundTasks)
};

/** The patch the sample-to-patch result describes: `base`'s extras (macro names, LFO shapes, Osc B's table,
    seed) with the result's parameter values, the matched table installed as Osc A's user table (source
    Matched, Osc A on and set to "User"). Named `name`, else after the table, else "Sample Match";
    category "User". */
verdant::Patch patchFromSampleMatch (const verdant::SampleMatchResult&, const verdant::Patch& base,
                                     const juce::String& name = {});

/** A user table holding a copy of a built wavetable's level-0 frames. */
verdant::UserTable userTableFromWavetable (const verdant::Wavetable&, verdant::UserTable::Source, const juce::String& name);

} // namespace verdant
