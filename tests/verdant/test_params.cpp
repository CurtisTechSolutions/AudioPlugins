// Parameter table (CONTRACT §3 + the pitch_snap amendment): count, ids / names, ranges, skew mapping,
// instanced-parameter helpers and the modulation destination list.

#include "TestHarness.h"

#include "engine/Params.h"
#include "engine/WavetableFactory.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace verdant;

namespace
{
    std::string idOf (P p)   { return info (p).id; }
    std::string nameOf (P p) { return info (p).name; }

    std::string choiceOf (P p, int index)
    {
        const auto& in = info (p);
        return in.choices != nullptr && index >= 0 && index < in.numChoices ? in.choices[index] : std::string ("<none>");
    }

    std::string defaultChoice (P p) { return choiceOf (p, (int) info (p).def); }
}

AP_TEST (params_count_ids_and_names_unique)
{
    CHECK_EQ (kNumParams, 348);
    std::set<std::string> ids, names;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& in = info ((P) i);
        CHECK (in.id != nullptr && std::strlen (in.id) > 0);
        CHECK (in.name != nullptr && std::strlen (in.name) > 0);
        CHECK (in.shortName != nullptr && std::strlen (in.shortName) > 0);
        CHECK (in.group != nullptr && std::strlen (in.group) > 0);
        CHECK (in.unit != nullptr);
        ids.insert (in.id);
        names.insert (in.name);
        CHECK_EQ (indexOf (in.id), i);
    }
    CHECK_EQ ((int) ids.size(), kNumParams);
    CHECK_EQ ((int) names.size(), kNumParams);
    CHECK_EQ (indexOf ("no_such_param"), -1);
    CHECK_EQ (indexOf (nullptr), -1);
}

AP_TEST (params_ranges_and_defaults)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& in = info ((P) i);
        CHECK (in.min < in.max);
        CHECK (in.def >= in.min && in.def <= in.max);
        if (in.midpoint != 0.0f)
        {
            CHECK (in.kind == ParamKind::Float);
            CHECK (in.midpoint > in.min && in.midpoint < in.max);
        }
        switch (in.kind)
        {
            case ParamKind::Choice:
                CHECK (in.choices != nullptr);
                CHECK (in.numChoices >= 2);
                CHECK_EQ (in.min, 0.0f);
                CHECK_EQ (in.max, (float) (in.numChoices - 1));
                CHECK_EQ (in.def, std::floor (in.def));
                for (int c = 0; c < in.numChoices; ++c) CHECK (in.choices[c] != nullptr && std::strlen (in.choices[c]) > 0);
                break;
            case ParamKind::Bool:
                CHECK_EQ (in.min, 0.0f);
                CHECK_EQ (in.max, 1.0f);
                CHECK (in.def == 0.0f || in.def == 1.0f);
                break;
            case ParamKind::Int:
                CHECK_EQ (in.min, std::floor (in.min));
                CHECK_EQ (in.max, std::floor (in.max));
                CHECK_EQ (in.def, std::floor (in.def));
                break;
            case ParamKind::Float:
                break;
        }
        if (in.kind != ParamKind::Float) CHECK (! in.modulatable);
    }

    const auto d = defaultValues();
    for (int i = 0; i < kNumParams; ++i)
        CHECK_EQ (d[(size_t) i], info ((P) i).def);
}

AP_TEST (params_specific_rows)
{
    CHECK (idOf (P::Voices) == "voices");
    CHECK (idOf (P::Master) == "master");
    CHECK (idOf (P::KeyScale) == "key_scale");
    CHECK (idOf (P::PitchSnap) == "pitch_snap");
    CHECK_EQ ((int) P::PitchSnap, (int) P::KeyScale + 1);
    CHECK (nameOf (P::PitchSnap) == "Pitch Snap to Key");
    CHECK (std::string (info (P::PitchSnap).shortName) == "Pitch Snap");
    CHECK (std::string (info (P::PitchSnap).group) == "Global");
    CHECK (info (P::PitchSnap).kind == ParamKind::Bool);
    CHECK_EQ (info (P::PitchSnap).def, 0.0f);
    CHECK (! info (P::PitchSnap).modulatable);
    CHECK (idOf (P::SubLevel) == "sub_level");
    CHECK (idOf (P::NoiseRoute) == "noise_route");
    CHECK (idOf (P::FilterRouting) == "filter_routing");
    CHECK (idOf (P::BallGravity) == "ball_gravity");
    CHECK (idOf (P::Alive) == "alive");
    CHECK (idOf (P::ArpOn) == "arp_on");
    CHECK (idOf (P::SeqOn) == "seq_on");
    CHECK (idOf (P::SeqSnap) == "seq_snap");
    CHECK (idOf (P::DistType) == "dist_type");
    CHECK (idOf (P::DelayPingPong) == "delay_pingpong");
    CHECK (idOf (P::EqMidQ) == "eq_mid_q");
    CHECK (idOf (P::CompMix) == "comp_mix");
    CHECK (idOf (P::SympAmount) == "symp_amount");
    CHECK (idOf (P::SympSnap) == "symp_snap");
    CHECK_EQ ((int) P::SympSnap, kNumParams - 1);

    CHECK (nameOf (P::Master) == "Master");
    CHECK (nameOf (P::DelayMix) == "Delay Mix");
    CHECK (nameOf (P::Alive) == "Alive");
    CHECK (nameOf (P::SympAmount) == "Sympathetic");
    CHECK (std::string (info (P::DelayMix).shortName) == "Mix");
    CHECK (std::string (info (P::FilterRouting).group) == "Filter 1");

    // Defaults called out in the contract.
    CHECK_EQ (info (P::Voices).def, 16.0f);
    CHECK_EQ (info (P::Master).def, -6.0f);
    CHECK_EQ (info (oscParam (0, OscParam::On)).def, 1.0f);
    CHECK_EQ (info (oscParam (1, OscParam::On)).def, 0.0f);
    CHECK_EQ (info (filterParam (0, FilterParam::On)).def, 1.0f);
    CHECK_EQ (info (filterParam (1, FilterParam::On)).def, 0.0f);
    CHECK_EQ (info (filterParam (0, FilterParam::Cutoff)).def, 12000.0f);
    CHECK_NEAR (info (filterParam (0, FilterParam::Cutoff)).midpoint, std::sqrt (20.0 * 20000.0), 1.0e-3);
    CHECK_EQ (info (envParam (0, EnvParam::S)).def, 80.0f);
    CHECK_EQ (info (envParam (1, EnvParam::S)).def, 0.0f);
    CHECK_EQ (info (envParam (2, EnvParam::S)).def, 0.0f);
    CHECK_EQ (info (envParam (0, EnvParam::DCurve)).def, -40.0f);
    CHECK_EQ (info (P::SeqSnap).def, 1.0f);
    CHECK_EQ (info (P::DelayPingPong).def, 1.0f);
    CHECK_EQ (info (P::SympMemory).def, 4.0f);
    CHECK (std::string (info (P::SympMemory).unit) == "notes");
    CHECK (std::string (info (P::CompRatio).unit) == ":1");
    CHECK (std::string (info (oscParam (0, OscParam::Phase)).unit) == "\xC2\xB0");

    CHECK (defaultChoice (oscParam (0, OscParam::Table)) == "Basic Shapes");
    CHECK (choiceOf (oscParam (1, OscParam::Table), 16) == "User");
    CHECK_EQ (info (oscParam (0, OscParam::Table)).numChoices, 17);
    for (int t = 0; t < kNumFactoryTables; ++t)
        CHECK (choiceOf (oscParam (0, OscParam::Table), t) == factoryTableNames()[t]);
    CHECK (defaultChoice (filterParam (0, FilterParam::Type)) == "LP 24");
    CHECK_EQ (info (filterParam (0, FilterParam::Type)).numChoices, 11);
    CHECK (defaultChoice (P::SubOct) == "-1");
    CHECK (defaultChoice (P::SubRoute) == "Direct");
    CHECK (defaultChoice (lfoParam (0, LfoParam::Div)) == "1/4");
    CHECK (defaultChoice (P::ArpDiv) == "1/8");
    CHECK (defaultChoice (P::SeqDiv) == "1/16");
    CHECK (defaultChoice (P::DelayDiv) == "1/8");
    CHECK (defaultChoice (P::PhaserStages) == "6");
    CHECK_EQ (info (P::KeyScale).numChoices, 14);
    CHECK (choiceOf (P::KeyScale, 13) == "Whole Tone");
    CHECK_EQ (info (lfoParam (3, LfoParam::Div)).numChoices, 12);
    CHECK_EQ (info (fxSlotType (0)).numChoices, 9);
}

AP_TEST (params_instanced_helpers)
{
    CHECK (idOf (oscParam (0, OscParam::On)) == "a_on");
    CHECK (idOf (oscParam (1, OscParam::Pos)) == "b_pos");
    CHECK (idOf (oscParam (1, OscParam::Route)) == "b_route");
    CHECK (idOf (filterParam (0, FilterParam::Cutoff)) == "f1_cutoff");
    CHECK (idOf (filterParam (1, FilterParam::Mix)) == "f2_mix");
    CHECK (idOf (envParam (2, EnvParam::DCurve)) == "env3_dcurve");
    CHECK (idOf (lfoParam (3, LfoParam::Polarity)) == "lfo4_polarity");
    CHECK (idOf (macroParam (7)) == "macro8");
    CHECK (idOf (modSrc (0)) == "mod1_src");
    CHECK (idOf (modDst (23)) == "mod24_dst");
    CHECK (idOf (modAmt (11)) == "mod12_amt");
    CHECK (idOf (fxSlotType (7)) == "fx8_type");
    CHECK (idOf (seqStepParam (0, SeqStepParam::Pitch)) == "seq1_pitch");
    CHECK (idOf (seqStepParam (15, SeqStepParam::Mod)) == "seq16_mod");

    CHECK (nameOf (oscParam (0, OscParam::Pos)) == "Osc A Position");
    CHECK (nameOf (filterParam (1, FilterParam::Cutoff)) == "Filter 2 Cutoff");
    CHECK (nameOf (envParam (0, EnvParam::A)) == "Env 1 Attack");
    CHECK (nameOf (lfoParam (2, LfoParam::Rate)) == "LFO 3 Rate");
    CHECK (nameOf (modSrc (6)) == "Mod 7 Source");
    CHECK (nameOf (seqStepParam (11, SeqStepParam::Pitch)) == "Step 12 Pitch");
    CHECK (nameOf (fxSlotType (2)) == "FX Slot 3");
    CHECK (std::string (info (fxSlotType (2)).shortName) == "Slot 3");
    CHECK (std::string (info (oscParam (0, OscParam::Pos)).shortName) == "Position");
    CHECK (std::string (info (envParam (0, EnvParam::A)).group) == "Env 1");

    // Every instance block is contiguous and in order.
    for (int o = 0; o < kNumOscs; ++o)
        for (int w = 0; w < (int) OscParam::Count; ++w)
            CHECK (std::string (info (oscParam (o, (OscParam) w)).group) == (o == 0 ? "Osc A" : "Osc B"));
    for (int s = 0; s < kNumSeqSteps; ++s)
        CHECK (idOf (seqStepParam (s, SeqStepParam::Vel)) == "seq" + std::to_string (s + 1) + "_vel");
    for (int s = 0; s < kModSlots; ++s)
        CHECK (idOf (modAmt (s)) == "mod" + std::to_string (s + 1) + "_amt");
}

AP_TEST (params_normalised_round_trip)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const P p = (P) i;
        const auto& in = info (p);
        const double range = (double) in.max - (double) in.min;

        std::vector<float> plains { in.min, in.max, in.def };
        if (in.midpoint != 0.0f) plains.push_back (in.midpoint);
        for (int k = 1; k < 10; ++k) plains.push_back (in.min + (float) (range * k / 10.0));

        for (float v : plains)
        {
            if (in.kind != ParamKind::Float) v = std::floor (v + 0.5f);
            const float n = toNormalised (p, v);
            CHECK (n >= 0.0f && n <= 1.0f);
            const float back = fromNormalised (p, n);
            if (in.kind == ParamKind::Float) CHECK_NEAR (back, v, 2.0e-5 * range + 1.0e-6);
            else CHECK_EQ (back, v);
        }

        CHECK_EQ (toNormalised (p, in.min), 0.0f);
        CHECK_EQ (toNormalised (p, in.max), 1.0f);
        CHECK_EQ (fromNormalised (p, 0.0f), in.min);
        CHECK_EQ (fromNormalised (p, 1.0f), in.max);
        CHECK_EQ (toNormalised (p, in.max + 1000.0f), 1.0f);   // clamped
        CHECK_EQ (toNormalised (p, in.min - 1000.0f), 0.0f);

        if (in.midpoint != 0.0f)
            CHECK_NEAR (toNormalised (p, in.midpoint), 0.5, 1.0e-5);

        // Normalised -> plain -> normalised is monotonic and close for continuous params.
        float last = -std::numeric_limits<float>::infinity();
        for (int k = 0; k <= 20; ++k)
        {
            const float n = (float) k / 20.0f;
            const float plain = fromNormalised (p, n);
            CHECK (plain >= last);
            last = plain;
            if (in.kind == ParamKind::Float) CHECK_NEAR (toNormalised (p, plain), n, 1.0e-5);
        }
    }

    // JUCE setSkewForCentre formula, checked by hand for one row.
    const P cutoff = filterParam (0, FilterParam::Cutoff);
    const double skew = std::log (0.5) / std::log ((std::sqrt (20.0 * 20000.0) - 20.0) / (20000.0 - 20.0));
    CHECK_NEAR (toNormalised (cutoff, 1000.0f), std::pow ((1000.0 - 20.0) / 19980.0, skew), 1.0e-6);
    CHECK_NEAR (fromNormalised (cutoff, 0.25f), 20.0 + 19980.0 * std::pow (0.25, 1.0 / skew), 1.0e-2);

    // Discrete kinds snap like the host's stepped parameters.
    CHECK_EQ (fromNormalised (P::Voices, 0.5f), 17.0f);   // 1 + round (0.5 * 31)
    CHECK_EQ (fromNormalised (oscParam (0, OscParam::On), 0.49f), 0.0f);
    CHECK_EQ (fromNormalised (oscParam (0, OscParam::On), 0.5f), 1.0f);
    CHECK_NEAR (toNormalised (filterParam (0, FilterParam::Type), 1.0f), 0.1, 1.0e-6);
}

AP_TEST (params_clamp_to_range)
{
    CHECK_EQ (clampToRange (P::Voices, 40.0f), 32.0f);
    CHECK_EQ (clampToRange (P::Voices, 3.4f), 3.0f);
    CHECK_EQ (clampToRange (P::Voices, 3.6f), 4.0f);
    CHECK_EQ (clampToRange (P::Master, 100.0f), 12.0f);
    CHECK_EQ (clampToRange (P::Master, -3.25f), -3.25f);
    CHECK_EQ (clampToRange (P::Master, std::nanf ("")), -6.0f);
    CHECK_EQ (clampToRange (filterParam (0, FilterParam::Type), 42.0f), 10.0f);
}

AP_TEST (params_mod_sources)
{
    CHECK (std::string (modSourceName (ModSource::None)) == "None");
    CHECK (std::string (modSourceName (ModSource::Lfo1)) == "LFO 1");
    CHECK (std::string (modSourceName (ModSource::Env3)) == "Env 3");
    CHECK (std::string (modSourceName (ModSource::Velocity)) == "Velocity");
    CHECK (std::string (modSourceName (ModSource::ModWheel)) == "Mod Wheel");
    CHECK (std::string (modSourceName (ModSource::PitchBend)) == "Pitch Bend");
    CHECK (std::string (modSourceName (ModSource::Macro8)) == "Macro 8");
    CHECK (std::string (modSourceName (ModSource::Ball)) == "Ball");
    CHECK (std::string (modSourceName (ModSource::Alive)) == "Alive");
    CHECK (std::string (modSourceName (ModSource::SeqMod)) == "Seq Mod");

    const auto& src = info (modSrc (0));
    CHECK_EQ (src.numChoices, (int) ModSource::Count);
    for (int s = 0; s < (int) ModSource::Count; ++s)
        CHECK (std::string (src.choices[s]) == modSourceName ((ModSource) s));
}

AP_TEST (params_mod_destinations)
{
    static const char* const expected[] = {
        "None",
        "Master", "Pitch",
        "Osc A Position", "Osc A Warp Amount", "Osc A Fine", "Osc A Pitch", "Osc A Level", "Osc A Pan", "Osc A Detune", "Osc A Spread", "Osc A Blend",
        "Osc B Position", "Osc B Warp Amount", "Osc B Fine", "Osc B Pitch", "Osc B Level", "Osc B Pan", "Osc B Detune", "Osc B Spread", "Osc B Blend",
        "Sub Level", "Noise",
        "Filter 1 Cutoff", "Filter 1 Resonance", "Filter 1 Drive", "Filter 1 Mix",
        "Filter 2 Cutoff", "Filter 2 Resonance", "Filter 2 Drive", "Filter 2 Mix",
        "Env 1 Attack", "Env 1 Decay", "Env 1 Release", "Env 2 Attack", "Env 2 Decay", "Env 2 Release", "Env 3 Attack", "Env 3 Decay", "Env 3 Release",
        "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "LFO 4 Rate",
        "Dist Drive", "Dist Tone", "Dist Mix",
        "Chorus Rate", "Chorus Depth", "Chorus Mix",
        "Phaser Rate", "Phaser Depth", "Phaser Mix",
        "Flanger Rate", "Flanger Depth", "Flanger Mix",
        "Delay Time", "Delay Feedback", "Delay Mix",
        "Reverb Size", "Reverb Decay", "Reverb Mix",
        "EQ Low", "EQ Mid", "EQ Mid Freq", "EQ High",
        "Sympathetic", "Symp Decay", "Symp Brightness"
    };
    constexpr int n = (int) (sizeof (expected) / sizeof (expected[0])) - 1;

    CHECK_EQ (n, 68);
    CHECK_EQ (numModDestinations(), 68);
    CHECK_EQ (kNumModDestinations, 68);

    int modulatable = 0;
    for (int i = 0; i < kNumParams; ++i)
        if (info ((P) i).modulatable) ++modulatable;
    CHECK_EQ (modulatable, 68);

    for (int d = 0; d <= n; ++d)
        CHECK (std::string (modDestinationName (d)) == expected[d]);

    CHECK (modDestination (0) == P::Count);
    CHECK (modDestination (69) == P::Count);
    CHECK (modDestination (-1) == P::Count);
    int lastIndex = -1;
    for (int d = 1; d <= n; ++d)
    {
        const P p = modDestination (d);
        CHECK ((int) p > lastIndex);   // table order
        lastIndex = (int) p;
        CHECK (info (p).modulatable);
        CHECK_EQ (modDestinationIndexOf (p), d);
        CHECK (std::string (info (p).name) == modDestinationName (d));
    }
    CHECK_EQ (modDestinationIndexOf (P::Voices), 0);
    CHECK_EQ (modDestinationIndexOf (filterParam (0, FilterParam::Key)), 0);
    CHECK (modDestination (1) == P::Master);
    CHECK (modDestination (3) == oscParam (0, OscParam::Pos));
    CHECK (modDestination (68) == P::SympBright);

    // The mod{n}_dst choice list is "None" + the destination names.
    for (int s = 0; s < kModSlots; ++s)
    {
        const auto& dst = info (modDst (s));
        CHECK_EQ (dst.numChoices, 69);
        CHECK_EQ (dst.max, 68.0f);
        for (int d = 0; d <= n; ++d)
            CHECK (std::string (dst.choices[d]) == expected[d]);
    }
}
