#include "BackgroundTasks.h"

#include "PluginProcessor.h"
#include "PresetManager.h"

#include "engine/Engine.h"
#include "engine/FactoryPresets.h"
#include "engine/WavetableFactory.h"
#include "engine/WavetableIO.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr double kMaxSampleSeconds = 60.0;
    constexpr juce::int64 kMaxWavetableFileBytes = 64 * 1024 * 1024;
    constexpr double kProgressIntervalMs = 50.0;   // <= 20 progress callbacks per second

    // Sound-map previews (W-F's SoundMap assumes 44.1 kHz renders): C3, 1 s held, 2 s total.
    constexpr int kPreviewNote = 48;
    constexpr float kPreviewNoteSeconds = 1.0f, kPreviewTotalSeconds = 2.0f;
    constexpr double kPreviewSampleRate = 44100.0;
    constexpr int kSoundMapFormat = 1;

    /** Lets a progress value through at most every 50 ms (and always the final 1.0). */
    struct ProgressThrottle
    {
        bool shouldSend (float p) noexcept
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (p >= 1.0f || now - lastMs >= kProgressIntervalMs)
            {
                lastMs = now;
                return true;
            }
            return false;
        }
        double lastMs = -1.0e9;
    };

    juce::File soundMapFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Curtis Tech Solutions")
                   .getChildFile ("Verdant");
    }

    /** 64-bit FNV-1a, stable across platforms and runs (String::hashCode64 is not documented as stable). */
    struct Fnv64
    {
        void add (const void* data, size_t n) noexcept
        {
            const auto* b = static_cast<const uint8_t*> (data);
            for (size_t i = 0; i < n; ++i)
            {
                h ^= b[i];
                h *= 0x100000001B3ull;
            }
        }
        void add (const std::string& s) noexcept   { add (s.data(), s.size()); add ("\n", 1); }
        void add (const juce::String& s) noexcept  { add (s.toStdString()); }
        uint64_t h = 0xCBF29CE484222325ull;
    };

    juce::var featuresToVar (const SoundMapPoint& p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("preset", p.presetIndex);
        o->setProperty ("x", (double) p.x);
        o->setProperty ("y", (double) p.y);
        o->setProperty ("centroidHz", (double) p.features.centroidHz);
        o->setProperty ("attackMs", (double) p.features.attackMs);
        o->setProperty ("noisiness", (double) p.features.noisiness);
        o->setProperty ("movement", (double) p.features.movement);
        o->setProperty ("loudness", (double) p.features.loudness);
        return juce::var (o);
    }

    bool readCache (const juce::File& file, const juce::String& key, int expectedCount, std::vector<SoundMapPoint>& out)
    {
        if (! file.existsAsFile() || file.getSize() > 16 * 1024 * 1024)
            return false;
        const auto json = juce::JSON::parse (file);
        if (! json.isObject() || (int) json["format"] != kSoundMapFormat || json["key"].toString() != key)
            return false;
        const auto* points = json["points"].getArray();
        if (points == nullptr || points->size() != expectedCount)
            return false;

        std::vector<SoundMapPoint> result;
        result.reserve ((size_t) expectedCount);
        for (const auto& v : *points)
        {
            const auto num = [&v] (const char* name, bool& ok)
            {
                const auto& x = v[name];
                ok = ok && (x.isDouble() || x.isInt() || x.isInt64()) && std::isfinite ((double) x);
                return (float) (double) x;
            };
            bool ok = v.isObject();
            SoundMapPoint p {};
            p.presetIndex = (int) v["preset"];
            p.x = num ("x", ok);
            p.y = num ("y", ok);
            p.features.centroidHz = num ("centroidHz", ok);
            p.features.attackMs = num ("attackMs", ok);
            p.features.noisiness = num ("noisiness", ok);
            p.features.movement = num ("movement", ok);
            p.features.loudness = num ("loudness", ok);
            if (! ok || p.presetIndex < 0 || p.presetIndex >= expectedCount)
                return false;
            result.push_back (p);
        }
        out = std::move (result);
        return true;
    }

    void writeCache (const juce::File& file, const juce::String& key, const std::vector<SoundMapPoint>& points)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", kSoundMapFormat);
        root->setProperty ("key", key);
        juce::Array<juce::var> list;
        for (const auto& p : points)
            list.add (featuresToVar (p));
        root->setProperty ("points", list);

        const auto dir = file.getParentDirectory();
        if (! dir.createDirectory())
            return;
        // One cache at a time: drop maps of older preset lists.
        for (const auto& old : dir.findChildFiles (juce::File::findFiles, false, "soundmap-*.json"))
            if (old != file)
                old.deleteFile();
        file.replaceWithText (juce::JSON::toString (juce::var (root)));   // via a temporary file
    }
}

//==============================================================================
UserTable userTableFromWavetable (const Wavetable& t, UserTable::Source source, const juce::String& name)
{
    UserTable u;
    u.name = name.toStdString();
    u.numFrames = t.numFrames();
    u.source = source;
    u.frames.resize ((size_t) u.numFrames * (size_t) kFrameSize);
    for (int f = 0; f < u.numFrames; ++f)
        std::copy (t.frame (f), t.frame (f) + kFrameSize, u.frames.begin() + (std::ptrdiff_t) f * kFrameSize);
    return u;
}

Patch patchFromSampleMatch (const SampleMatchResult& result, const Patch& base, const juce::String& name)
{
    Patch p = base;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = result.values[(size_t) i];
        p.values[(size_t) i] = std::isfinite (v) ? clampToRange ((P) i, v) : info ((P) i).def;
    }

    juce::String patchName = name.trim();
    if (patchName.isEmpty() && result.table != nullptr)
        patchName = juce::String::fromUTF8 (result.table->name().c_str()).trim();
    if (patchName.isEmpty())
        patchName = "Sample Match";

    if (result.table != nullptr && result.table->numFrames() > 0)
    {
        p.userTables[0] = userTableFromWavetable (*result.table, UserTable::Source::Matched, patchName);
        p.values[(size_t) oscParam (0, OscParam::Table)] = (float) kNumFactoryTables;   // "User"
        p.values[(size_t) oscParam (0, OscParam::On)] = 1.0f;
    }

    p.name = patchName.toStdString();
    p.category = "User";
    p.tags.clear();
    return p;
}

//==============================================================================
BackgroundTasks::BackgroundTasks (Processor& p)
    : juce::Thread ("Verdant background tasks"),
      processor (p),
      shared (std::make_shared<Shared>())
{
}

BackgroundTasks::~BackgroundTasks()
{
    shared->alive.store (false);
    cancelAll();
    signalThreadShouldExit();
    wake.signal();
    stopThread (kStopTimeoutMs);   // jobs poll for cancellation, so this returns quickly
}

void BackgroundTasks::enqueue (std::function<void()> job)
{
    pending.fetch_add (1);
    {
        const juce::ScopedLock sl (queueLock);
        queue.push_back (std::move (job));
    }
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    wake.signal();
}

void BackgroundTasks::deliver (int gen, std::function<void()> callback) const
{
    if (callback == nullptr)
        return;
    // Only `shared` (never `this`) is touched until it says the tasks object is alive and not cancelled.
    juce::MessageManager::callAsync ([state = shared, gen, cb = std::move (callback)]
    {
        if (state->alive.load() && state->generation.load() == gen)
            cb();
    });
}

bool BackgroundTasks::cancelled (int gen) const
{
    return threadShouldExit() || shared->generation.load() != gen;
}

void BackgroundTasks::cancelAll()
{
    shared->generation.fetch_add (1);
    int dropped = 0;
    {
        const juce::ScopedLock sl (queueLock);
        dropped = (int) queue.size();
        queue.clear();
    }
    pending.fetch_sub (dropped);
}

bool BackgroundTasks::isBusy() const noexcept
{
    return pending.load() > 0;
}

void BackgroundTasks::run()
{
    while (! threadShouldExit())
    {
        std::function<void()> job;
        {
            const juce::ScopedLock sl (queueLock);
            if (! queue.empty())
            {
                job = std::move (queue.front());
                queue.pop_front();
            }
        }

        if (job != nullptr)
        {
            job();
            pending.fetch_sub (1);
        }
        else
        {
            wake.wait (500);
        }
    }
}

//==============================================================================
void BackgroundTasks::importWavetable (const juce::File& file, int osc, std::function<void (juce::String)> done)
{
    const int gen = shared->generation.load();
    enqueue ([this, file, osc, gen, done = std::move (done)]
    {
        juce::String error;
        std::shared_ptr<Wavetable> table;
        juce::MemoryBlock data;

        if (osc < 0 || osc > 1)
            error = "Unknown oscillator";
        else if (! file.existsAsFile())
            error = "Could not find " + file.getFileName();
        else if (file.getSize() > kMaxWavetableFileBytes)
            error = file.getFileName() + " is too large for a wavetable";
        else if (! file.loadFileAsData (data))
            error = "Could not read " + file.getFileName();
        else
        {
            auto result = importWavetableWav (static_cast<const uint8_t*> (data.getData()), data.getSize(),
                                              file.getFileNameWithoutExtension().toStdString());
            if (! result.error.empty())                                    error = juce::String::fromUTF8 (result.error.c_str());
            else if (result.table == nullptr || result.table->numFrames() <= 0) error = "No wavetable frames found in " + file.getFileName();
            else                                                           table = std::move (result.table);
        }

        if (cancelled (gen))
            return;

        deliver (gen, [this, osc, table, error, done, name = file.getFileNameWithoutExtension()]
        {
            if (table != nullptr)
                processor.setUserTable (osc, userTableFromWavetable (*table, UserTable::Source::Imported, name), table);
            if (done != nullptr) done (error);
        });
    });
}

void BackgroundTasks::analyzeSample (const juce::File& file, std::function<void (float)> progress,
                                     std::function<void (const SampleMatchResult&, juce::String)> done)
{
    const int gen = shared->generation.load();
    enqueue ([this, file, gen, progress = std::move (progress), done = std::move (done)]
    {
        juce::String error;
        std::vector<float> mono;
        double sampleRate = 0.0;

        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (file.existsAsFile() ? formats.createReaderFor (file) : nullptr);
            if (reader == nullptr || ! (reader->sampleRate > 0.0) || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
            {
                error = "Could not decode " + file.getFileName();
            }
            else
            {
                sampleRate = reader->sampleRate;
                const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (kMaxSampleSeconds * sampleRate));
                const int channels = (int) juce::jmin (reader->numChannels, 8u);
                juce::AudioBuffer<float> buffer (channels, length);
                if (! reader->read (&buffer, 0, length, 0, true, channels > 1))
                {
                    error = "Could not decode " + file.getFileName();
                }
                else
                {
                    mono.assign ((size_t) length, 0.0f);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const float* src = buffer.getReadPointer (ch);
                        for (int i = 0; i < length; ++i)
                            mono[(size_t) i] += src[i] / (float) channels;
                    }
                    for (auto& s : mono)
                        if (! std::isfinite (s)) s = 0.0f;
                }
            }
        }

        if (cancelled (gen))
            return;

        SampleMatchResult result;
        result.values = defaultValues();
        if (error.isEmpty())
        {
            ProgressThrottle throttle;
            result = matchSample (mono.data(), (int) mono.size(), sampleRate, [&] (float p)
            {
                if (cancelled (gen))
                    return false;
                if (progress != nullptr && throttle.shouldSend (p))
                    deliver (gen, [progress, p] { progress (juce::jlimit (0.0f, 1.0f, p)); });
                return true;
            });
            if (cancelled (gen))
                return;
            if (result.table == nullptr && result.notes.empty())
                error = "No usable sound found in " + file.getFileName();
        }

        if (progress != nullptr && error.isEmpty())
            deliver (gen, [progress] { progress (1.0f); });
        deliver (gen, [result = std::move (result), error, done] { if (done != nullptr) done (result, error); });
    });
}

void BackgroundTasks::setPaintCanvas (int osc, const PaintCanvas& canvas, std::function<void()> done)
{
    const int gen = shared->generation.load();
    enqueue ([this, osc, canvas, gen, done = std::move (done)]
    {
        const bool valid = osc >= 0 && osc <= 1 && canvas.cols > 0 && canvas.rows > 0
                        && canvas.amp.size() >= (size_t) canvas.cols * (size_t) canvas.rows;
        std::shared_ptr<Wavetable> table = valid ? canvasToWavetable (canvas, "Painted") : nullptr;
        if (cancelled (gen))
            return;

        deliver (gen, [this, osc, canvas, table, done]
        {
            if (table != nullptr && table->numFrames() > 0)
            {
                auto user = userTableFromWavetable (*table, UserTable::Source::Painted, "Painted");
                user.canvas = canvas;
                processor.setUserTable (osc, user, table);
            }
            if (done != nullptr) done();
        });
    });
}

//==============================================================================
juce::File BackgroundTasks::soundMapCacheFile() const
{
    Fnv64 hash;
    hash.add ("verdant-soundmap " + std::to_string (kSoundMapFormat) + " note " + std::to_string (kPreviewNote)
              + " " + std::to_string (kPreviewNoteSeconds) + "/" + std::to_string (kPreviewTotalSeconds)
              + " @ " + std::to_string (kPreviewSampleRate));

    const auto& bank = factoryPresets();
    hash.add ("factory " + std::to_string (bank.size()));
    for (const auto& p : bank)
        hash.add (serializePatch (p));   // the factory sound itself, so a changed bank invalidates the cache

    for (const auto& e : processor.presets().entries())
        if (! e.factory)
            hash.add (e.category + "/" + e.name + " " + juce::String (e.file.getSize())
                      + " " + juce::String (e.file.getLastModificationTime().toMilliseconds()));

    return soundMapFolder().getChildFile ("soundmap-" + juce::String::toHexString ((juce::int64) hash.h).paddedLeft ('0', 16) + ".json");
}

bool BackgroundTasks::hasCachedSoundMap() const
{
    return soundMapCacheFile().existsAsFile();
}

void BackgroundTasks::buildSoundMap (std::function<void (std::vector<SoundMapPoint>)> done)
{
    buildSoundMap (nullptr, std::move (done), false);
}

void BackgroundTasks::buildSoundMap (std::function<void (float)> progress,
                                     std::function<void (std::vector<SoundMapPoint>)> done, bool ignoreCache)
{
    struct Source { bool factory; int factoryIndex; juce::File file; };
    std::vector<Source> sources;
    for (const auto& e : processor.presets().entries())
        sources.push_back ({ e.factory, e.factoryIndex, e.file });

    const auto cacheFile = soundMapCacheFile();
    const auto key = cacheFile.getFileNameWithoutExtension();
    const int gen = shared->generation.load();

    enqueue ([this, sources, cacheFile, key, ignoreCache, gen, progress = std::move (progress), done = std::move (done)]
    {
        const int count = (int) sources.size();
        std::vector<SoundMapPoint> points;

        if (! ignoreCache && readCache (cacheFile, key, count, points))
        {
            if (progress != nullptr) deliver (gen, [progress] { progress (1.0f); });
            deliver (gen, [points = std::move (points), done] { if (done != nullptr) done (points); });
            return;
        }

        std::vector<Patch> patches;
        patches.reserve (sources.size());
        const auto& bank = factoryPresets();
        for (const auto& s : sources)
        {
            Patch p;   // a placeholder keeps the indices aligned with the entries if a file fails to load
            if (s.factory && s.factoryIndex >= 0 && s.factoryIndex < (int) bank.size())
                p = bank[(size_t) s.factoryIndex];
            else if (s.file.existsAsFile() && s.file.getSize() < 64 * 1024 * 1024)
                if (const auto xml = juce::parseXML (s.file))
                    patchFromXml (*xml, p);
            patches.push_back (std::move (p));
            if (cancelled (gen))
                return;
        }

        const auto previewLength = (size_t) std::lround ((double) kPreviewTotalSeconds * kPreviewSampleRate);
        ProgressThrottle throttle;
        int rendered = 0;
        points = verdant::buildSoundMap (patches, [&] (const Patch& p)
        {
            if (cancelled (gen))
                return std::vector<float> (previewLength, 0.0f);   // finish quickly; the result is dropped
            auto audio = renderPreview (p, kPreviewNote, kPreviewNoteSeconds, kPreviewTotalSeconds, kPreviewSampleRate);
            ++rendered;
            const float fraction = count > 0 ? (float) rendered / (float) count : 1.0f;
            if (progress != nullptr && throttle.shouldSend (juce::jmin (fraction, 0.99f)))
                deliver (gen, [progress, fraction] { progress (juce::jmin (fraction, 0.99f)); });
            return audio;
        });

        if (cancelled (gen))
            return;

        writeCache (cacheFile, key, points);
        if (progress != nullptr) deliver (gen, [progress] { progress (1.0f); });
        deliver (gen, [points = std::move (points), done] { if (done != nullptr) done (points); });
    });
}

} // namespace verdant
