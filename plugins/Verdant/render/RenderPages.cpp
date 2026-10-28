// verdant_render <outDir>: creates the processor, loads factory preset 0, opens
// the editor without a window and writes every page to <outDir>/<page>@1x.png
// and <page>@2x.png. Runs headless (no display needed).

#include "../PluginEditor.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: verdant_render <outDir>\n");
        return 2;
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

        verdant::Editor editor (processor);
        editor.setSize (verdant::Editor::baseWidth, verdant::Editor::baseHeight);

        for (int page = 0; page < verdant::Editor::numPages; ++page)
        {
            editor.showPage (page);
            for (int scale : { 1, 2 })
            {
                const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, (float) scale, juce::SoftwareImageType());
                const auto file = outDir.getChildFile (juce::String (verdant::Editor::pageId (page)) + "@" + juce::String (scale) + "x.png");
                file.deleteFile();

                juce::PNGImageFormat png;
                bool ok = false;
                if (auto stream = file.createOutputStream())
                    ok = image.isValid() && png.writeImageToStream (image, *stream);

                std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
                if (! ok) ++failures;
            }
        }

        processor.releaseResources();
    }

    return failures == 0 ? 0 : 1;
}
