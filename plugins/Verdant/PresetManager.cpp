#include "PresetManager.h"

#include "PluginProcessor.h"

#include "engine/FactoryPresets.h"

namespace verdant
{

namespace
{
    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    PresetManager::Entry entryFor (const Patch& p)
    {
        PresetManager::Entry e;
        e.name = str (p.name);
        e.category = str (p.category);
        e.author = str (p.author);
        for (const auto& t : p.tags) e.tags.add (str (t));
        return e;
    }
}

//==============================================================================
juce::String extrasText (const Patch& p)
{
    const std::string full = serializePatch (p);
    std::string out;
    out.reserve (full.size() / 4);
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
        e->setAttribute ("value", (double) p.values[(size_t) i]);
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
        if (deserializePatch (extrasXml->getAllSubText().toStdString(), fromText))
            p = std::move (fromText);
    }

    p.values = defaultValues();
    for (const auto* child : xml.getChildWithTagNameIterator ("PARAM"))
    {
        const int idx = indexOf (child->getStringAttribute ("id").toRawUTF8());
        if (idx >= 0 && child->hasAttribute ("value"))
            p.values[(size_t) idx] = clampToRange ((P) idx, (float) child->getDoubleAttribute ("value"));
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

void PresetManager::rescan()
{
    const juce::File currentFile = current >= 0 && current < (int) list.size() ? list[(size_t) current].file : juce::File();
    const bool currentWasFactory = current >= 0 && current < (int) list.size() && list[(size_t) current].factory;
    const int currentFactory = currentWasFactory ? list[(size_t) current].factoryIndex : -1;

    list.clear();
    const auto& bank = factoryPresets();
    for (size_t i = 0; i < bank.size(); ++i)
    {
        auto e = entryFor (bank[i]);
        e.factory = true;
        e.factoryIndex = (int) i;
        list.push_back (std::move (e));
    }

    const auto folder = userPresetFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension);
        files.sort();
        for (const auto& f : files)
        {
            const auto xml = juce::parseXML (f);
            Patch p;
            if (xml == nullptr || ! patchFromXml (*xml, p))
                continue;   // not a Verdant preset

            Entry e = entryFor (p);
            if (xml->getChildByName ("EXTRAS") == nullptr)
            {
                e.name = f.getFileNameWithoutExtension();
                e.category = f.getParentDirectory() != folder ? f.getParentDirectory().getFileName() : juce::String ("User");
            }
            e.factory = false;
            e.file = f;
            list.push_back (std::move (e));
        }
    }

    current = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if ((currentWasFactory && list[i].factory && list[i].factoryIndex == currentFactory)
            || (! currentWasFactory && currentFile != juce::File() && list[i].file == currentFile))
            current = (int) i;
}

bool PresetManager::readPatch (int index, Patch& out) const
{
    if (index < 0 || index >= (int) list.size())
        return false;

    const auto& e = list[(size_t) index];
    if (e.factory)
    {
        const auto& bank = factoryPresets();
        if (e.factoryIndex < 0 || e.factoryIndex >= (int) bank.size())
            return false;
        out = bank[(size_t) e.factoryIndex];
        return true;
    }

    const auto xml = juce::parseXML (e.file);
    return xml != nullptr && patchFromXml (*xml, out);
}

void PresetManager::load (int index)
{
    Patch p;
    if (! readPatch (index, p))
        return;
    current = index;
    processor.applyPatch (p, false);
}

void PresetManager::next()
{
    if (list.empty()) return;
    load ((current + 1 + (int) list.size()) % (int) list.size());
}

void PresetManager::previous()
{
    if (list.empty()) return;
    load ((current < 0 ? (int) list.size() - 1 : current - 1 + (int) list.size()) % (int) list.size());
}

void PresetManager::init()
{
    processor.applyPatch (initPatch(), false);
    current = -1;
}

bool PresetManager::saveCurrent (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                                 const juce::String& author, juce::String* error)
{
    const auto cleanName = juce::File::createLegalFileName (name.trim());
    const auto cleanCategory = juce::File::createLegalFileName (category.trim().isNotEmpty() ? category.trim() : juce::String ("User"));
    if (cleanName.isEmpty())
    {
        if (error != nullptr) *error = "Please enter a preset name";
        return false;
    }

    Patch p = processor.currentPatch();
    p.name = name.trim().toStdString();
    p.category = (category.trim().isNotEmpty() ? category.trim() : juce::String ("User")).toStdString();
    p.author = author.trim().toStdString();
    p.tags.clear();
    for (const auto& t : tags)
        if (t.trim().isNotEmpty()) p.tags.push_back (t.trim().toStdString());

    const auto dir = userPresetFolder().getChildFile (cleanCategory);
    const auto file = dir.getChildFile (cleanName + fileExtension);
    if (! dir.createDirectory())
    {
        if (error != nullptr) *error = "Could not create " + dir.getFullPathName();
        return false;
    }
    if (! patchToXml (p)->writeTo (file))
    {
        if (error != nullptr) *error = "Could not write " + file.getFullPathName();
        return false;
    }

    processor.applyPatch (p, false);   // the sound is unchanged; this updates the name / category / tags
    rescan();
    for (size_t i = 0; i < list.size(); ++i)
        if (! list[i].factory && list[i].file == file)
            current = (int) i;
    return true;
}

juce::StringArray PresetManager::categories() const
{
    juce::StringArray result;
    for (const auto& e : list)
        result.addIfNotAlreadyThere (e.category);
    return result;
}

} // namespace verdant
