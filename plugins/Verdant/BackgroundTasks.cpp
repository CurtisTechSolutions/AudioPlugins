#include "BackgroundTasks.h"

#include "PluginProcessor.h"
#include "PresetManager.h"

#include "engine/Engine.h"
#include "engine/FactoryPresets.h"
#include "engine/WavetableIO.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

namespace verdant
{

namespace
{
    UserTable userTableFrom (const Wavetable& t, UserTable::Source source, const juce::String& name)
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

    constexpr double kMaxSampleSeconds = 60.0;
}

BackgroundTasks::BackgroundTasks (Processor& p)
    : juce::Thread ("Verdant background tasks"),
      processor (p),
      alive (std::make_shared<std::atomic<bool>> (true))
{
}

BackgroundTasks::~BackgroundTasks()
{
    alive->store (false);
    cancelAll();
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);
}

void BackgroundTasks::enqueue (std::function<void()> job)
{
    {
        const juce::ScopedLock sl (queueLock);
        queue.push_back (std::move (job));
    }
    if (! isThreadRunning())
        startThread();
    wake.signal();
}

void BackgroundTasks::deliver (int gen, std::function<void()> callback)
{
    juce::MessageManager::callAsync ([token = alive, gen, this, cb = std::move (callback)]
    {
        if (token->load() && generation.load() == gen && cb != nullptr)   // `this` is only touched while alive
            cb();
    });
}

void BackgroundTasks::cancelAll()
{
    generation.fetch_add (1);
    const juce::ScopedLock sl (queueLock);
    queue.clear();
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

        if (job != nullptr) job();
        else wake.wait (500);
    }
}

//==============================================================================
void BackgroundTasks::importWavetable (const juce::File& file, int osc, std::function<void (juce::String)> done)
{
    const int gen = generation.load();
    enqueue ([this, file, osc, gen, done = std::move (done)]
    {
        juce::String error;
        std::shared_ptr<Wavetable> table;
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data))
        {
            error = "Could not read " + file.getFileName();
        }
        else
        {
            auto result = importWavetableWav (static_cast<const uint8_t*> (data.getData()), data.getSize(),
                                              file.getFileNameWithoutExtension().toStdString());
            if (! result.error.empty())       error = juce::String::fromUTF8 (result.error.c_str());
            else if (result.table == nullptr) error = "No wavetable frames found in " + file.getFileName();
            else                              table = std::move (result.table);
        }

        deliver (gen, [this, osc, table, error, done, name = file.getFileNameWithoutExtension()]
        {
            if (table != nullptr)
                processor.setUserTable (osc, userTableFrom (*table, UserTable::Source::Imported, name), table);
            if (done != nullptr) done (error);
        });
    });
}

void BackgroundTasks::analyzeSample (const juce::File& file, std::function<void (float)> progress,
                                     std::function<void (const SampleMatchResult&, juce::String)> done)
{
    const int gen = generation.load();
    enqueue ([this, file, gen, progress = std::move (progress), done = std::move (done)]
    {
        juce::String error;
        std::vector<float> mono;
        double sampleRate = 0.0;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            error = "Could not decode " + file.getFileName();
        }
        else
        {
            sampleRate = reader->sampleRate;
            const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (kMaxSampleSeconds * sampleRate));
            const int channels = juce::jmax (1, (int) reader->numChannels);
            juce::AudioBuffer<float> buffer (channels, length);
            reader->read (&buffer, 0, length, 0, true, true);
            mono.assign ((size_t) length, 0.0f);
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* src = buffer.getReadPointer (ch);
                for (int i = 0; i < length; ++i)
                    mono[(size_t) i] += src[i] / (float) channels;
            }
        }

        SampleMatchResult result;
        if (error.isEmpty())
        {
            float lastSent = -1.0f;
            result = matchSample (mono.data(), (int) mono.size(), sampleRate, [&] (float p)
            {
                if (threadShouldExit() || generation.load() != gen)
                    return false;
                if (p - lastSent >= 0.01f || p >= 1.0f)
                {
                    lastSent = p;
                    deliver (gen, [progress, p] { if (progress != nullptr) progress (p); });
                }
                return true;
            });
        }

        deliver (gen, [result, error, done] { if (done != nullptr) done (result, error); });
    });
}

void BackgroundTasks::setPaintCanvas (int osc, const PaintCanvas& canvas, std::function<void()> done)
{
    const int gen = generation.load();
    enqueue ([this, osc, canvas, gen, done = std::move (done)]
    {
        std::shared_ptr<Wavetable> table = canvasToWavetable (canvas, "Painted");
        deliver (gen, [this, osc, canvas, table, done]
        {
            if (table != nullptr)
            {
                auto user = userTableFrom (*table, UserTable::Source::Painted, "Painted");
                user.canvas = canvas;
                processor.setUserTable (osc, user, table);
            }
            if (done != nullptr) done();
        });
    });
}

void BackgroundTasks::buildSoundMap (std::function<void (std::vector<SoundMapPoint>)> done)
{
    struct Source { bool factory; int factoryIndex; juce::File file; };
    std::vector<Source> sources;
    for (const auto& e : processor.presets().entries())
        sources.push_back ({ e.factory, e.factoryIndex, e.file });

    const int gen = generation.load();
    enqueue ([this, sources, gen, done = std::move (done)]
    {
        std::vector<Patch> patches;
        patches.reserve (sources.size());
        const auto& bank = factoryPresets();
        for (const auto& s : sources)
        {
            Patch p;   // a placeholder keeps the indices aligned with the entries if a file fails to load
            if (s.factory && s.factoryIndex >= 0 && s.factoryIndex < (int) bank.size())
                p = bank[(size_t) s.factoryIndex];
            else if (const auto xml = juce::parseXML (s.file))
                patchFromXml (*xml, p);
            patches.push_back (std::move (p));
        }

        auto points = verdant::buildSoundMap (patches, [this, gen] (const Patch& p)
        {
            if (threadShouldExit() || generation.load() != gen)
                return std::vector<float>();
            return renderPreview (p, 48, 1.0f, 1.5f, 44100.0);
        });

        deliver (gen, [points = std::move (points), done]
        {
            if (done != nullptr) done (points);
        });
    });
}

} // namespace verdant
