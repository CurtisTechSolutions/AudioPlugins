#include "PresetManager.h"

#include "PluginProcessor.h"

#include "engine/FactoryPresets.h"

#include <cmath>

namespace verdant
{

namespace
{
    // A preset with two full user tables is a few MB of base64; anything far larger is not ours.
    constexpr juce::int64 kMaxPresetFileBytes = 64 * 1024 * 1024;

    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    PresetManager::Entry entryFor (const Patch& p)
    {
        PresetManager::Entry e;
        e.name = str (p.name);
        e.category = str (p.category);
        e.author = str (p.author);
        for (const auto& t : p.tags)
            e.tags.add (str (t));
        return e;
    }

    std::unique_ptr<juce::XmlElement> readPresetXml (const juce::File& f)
    {
        if (! f.existsAsFile() || f.getSize() <= 0 || f.getSize() > kMaxPresetFileBytes)
            return nullptr;
        return juce::parseXML (f);   // nullptr for unreadable or malformed files
    }

    bool sameValues (const ParamValues& a, const ParamValues& b) noexcept
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (! juce::exactlyEqual (a[i], b[i]))
                return false;
        return true;
    }
}

//==============================================================================
juce::String extrasText (const Patch& p)
{
    const std::string full = serializePatch (p);
    std::string out;
    out.reserve (full.size());
    size_t start = 0;
    while (start < full.size())
    {
        size_t end = full.find ('\n', start);
        if (end == std::string::npos) end = full.size();
        if (full.compare (start, 6, "param ") != 0)
            out.append (full, start, end - start).append ("\n");
        start = end + 1;
    }
    return juce::String::fromUTF8 (out.c_str());
}

std::unique_ptr<juce::XmlElement> patchToXml (const Patch& p)
{
    auto xml = std::make_unique<juce::XmlElement> ("VERDANT");
    for (int i = 0; i < kNumParams; ++i)
    {
        auto* e = xml->createNewChildElement ("PARAM");
        e->setAttribute ("id", info ((P) i).id);
        e->setAttribute ("value", (double) p.values[(size_t) i]);   // float -> double is exact; JUCE writes it round-trip safe
    }
    xml->createNewChildElement ("EXTRAS")->addTextElement (extrasText (p));
    return xml;
}

bool patchFromXml (const juce::XmlElement& xml, Patch& out, juce::String* error)
{
    if (! xml.hasTagName ("VERDANT"))
    {
        if (error != nullptr) *error = "Not a Verdant preset";
        return false;
    }

    Patch p;
    if (const auto* extrasXml = xml.getChildByName ("EXTRAS"))
    {
        Patch fromText;
        std::string why;
        if (deserializePatch (extrasXml->getAllSubText().toStdString(), fromText, &why))
            p = std::move (fromText);
        else if (error != nullptr)
            *error = "Preset extras ignored: " + str (why);   // still a usable patch: parameters below
    }

    // The <PARAM> children are authoritative (they are what the host automates); "param" lines that
    // might sit in the extras text only count when there are no <PARAM> children at all.
    bool anyParam = false;
    for (const auto* child : xml.getChildWithTagNameIterator ("PARAM"))
    {
        if (! anyParam)
        {
            p.values = defaultValues();
            anyParam = true;
        }
        const int idx = indexOf (child->getStringAttribute ("id").toRawUTF8());
        if (idx < 0 || ! child->hasAttribute ("value"))
            continue;
        const double v = child->getDoubleAttribute ("value");
        if (std::isfinite (v))
            p.values[(size_t) idx] = clampToRange ((P) idx, (float) v);
    }
    for (int i = 0; i < kNumParams; ++i)   // also sanitises values that came from the extras text
    {
        const float v = p.values[(size_t) i];
        p.values[(size_t) i] = std::isfinite (v) ? clampToRange ((P) i, v) : info ((P) i).def;
    }

    out = std::move (p);
    return true;
}

//==============================================================================
PresetManager::PresetManager (Processor& p)
    : processor (p)
{
    rescan();
}

juce::File PresetManager::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Curtis Tech Solutions")
               .getChildFile ("Verdant")
               .getChildFile ("Presets");
}

juce::String PresetManager::legalName (const juce::String& s)
{
    auto name = juce::File::createLegalFileName (s.trim()).trim();
    while (name.startsWithChar ('.') || name.startsWithChar (' '))
        name = name.substring (1);
    name = name.substring (0, 64);
    while (name.endsWithChar ('.') || name.endsWithChar (' '))
        name = name.dropLastCharacters (1);
    return name;
}

std::vector<PresetManager::Entry> PresetManager::scanUserFolder()
{
    std::vector<Entry> result;
    const auto folder = userPresetFolder();
    if (! folder.isDirectory())
        return result;

    auto files = folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension);
    files.sort();   // by path: category folder, then name
    for (const auto& f : files)
    {
        const auto xml = readPresetXml (f);
        Patch p;
        if (xml == nullptr || ! patchFromXml (*xml, p))
            continue;   // unreadable, malformed or not a Verdant preset

        Entry e = entryFor (p);
        // Files without extras (or with an empty name) are named after the file and its folder.
        if (xml->getChildByName ("EXTRAS") == nullptr || e.name.trim().isEmpty())
            e.name = f.getFileNameWithoutExtension();
        if (xml->getChildByName ("EXTRAS") == nullptr || e.category.trim().isEmpty())
            e.category = f.getParentDirectory() != folder ? f.getParentDirectory().getFileName() : juce::String ("User");
        e.factory = false;
        e.file = f;
        result.push_back (std::move (e));
    }
    return result;
}

void PresetManager::rescan()
{
    std::vector<Entry> fresh;
    const auto& bank = factoryPresets();
    fresh.reserve (bank.size());
    for (size_t i = 0; i < bank.size(); ++i)
    {
        auto e = entryFor (bank[i]);
        e.factory = true;
        e.factoryIndex = (int) i;
        fresh.push_back (std::move (e));
    }
    auto user = scanUserFolder();   // file I/O outside the lock
    for (auto& e : user)
        fresh.push_back (std::move (e));

    const juce::ScopedLock sl (lock);
    list = std::move (fresh);
    if (loaded.valid && indexOfLocked (loaded) < 0)
        loaded = {};   // the loaded file went away
}

int PresetManager::indexOfLocked (const Identity& id) const
{
    if (! id.valid)
        return -1;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& e = list[i];
        if (id.factory ? (e.factory && e.factoryIndex == id.factoryIndex)
                       : (! e.factory && e.file == id.file))
            return (int) i;
    }
    return -1;
}

PresetManager::Identity PresetManager::identityOf (int index) const
{
    Identity id;
    if (index >= 0 && index < (int) list.size())
    {
        const auto& e = list[(size_t) index];
        id.valid = true;
        id.factory = e.factory;
        id.factoryIndex = e.factoryIndex;
        id.file = e.file;
    }
    return id;
}

void PresetManager::markLoadedLocked (const Identity& id)
{
    loaded = id;
    loadedValues = processor.parameterValues();
    loadedRevision = processor.extrasRevision();
}

int PresetManager::lastLoadedIndex() const
{
    const juce::ScopedLock sl (lock);
    return indexOfLocked (loaded);
}

bool PresetManager::isModified() const
{
    const juce::ScopedLock sl (lock);
    return ! loaded.valid
        || processor.extrasRevision() != loadedRevision
        || ! sameValues (processor.parameterValues(), loadedValues);
}

int PresetManager::currentIndex() const
{
    const juce::ScopedLock sl (lock);
    const int index = indexOfLocked (loaded);
    if (index < 0
        || processor.extrasRevision() != loadedRevision
        || ! sameValues (processor.parameterValues(), loadedValues))
        return -1;
    return index;
}

bool PresetManager::readPatch (int index, Patch& out) const
{
    Entry e;
    {
        const juce::ScopedLock sl (lock);
        if (index < 0 || index >= (int) list.size())
            return false;
        e = list[(size_t) index];
    }

    if (e.factory)
    {
        const auto& bank = factoryPresets();
        if (e.factoryIndex < 0 || e.factoryIndex >= (int) bank.size())
            return false;
        out = bank[(size_t) e.factoryIndex];
        return true;
    }

    const auto xml = readPresetXml (e.file);
    Patch p;
    if (xml == nullptr || ! patchFromXml (*xml, p))
        return false;
    // Keep what the browser showed for files that lack a name / category of their own.
    if (p.name.empty() || xml->getChildByName ("EXTRAS") == nullptr)         p.name = e.name.toStdString();
    if (p.category.empty() || xml->getChildByName ("EXTRAS") == nullptr)     p.category = e.category.toStdString();
    out = std::move (p);
    return true;
}

void PresetManager::load (int index)
{
    Patch p;
    if (! readPatch (index, p))
        return;

    processor.applyPatch (p, false);

    Identity id;
    {
        const juce::ScopedLock sl (lock);
        id = identityOf (index);
        markLoadedLocked (id);
    }
    if (id.factory)
        processor.setCurrentProgramIndex (id.factoryIndex, true);   // outside the lock: this calls into the host
}

void PresetManager::next()
{
    int size = 0, from = -1;
    {
        const juce::ScopedLock sl (lock);
        size = (int) list.size();
        from = indexOfLocked (loaded);
    }
    if (size > 0)
        load (from < 0 ? 0 : (from + 1) % size);
}

void PresetManager::previous()
{
    int size = 0, from = -1;
    {
        const juce::ScopedLock sl (lock);
        size = (int) list.size();
        from = indexOfLocked (loaded);
    }
    if (size > 0)
        load (from < 0 ? size - 1 : (from - 1 + size) % size);
}

void PresetManager::init()
{
    processor.applyPatch (initPatch(), false);
    const juce::ScopedLock sl (lock);
    loaded = {};
}

void PresetManager::programLoaded (int factoryIndex)
{
    const juce::ScopedLock sl (lock);
    Identity id;
    id.valid = factoryIndex >= 0 && factoryIndex < (int) factoryPresets().size();
    id.factory = true;
    id.factoryIndex = factoryIndex;
    if (id.valid) markLoadedLocked (id);
    else          loaded = {};
}

void PresetManager::stateRestored (const Patch& p, int program)
{
    // A session remembers which preset it came from only by name: the factory program it reported, or a
    // user preset with the same name and category. Anything else restores as "no preset" (-1).
    const auto name = str (p.name), category = str (p.category);
    const auto& bank = factoryPresets();

    const juce::ScopedLock sl (lock);
    Identity id;
    if (program >= 0 && program < (int) bank.size() && bank[(size_t) program].name == p.name)
    {
        id.valid = true;
        id.factory = true;
        id.factoryIndex = program;
    }
    else
    {
        for (const auto& e : list)
            if (! e.factory && e.name == name && e.category == category)
            {
                id.valid = true;
                id.file = e.file;
                break;
            }
    }
    if (id.valid) markLoadedLocked (id);
    else          loaded = {};
}

bool PresetManager::savePatch (const Patch& patch, const juce::String& name, const juce::String& category,
                               const juce::StringArray& tags, const juce::String& author,
                               juce::String* error, juce::File* written)
{
    const auto displayName = name.trim();
    const auto displayCategory = category.trim().isNotEmpty() ? category.trim() : juce::String ("User");
    const auto fileName = legalName (displayName);
    auto folderName = legalName (displayCategory);
    if (folderName.isEmpty())
        folderName = "User";

    if (fileName.isEmpty())
    {
        if (error != nullptr) *error = "Please enter a preset name";
        return false;
    }

    Patch p = patch;
    p.name = displayName.toStdString();
    p.category = displayCategory.toStdString();
    p.author = author.trim().toStdString();
    p.tags.clear();
    for (const auto& tag : cleanTags (tags))
        p.tags.push_back (tag.toStdString());

    const auto dir = userPresetFolder().getChildFile (folderName);
    const auto file = dir.getChildFile (fileName + fileExtension);
    if (! dir.createDirectory())
    {
        if (error != nullptr) *error = "Could not create " + dir.getFullPathName();
        return false;
    }
    if (! patchToXml (p)->writeTo (file))   // writes through a temporary file, then replaces
    {
        if (error != nullptr) *error = "Could not write " + file.getFullPathName();
        return false;
    }

    if (written != nullptr)
        *written = file;
    rescan();
    return true;
}

bool PresetManager::saveCurrent (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                                 const juce::String& author, juce::String* error)
{
    juce::File file;
    if (! savePatch (processor.currentPatch(), name, category, tags, author, error, &file))
        return false;

    // The sound is unchanged; only the name / category / tags / author follow the saved file.
    processor.setPatchInfo (name.trim(), category.trim().isNotEmpty() ? category.trim() : juce::String ("User"),
                            cleanTags (tags), author.trim());

    const juce::ScopedLock sl (lock);
    Identity id;
    id.valid = true;
    id.file = file;
    if (indexOfLocked (id) >= 0)
        markLoadedLocked (id);
    return true;
}

bool PresetManager::remove (int index, juce::String* error)
{
    juce::File file;
    {
        const juce::ScopedLock sl (lock);
        if (index < 0 || index >= (int) list.size() || list[(size_t) index].factory)
        {
            if (error != nullptr) *error = "Factory presets cannot be deleted";
            return false;
        }
        file = list[(size_t) index].file;
    }

    if (file.existsAsFile() && ! file.deleteFile())
    {
        if (error != nullptr) *error = "Could not delete " + file.getFullPathName();
        return false;
    }

    // Tidy up a category folder that is now empty (never the presets root itself).
    const auto dir = file.getParentDirectory();
    if (dir != userPresetFolder() && dir.isDirectory() && dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        dir.deleteFile();   // removes an empty directory

    rescan();
    processor.patchChanged.sendChangeMessage();   // browsers refresh their lists
    return true;
}

juce::StringArray PresetManager::cleanTags (const juce::StringArray& tags)
{
    juce::StringArray result;
    for (const auto& t : tags)
    {
        const auto tag = t.removeCharacters ("\r\n").trim();
        if (tag.isNotEmpty() && ! result.contains (tag, true))
            result.add (tag);
    }
    return result;
}

juce::StringArray PresetManager::categories() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray result;
    for (const auto& e : list)
        result.addIfNotAlreadyThere (e.category);
    return result;
}

juce::StringArray PresetManager::allTags() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray result;
    for (const auto& e : list)
        for (const auto& t : e.tags)
            result.addIfNotAlreadyThere (t, true);
    return result;
}

} // namespace verdant
