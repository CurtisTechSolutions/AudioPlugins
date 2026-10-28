#pragma once

#include <juce_core/juce_core.h>

#include "engine/Patch.h"

#include <memory>
#include <vector>

namespace verdant
{

class Processor;

/**
    Factory presets (from factoryPresets(), listed first, so entry i = host program i)
    plus user presets in
        <userDocuments>/Curtis Tech Solutions/Verdant/Presets/<Category>/<Name>.vrdt
    (the same XML as the plugin state; the folder is created on the first save).

    Message thread for everything that reads or writes files (rescan, load of a
    user preset, save, remove). The processor reports host program changes and
    state restores through programLoaded() / stateRestored(), which may run on
    any thread and only touch lock-protected bookkeeping. The audio thread never
    calls in here.
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

    /** Factory entries first (in factoryPresets() order), then user presets sorted by category folder and
        name. Message thread (the list is replaced only by rescan / save / remove, on the message thread). */
    const std::vector<Entry>& entries() const noexcept { return list; }

    /** The entry that is loaded and unchanged since, or -1 (nothing loaded, init(), or modified: any
        parameter or extra changed after the load). */
    int currentIndex() const;

    /** The entry most recently loaded or saved, even if modified since (-1 = none); next() / previous()
        step from here. */
    int lastLoadedIndex() const;

    bool isModified() const;

    void load (int index);
    void next();        // wraps
    void previous();    // wraps
    void init();        // applies initPatch(); currentIndex() becomes -1

    /** Saves the current sound as a user preset (overwrites a preset of the same category / name), renames
        the current patch accordingly and makes the new file the current entry. */
    bool saveCurrent (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                      const juce::String& author, juce::String* error = nullptr);

    /** Saves any patch as a user preset without loading it (e.g. a BREED child). `written` receives the file. */
    bool savePatch (const Patch&, const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                    const juce::String& author, juce::String* error = nullptr, juce::File* written = nullptr);

    /** Deletes a user preset's file (factory entries cannot be removed) and rescans. */
    bool remove (int index, juce::String* error = nullptr);

    /** Categories in list order (factory first), without duplicates. */
    juce::StringArray categories() const;

    /** Every tag used by any entry, in first-seen order, without duplicates (case-insensitive). */
    juce::StringArray allTags() const;

    /** Re-reads the user folder (unreadable or foreign files are skipped). */
    void rescan();

    /** Reads an entry's patch without applying it (breeding parents, the sound map, previews). */
    bool readPatch (int index, Patch& out) const;

    /** Processor hooks (any thread): a host program change / a restored session. */
    void programLoaded (int factoryIndex);
    void stateRestored (const Patch&, int program);

    static juce::File userPresetFolder();
    static constexpr const char* fileExtension = ".vrdt";

    /** A file-system-safe preset / category name: illegal characters removed, no leading or trailing dots
        or spaces (so "..", "." or "/x" cannot escape the folder), at most 64 characters. May be empty. */
    static juce::String legalName (const juce::String&);

    /** Tags trimmed, without line breaks, empties or case-insensitive duplicates (first spelling kept). */
    static juce::StringArray cleanTags (const juce::StringArray&);

private:
    struct Identity
    {
        bool valid = false, factory = false;
        int factoryIndex = -1;
        juce::File file;
    };

    int indexOfLocked (const Identity&) const;
    void markLoadedLocked (const Identity&);
    Identity identityOf (int index) const;
    static std::vector<Entry> scanUserFolder();

    Processor& processor;
    mutable juce::CriticalSection lock;   // guards list replacement, `loaded` and the snapshot
    std::vector<Entry> list;
    Identity loaded;
    ParamValues loadedValues {};
    uint32_t loadedRevision = 0;
};

/** <VERDANT><PARAM id=".." value=".."/>...<EXTRAS>serializePatch text without param lines</EXTRAS></VERDANT>
    (the plugin state and the .vrdt file format). */
std::unique_ptr<juce::XmlElement> patchToXml (const Patch&);

/** Missing parameters keep their defaults, non-finite values are ignored, values are clamped to their
    ranges; unknown ids and tags are ignored. False only when the root tag is not VERDANT. */
bool patchFromXml (const juce::XmlElement&, Patch&, juce::String* error = nullptr);

/** serializePatch() text without the "param" lines (the EXTRAS payload). */
juce::String extrasText (const Patch&);

} // namespace verdant
