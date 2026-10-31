// <plugin>_render <outDir> [--scale <f>]
//
// Headless design-review tool for the plugins built on ap::ui::EditorBase
// (Strata, VoxNote, Intone, Ducker, Frisson). It creates the plugin's
// processor, plays a short demo signal through it (a hummed melody, a chord,
// a beat...) while ticking the editor's displays like its timer would, and
// writes <outDir>/<plugin>@1x.png and @2x.png (and @<f>x with --scale).
// AP_RENDER_DUMP=1 in the environment also prints the component tree with bounds.
//
// Compiled once per plugin with that plugin's own sources (see CMakeLists.txt);
// the demo is picked from the processor's name. Not part of the release zips.

#include <juce_audio_processors/juce_audio_processors.h>

#include <ap_ui/EditorBase.h>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <typeinfo>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;
    juce::String plugin;   // the processor's name, set in main()

    struct PlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (120.0);
            p.setTimeInSamples (samples);
            p.setPpqPosition ((double) samples / kRate * 2.0);
            p.setIsPlaying (true);
            return p;
        }
        juce::int64 samples = 0;
    };

    /** A hummed melody (vibrato, jitter, soft attacks), 0.35 s notes with short gaps. */
    struct Hum
    {
        explicit Hum (float detuneCents = 0.0f) : detune (detuneCents) {}

        float next()
        {
            static const int melody[] = { 57, 60, 62, 64, 62, 60, 64, 67, 65, 64 };
            const double noteLen = 0.42, gap = 0.08;
            const double t = (double) n++ / kRate;
            const int idx = (int) (t / (noteLen + gap)) % (int) std::size (melody);
            const double local = std::fmod (t, noteLen + gap);
            noise = 0.999f * noise + 0.001f * gauss (rng);
            if (local > noteLen) return 0.0005f * gauss (rng);
            const double cents = detune + 22.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t) + 600.0 * noise;
            phase += 440.0 * std::pow (2.0, (melody[idx] - 69 + cents / 100.0) / 12.0) / kRate;
            const double env = std::min ({ 1.0, local / 0.04, (noteLen - local) / 0.05 });
            double s = 0.0;
            const double harm[] = { 1.0, 0.45, 0.2, 0.08 };
            for (int h = 0; h < 4; ++h) s += harm[h] * std::sin (2.0 * juce::MathConstants<double>::pi * phase * (h + 1));
            return (float) (0.12 * env * s) + 0.0005f * gauss (rng);
        }

        float detune;
        juce::int64 n = 0;
        double phase = 0.0;
        float noise = 0.0f;
        std::mt19937 rng { 7 };
        std::normal_distribution<float> gauss { 0.0f, 1.0f };
    };

    /** A small mix: kick on every beat, a bass line and a pad (for the dynamics / enhancer plugins). */
    struct Beat
    {
        float next (int channel)
        {
            const double t = (double) n / kRate;
            if (channel == 0) ++n;
            const double beat = t * 2.0;                        // 120 bpm
            const double inBeat = (beat - std::floor (beat)) / 2.0;
            const double kick = std::exp (-inBeat * 28.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * inBeat + 90.0 * (1.0 - std::exp (-inBeat * 40.0)) / 40.0) * 1.0);
            const double bass = 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t) * (0.6 + 0.4 * std::sin (juce::MathConstants<double>::pi * beat));
            double pad = 0.0;
            for (double f : { 220.0, 277.18, 329.63, 440.0 })
                pad += 0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t * (channel == 0 ? 1.0 : 1.002));
            const double hat = (std::fmod (beat * 2.0, 1.0) < 0.04 ? 0.08 : 0.0) * gauss (rng);
            return (float) (0.55 * kick + bass + pad + hat);
        }

        juce::int64 n = 0;
        std::mt19937 rng { 3 };
        std::normal_distribution<double> gauss { 0.0, 1.0 };
    };

    void setParam (juce::AudioProcessor& p, const juce::String& name, const juce::String& text)
    {
        for (auto* prm : p.getParameters())
            if (prm->getName (64) == name)
            {
                prm->setValueNotifyingHost (prm->getValueForText (text));
                return;
            }
        std::printf ("  (no parameter '%s')\n", name.toRawUTF8());
    }

    void tickEditor (juce::AudioProcessorEditor& ed)
    {
        if (auto* base = dynamic_cast<ap::ui::EditorBase*> (&ed))
            base->tick();
    }

    /** Plays the plugin's demo for `seconds`, ticking the editor every ~33 ms. */
    void runDemo (juce::AudioProcessor& p, juce::AudioProcessorEditor& ed, PlayHead& head, double seconds)
    {
        const int numCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 1);
        juce::AudioBuffer<float> buf (numCh, kBlock);
        Hum hum (plugin == "Intone" ? 28.0f : 0.0f);
        Beat beat;
        const int blocks = (int) (seconds * kRate / kBlock);
        double sinceTick = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            buf.clear();
            if (plugin == "Strata")
            {
                // A held minor-ninth chord, re-struck every 2 s.
                if (b % (int) (2.0 * kRate / kBlock) == 0)
                    for (int n : { 45, 57, 60, 64, 71 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            }
            else
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    if (plugin == "VoxNote" || plugin == "Intone")
                    {
                        const float s = hum.next();
                        for (int ch = 0; ch < numCh; ++ch) buf.setSample (ch, i, s);
                    }
                    else
                    {
                        for (int ch = 0; ch < numCh; ++ch) buf.setSample (ch, i, beat.next (ch) * 0.5f);
                    }
                }
            }
            p.processBlock (buf, midi);
            head.samples += kBlock;
            sinceTick += kBlock / kRate;
            if (sinceTick >= 1.0 / 30.0)
            {
                sinceTick = 0.0;
                tickEditor (ed);
            }
        }
    }

    bool writePng (juce::Component& c, const juce::File& file, float scale)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale, juce::SoftwareImageType());
        file.deleteFile();
        juce::PNGImageFormat png;
        bool ok = false;
        if (auto stream = file.createOutputStream())
            ok = image.isValid() && png.writeImageToStream (image, *stream);
        std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
        return ok;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <outDir> [--scale <f>]\n", argv[0]);
        return 2;
    }
    float extraScale = 0.0f;
    for (int i = 2; i < argc; ++i)
        if (juce::String (argv[i]) == "--scale" && i + 1 < argc)
            extraScale = juce::String (argv[++i]).getFloatValue();

    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1]));
    if (! outDir.createDirectory())
    {
        std::printf ("cannot create %s\n", outDir.getFullPathName().toRawUTF8());
        return 1;
    }

    int failures = 0;
    {
        std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
        plugin = processor->getName();
        PlayHead head;
        processor->setPlayHead (&head);
        processor->setRateAndBufferSizeDetails (kRate, kBlock);   // as a host does before prepareToPlay
        processor->prepareToPlay (kRate, kBlock);

        // Showcase settings so the displays have something to show.
        if (plugin == "VoxNote")
        {
            setParam (*processor, "Snap To Scale", "On");
            setParam (*processor, "Scale", "Major");
        }
        else if (plugin == "Intone")
        {
            setParam (*processor, "Scale", "Major");
            setParam (*processor, "Retune Speed", "25 ms");
        }
        else if (plugin == "Ducker")
        {
            setParam (*processor, "Mode", "Ghost (tempo)");
        }
        else if (plugin == "Frisson")
        {
            for (auto* n : { "WEIGHT", "Weight", "RUSH", "Rush", "SHINE", "Shine", "GROOVE", "Groove" })
                for (auto* prm : processor->getParameters())
                    if (prm->getName (64) == n) prm->setValueNotifyingHost (0.65f);
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
        if (editor == nullptr)
        {
            std::printf ("no editor\n");
            return 1;
        }

        runDemo (*processor, *editor, head, plugin == "Strata" ? 1.2 : 4.0);

        if (std::getenv ("AP_RENDER_DUMP") != nullptr)
        {
            std::function<void (juce::Component&, int)> dump = [&] (juce::Component& c, int depth)
            {
                const auto b = c.getBoundsInParent();
                std::printf ("%*s%s %d,%d %dx%d alpha %.2f opaque %d\n", depth * 2, "", typeid (c).name(),
                             b.getX(), b.getY(), b.getWidth(), b.getHeight(), c.getAlpha(), c.isOpaque() ? 1 : 0);
                for (auto* child : c.getChildren()) dump (*child, depth + 1);
            };
            dump (*editor, 0);
        }

        const auto name = plugin.toLowerCase();
        for (float scale : { 1.0f, 2.0f })
            if (! writePng (*editor, outDir.getChildFile (name + "@" + juce::String ((int) scale) + "x.png"), scale))
                ++failures;

        if (extraScale > 0.0f && editor->isResizable())
        {
            const int w = editor->getWidth(), h = editor->getHeight();
            editor->setSize (juce::roundToInt ((float) w * extraScale), juce::roundToInt ((float) h * extraScale));
            runDemo (*processor, *editor, head, 0.2);
            if (! writePng (*editor, outDir.getChildFile (name + "@" + juce::String (extraScale, 2) + "x.png"), 1.0f))
                ++failures;
        }

        processor->editorBeingDeleted (editor.get());
        editor.reset();
        processor->releaseResources();
    }
    return failures == 0 ? 0 : 1;
}
