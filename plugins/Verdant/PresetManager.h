#pragma once

#include <juce_core/juce_core.h>

#include "engine/Patch.h"

#include <memory>
#include <vector>

namespace verdant
{

class Processor;

/**
    Factory presets (from factoryPresets(), listed first) plus user presets in
    <userDocuments>/Curtis Tech Solutions/Verdant/Presets/<Category>/<Name>.vrdt
    (the same XML as the plugin state). Message thread only.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name, category, author;
        juce::StringArray tags;
        bool factory = false;
        int factoryIndex = -1;    // index into factoryPresets() when factory
        juce::File file;          // the .vrdt file when not factory
    };

    explicit PresetManager (Processor&);

    const std::vector<Entry>& entries() const noexcept { return list; }
    int currentIndex() const noexcept { return current; }   // -1 = none (e.g. after init())

    void load (int index);
    void next();
    void previous();
    void init();

    /** Saves the current sound as a user preset (overwrites a preset of the same category / name). */
    bool saveCurrent (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                      const juce::String& author, juce::String* error = nullptr);

    /** Categories in list order (factory first), without duplicates. */
    juce::StringArray categories() const;

    void rescan();

    /** Reads an entry's patch without applying it (breeding parents, the sound map, previews). */
    bool readPatch (int index, Patch& out) const;

    static juce::File userPresetFolder();
    static constexpr const char* fileExtension = ".vrdt";

private:
    Processor& processor;
    std::vector<Entry> list;
    int current = 0;
};

/** <VERDANT><PARAM id=".." value=".."/>...<EXTRAS>serializePatch text without param lines</EXTRAS></VERDANT> */
std::unique_ptr<juce::XmlElement> patchToXml (const Patch&);

/** Missing parameters keep their defaults; unknown ids and tags are ignored. */
bool patchFromXml (const juce::XmlElement&, Patch&, juce::String* error = nullptr);

/** serializePatch() text without the "param" lines (the EXTRAS payload). */
juce::String extrasText (const Patch&);

} // namespace verdant
