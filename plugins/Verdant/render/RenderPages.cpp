// verdant_render <outDir> [--scale <f>] [--demo] [--no-map]
//
// Headless (no display needed) design review tool. Creates the processor, loads
// factory preset 0, opens the editor without a window and writes PNGs:
//
//   <page>@1x.png, <page>@2x.png   osc, mod, fx, arpseq, breed, browse
//   paint@1x / @2x                 the OSC page with the spectral-paint panel open on Osc A
//   scope@1x / @2x                 the FX page with the output scope on while a note plays
//   spectrum@1x / @2x              the ARP/SEQ page with the scope in spectrum mode
//   <page>@<f>x.png                every page again at --scale f (the editor resized to f x base)
//
// Before rendering it breeds one generation (parents: presets 0 and 9) and builds
// the sound map synchronously from every preset (the same renders as
// BackgroundTasks::buildSoundMap, without touching its cache); --no-map skips that.
// --demo also sets up a showcase state (FX rack, sequencer pattern, arp, Sympathetic)
// and writes fx-<effect>@1x.png for every effect type.

#include "../BackgroundTasks.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"
#include "../ui/BrowsePage.h"
#include "../ui/FxPage.h"
#include "../ui/OscPage.h"
#include "../ui/PaintCanvas.h"
#include "../ui/Scope.h"

#include "engine/FactoryPresets.h"
#include "engine/SoundMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace
{
    template <typename T>
    T* findChild (juce::Component& root)
    {
        if (auto* t = dynamic_cast<T*> (&root))
            return t;
        for (auto* c : root.getChildren())
            if (auto* t = findChild<T> (*c))
                return t;
        return nullptr;
    }

    juce::Component* findById (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id)
            return &root;
        for (auto* c : root.getChildren())
            if (auto* t = findById (*c, id))
                return t;
        return nullptr;
    }

    juce::String scaleText (float s)
    {
        return std::abs (s - std::round (s)) < 1.0e-4f ? juce::String (juce::roundToInt (s)) : juce::String (s, 2).trimCharactersAtEnd ("0");
    }

    void setParam (verdant::Processor& p, verdant::P param, float plain)
    {
        if (auto* q = p.apvts.getParameter (verdant::info (param).id))
            q->setValueNotifyingHost (q->convertTo0to1 (plain));
    }

    /** Runs the synth for `blocks` blocks of 512 at 48 kHz, starting `note` (if >= 0) in the first one. */
    void play (verdant::Processor& p, int blocks, int note)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0 && note >= 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    }

    std::vector<verdant::SoundMapPoint> buildMap (verdant::Processor& p)
    {
        std::vector<verdant::Patch> patches;
        const int n = (int) p.presets().entries().size();
        for (int i = 0; i < n; ++i)
        {
            verdant::Patch patch;
            p.presets().readPatch (i, patch);
            patches.push_back (std::move (patch));
        }
        // Same note / length / rate as BackgroundTasks::buildSoundMap.
        return verdant::buildSoundMap (patches, [] (const verdant::Patch& patch)
        {
            return verdant::renderPreview (patch, 48, 1.0f, 2.0f, 44100.0);
        });
    }

    void demoState (verdant::Processor& p)
    {
        using verdant::P;
        const int fx[] = { 1, 2, 5, 6, 7, 0, 0, 0 };   // Distortion, Chorus, Delay, Reverb, EQ
        for (int s = 0; s < verdant::kNumFxSlots; ++s)
            setParam (p, verdant::fxSlotType (s), (float) fx[s]);
        setParam (p, P::EqLowGain, 4.5f);
        setParam (p, P::EqMidGain, -6.0f);
        setParam (p, P::EqMidFreq, 900.0f);
        setParam (p, P::EqHighGain, 3.0f);
        setParam (p, P::DistDrive, 55.0f);
        setParam (p, P::DistTone, 2400.0f);
        setParam (p, P::SympAmount, 45.0f);
        setParam (p, P::ArpOn, 1.0f);
        setParam (p, P::ArpMode, 2.0f);
        setParam (p, P::ArpOct, 2.0f);
        setParam (p, P::ArpSwing, 30.0f);
        setParam (p, P::SeqOn, 1.0f);
        setParam (p, P::SeqSteps, 12.0f);
        setParam (p, P::KeyRoot, 9.0f);
        setParam (p, P::KeyScale, 2.0f);
        const int pitch[16] = { 0, 0, 7, 3, 0, 12, 10, 7, 0, -5, 3, 5, 0, 0, 0, 0 };
        const float vel[16] = { 100, 60, 85, 55, 100, 70, 80, 50, 95, 60, 75, 65, 100, 100, 100, 100 };
        const float mod[16] = { 10, 30, 50, 70, 90, 70, 50, 30, 10, 40, 80, 20, 50, 50, 50, 50 };
        for (int s = 0; s < 16; ++s)
        {
            setParam (p, verdant::seqStepParam (s, verdant::SeqStepParam::Pitch), (float) pitch[s]);
            setParam (p, verdant::seqStepParam (s, verdant::SeqStepParam::Vel), vel[s]);
            setParam (p, verdant::seqStepParam (s, verdant::SeqStepParam::Mod), mod[s]);
            setParam (p, verdant::seqStepParam (s, verdant::SeqStepParam::Gate), s == 3 || s == 9 ? 0.0f : 1.0f);
        }
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: verdant_render <outDir> [--scale <f>] [--demo] [--no-map]\n");
        return 2;
    }

    float extraScale = 0.0f;
    bool demo = false, withMap = true;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--scale" && i + 1 < argc) extraScale = juce::String (argv[++i]).getFloatValue();
        else if (a == "--demo")             demo = true;
        else if (a == "--no-map")           withMap = false;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1]));
    if (! outDir.createDirectory())
    {
        std::printf ("cannot create %s\n", outDir.getFullPathName().toRawUTF8());
        return 1;
    }

    int failures = 0;
    {
        verdant::Processor processor;
        processor.setPlayConfigDetails (0, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        if (! processor.presets().entries().empty())
            processor.presets().load (0);
        if (demo)
            demoState (processor);

        // One bred generation for the BREED page.
        const int numEntries = (int) processor.presets().entries().size();
        std::printf ("presets: %d (%d factory), user folder %s\n", numEntries, (int) verdant::factoryPresets().size(),
                     verdant::PresetManager::userPresetFolder().getFullPathName().toRawUTF8());
        if (numEntries > 0)
        {
            processor.setParentFromPreset (0, 0);
            processor.setParentFromPreset (1, juce::jmin (9, numEntries - 1));
            processor.breedNow();
        }

        verdant::Editor editor (processor);
        editor.setSize (verdant::Editor::baseWidth, verdant::Editor::baseHeight);

        if (withMap)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto points = buildMap (processor);
            std::printf ("sound map: %d points in %.1f s\n", (int) points.size(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
            if (auto* browse = dynamic_cast<verdant::ui::BrowsePage*> (editor.page (5)))
                browse->setSoundMap (std::move (points));
        }

        auto write = [&] (const juce::String& name, float scale)
        {
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, scale, juce::SoftwareImageType());
            const auto file = outDir.getChildFile (name + "@" + scaleText (scale) + "x.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            bool ok = false;
            if (auto stream = file.createOutputStream())
                ok = image.isValid() && png.writeImageToStream (image, *stream);
            std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
            if (! ok) ++failures;
        };
        auto show = [&editor] (int page)
        {
            editor.showPage (page);
            if (auto* p = editor.page (page))
                for (int i = 0; i < 3; ++i) p->tick();
        };

        for (int page = 0; page < verdant::Editor::numPages; ++page)
        {
            show (page);
            for (float scale : { 1.0f, 2.0f })
                write (verdant::Editor::pageId (page), scale);
        }

        if (demo)
        {
            // Every effect type's panel, in slot 6 (empty in the demo rack) -> fx-<name>@1x.png.
            show (2);
            if (auto* fx = dynamic_cast<verdant::ui::FxPage*> (editor.page (2)))
            {
                const char* names[] = { "empty", "distortion", "chorus", "phaser", "flanger", "delay", "reverb", "eq", "compressor" };
                for (int t = 0; t < 9; ++t)
                {
                    setParam (processor, verdant::fxSlotType (5), (float) t);
                    fx->selectSlot (5);
                    editor.page (2)->tick();
                    write (juce::String ("fx-") + names[t], 1.0f);
                }
                setParam (processor, verdant::fxSlotType (5), 0.0f);
                fx->selectSlot (4);
            }
        }

        // The paint panel open on Osc A.
        show (0);
        if (auto* osc = dynamic_cast<verdant::ui::OscPage*> (editor.page (0)))
        {
            if (osc->onPaintRequested)
                osc->onPaintRequested (0);
            for (float scale : { 1.0f, 2.0f })
                write ("paint", scale);
            if (auto* panel = findChild<verdant::ui::PaintCanvasPanel> (*osc))
                panel->close();
        }

        // Scope (FX page) and spectrum (ARP/SEQ page) while a note plays.
        if (auto* toggle = dynamic_cast<juce::Button*> (findById (editor, "verdant.scopeToggle")))
        {
            toggle->setToggleState (true, juce::sendNotificationSync);
            auto* scope = findChild<verdant::ui::Scope> (editor);
            play (processor, 40, 57);
            show (2);
            for (int i = 0; i < 4; ++i)
            {
                play (processor, 3, -1);
                editor.page (2)->tick();
                if (scope != nullptr) scope->tick();
            }
            for (float scale : { 1.0f, 2.0f })
                write ("scope", scale);
            if (scope != nullptr)
            {
                scope->setMode (verdant::ui::Scope::Mode::Spectrum);
                show (3);
                for (int i = 0; i < 12; ++i)
                {
                    play (processor, 3, -1);
                    editor.page (3)->tick();
                    scope->tick();
                }
                for (float scale : { 1.0f, 2.0f })
                    write ("spectrum", scale);
                scope->setMode (verdant::ui::Scope::Mode::Scope);
            }
            toggle->setToggleState (false, juce::sendNotificationSync);
        }

        if (extraScale > 0.0f)
        {
            editor.setSize (juce::roundToInt ((float) verdant::Editor::baseWidth * extraScale),
                            juce::roundToInt ((float) verdant::Editor::baseHeight * extraScale));
            for (int page = 0; page < verdant::Editor::numPages; ++page)
            {
                show (page);
                const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
                const auto file = outDir.getChildFile (juce::String (verdant::Editor::pageId (page)) + "@" + scaleText (extraScale) + "x.png");
                file.deleteFile();
                juce::PNGImageFormat png;
                bool ok = false;
                if (auto stream = file.createOutputStream())
                    ok = image.isValid() && png.writeImageToStream (image, *stream);
                std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
                if (! ok) ++failures;
            }
        }

        processor.tasks().cancelAll();
        processor.releaseResources();
    }

    return failures == 0 ? 0 : 1;
}
