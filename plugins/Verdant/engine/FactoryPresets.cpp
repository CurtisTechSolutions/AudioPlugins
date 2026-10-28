#include "FactoryPresets.h"

namespace verdant
{

namespace
{
    void set (Patch& p, P param, float value) { p.values[(size_t) param] = clampToRange (param, value); }

    std::vector<Patch> build()
    {
        std::vector<Patch> bank;

        {
            Patch p = initPatch();
            p.author = "Curtis Tech Solutions";
            bank.push_back (p);
        }

        {
            Patch p = initPatch();
            p.name = "Soft Pad";
            p.category = "Pad";
            p.author = "Curtis Tech Solutions";
            p.tags = { "soft", "warm", "wide" };
            set (p, oscParam (0, OscParam::Pos), 20.0f);
            set (p, oscParam (0, OscParam::UniVoices), 3.0f);
            set (p, oscParam (0, OscParam::UniDetune), 12.0f);
            set (p, filterParam (0, FilterParam::Cutoff), 3000.0f);
            set (p, envParam (0, EnvParam::A), 400.0f);
            set (p, envParam (0, EnvParam::D), 800.0f);
            set (p, envParam (0, EnvParam::S), 70.0f);
            set (p, envParam (0, EnvParam::R), 1200.0f);
            bank.push_back (p);
        }

        {
            Patch p = initPatch();
            p.name = "Saw Lead";
            p.category = "Lead";
            p.author = "Curtis Tech Solutions";
            p.tags = { "bright", "classic" };
            set (p, oscParam (0, OscParam::Pos), 66.0f);
            set (p, oscParam (1, OscParam::On), 1.0f);
            set (p, oscParam (1, OscParam::Pos), 66.0f);
            set (p, oscParam (1, OscParam::Fine), 7.0f);
            set (p, oscParam (1, OscParam::Level), 60.0f);
            set (p, filterParam (0, FilterParam::Cutoff), 5000.0f);
            set (p, filterParam (0, FilterParam::Res), 25.0f);
            set (p, envParam (0, EnvParam::A), 2.0f);
            set (p, envParam (0, EnvParam::R), 200.0f);
            set (p, P::Master, -9.0f);
            bank.push_back (p);
        }

        return bank;
    }
}

const std::vector<Patch>& factoryPresets()
{
    static const std::vector<Patch> bank = build();
    return bank;
}

} // namespace verdant
