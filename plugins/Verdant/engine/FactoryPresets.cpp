#include "FactoryPresets.h"

#include "FactoryPresets_Recipes.h"

namespace verdant
{

namespace
{
    std::vector<Patch> build()
    {
        std::vector<Patch> bank;
        bank.reserve (210);
        // Fixed order (host program numbers): the two openers, then one block per category.
        factory::addOpeningPresets (bank);
        factory::addBassPresets (bank);
        factory::addLeadPresets (bank);
        factory::addPadPresets (bank);
        factory::addPluckPresets (bank);
        factory::addKeysPresets (bank);
        factory::addFxPresets (bank);
        factory::addTexturePresets (bank);
        factory::addSequencePresets (bank);
        factory::addDrumPresets (bank);
        // Expansions only ever append, so the original program numbers never move.
        factory::addExpansionPresets (bank);
        return bank;
    }
}

const std::vector<Patch>& factoryPresets()
{
    static const std::vector<Patch> bank = build();
    return bank;
}

const std::vector<std::string>& factoryCategories()
{
    static const std::vector<std::string> categories { "Bass", "Lead", "Pad", "Pluck", "Keys", "FX", "Texture",
                                                       "Sequence", "Drums/Perc" };
    return categories;
}

const std::vector<std::string>& factoryTags()
{
    // Timbre, character, use and technique; every factory preset picks 2-4 of these.
    static const std::vector<std::string> tags {
        "warm", "bright", "dark", "soft", "aggressive", "wide", "mono", "evolving", "rhythmic", "vintage",
        "digital", "cinematic", "dirty", "clean", "plucky", "hollow", "glassy", "airy", "sub", "growl",
        "chord", "retro", "modern", "ambient", "dusty", "percussive", "resonant", "vocal", "expressive",
        "metallic", "noisy", "organic", "glitchy", "lush", "punchy", "deep", "sequenced", "physical",
        "drone", "smooth", "bouncy", "detuned", "fm", "acid", "classic", "lofi", "tonal", "short"
    };
    return tags;
}

int factoryPreviewNote (const std::string& category)
{
    return category == "Bass" ? 36 : 48;
}

} // namespace verdant
