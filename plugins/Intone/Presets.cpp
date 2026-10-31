#include "Presets.h"

#include "PluginProcessor.h"

#include <ap/Scales.h>

#include <initializer_list>

namespace intone
{

namespace
{
    using ap::plug::FactoryPreset;
    using ap::plug::PresetValue;
    using ST = ap::ScaleType;

    float scaleIndex (ST s) { return (float) (int) s; }
    constexpr float kLow = 0.0f, kHigh = 2.0f;   // Voice Range choices (Mid is the default)
    enum : int { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };

    // The custom-scale toggles by pitch class (ids::note() builds the same strings).
    constexpr const char* kNoteIds[12] = { "note_0", "note_1", "note_2", "note_3", "note_4", "note_5",
                                           "note_6", "note_7", "note_8", "note_9", "note_10", "note_11" };

    /** A Custom scale that keeps only the listed pitch classes. */
    std::vector<PresetValue> customNotes (std::initializer_list<int> keep)
    {
        std::vector<PresetValue> v { { ids::scale, scaleIndex (ST::Custom) } };
        for (int pc = 0; pc < 12; ++pc)
        {
            bool on = false;
            for (int k : keep) on = on || k == pc;
            if (! on) v.push_back ({ kNoteIds[pc], 0.0f });
        }
        return v;
    }

    FactoryPreset make (const char* name, const char* category, std::initializer_list<PresetValue> values,
                        std::vector<PresetValue> extra = {})
    {
        FactoryPreset p { name, category, values };
        p.values.insert (p.values.end(), extra.begin(), extra.end());
        return p;
    }
}

std::vector<FactoryPreset> factoryPresets()
{
    using namespace ids;
    return {
        { "Default", "", {} },

        // ---- Correction: from invisible to tight, chromatic so they work in any key.
        make ("Transparent Pitch", "Correction",
              { { speed, 120.0f }, { amount, 50.0f } }),
        make ("Natural Correction", "Correction",
              { { speed, 60.0f }, { amount, 70.0f } }),
        make ("Studio Tight", "Correction",
              { { speed, 25.0f }, { amount, 90.0f }, { sensitivity, 55.0f } }),
        make ("Gentle Drift Fix", "Correction",
              { { speed, 250.0f }, { amount, 40.0f } }),
        make ("Low Voice Natural", "Correction",
              { { tracking, kLow }, { speed, 50.0f }, { amount, 75.0f }, { lowCut, 60.0f } }),
        make ("High Voice Natural", "Correction",
              { { tracking, kHigh }, { speed, 45.0f }, { amount, 75.0f }, { lowCut, 120.0f } }),

        // ---- Hard tune: instant snapping.
        make ("Hard Tune Chromatic", "Hard Tune",
              { { speed, 0.0f }, { amount, 100.0f }, { sensitivity, 60.0f } }),
        make ("Robot Pop (C Major)", "Hard Tune",
              { { key, (float) C }, { scale, scaleIndex (ST::Major) }, { speed, 0.0f }, { sensitivity, 60.0f },
                { presence, 2.0f }, { air, 3.0f } }),
        make ("Trap Minor (A)", "Hard Tune",
              { { key, (float) A }, { scale, scaleIndex (ST::NaturalMinor) }, { speed, 0.0f }, { sensitivity, 65.0f },
                { deessAmount, 35.0f }, { compThresh, -24.0f }, { compRatio, 4.0f }, { compMakeup, 4.0f } }),
        make ("Pentatonic Snap (A)", "Hard Tune",
              { { key, (float) A }, { scale, scaleIndex (ST::MinorPentatonic) }, { speed, 5.0f } }),
        make ("Blues Snap (E)", "Hard Tune",
              { { key, (float) E }, { scale, scaleIndex (ST::Blues) }, { speed, 10.0f } }),
        make ("Dorian Glide (D)", "Hard Tune",
              { { key, (float) D }, { scale, scaleIndex (ST::Dorian) }, { speed, 35.0f } }),

        // ---- Vocal chains: correction plus de-esser, compressor and tone.
        make ("Pop Vocal Chain", "Vocal Chain",
              { { speed, 30.0f }, { amount, 85.0f }, { deessAmount, 40.0f }, { deessFreq, 7000.0f },
                { compThresh, -22.0f }, { compRatio, 4.0f }, { compAttack, 5.0f }, { compRelease, 100.0f },
                { compMakeup, 4.0f }, { lowCut, 100.0f }, { body, -1.0f }, { presence, 3.0f }, { air, 4.0f } }),
        make ("Rap Vocal Chain", "Vocal Chain",
              { { speed, 15.0f }, { amount, 90.0f }, { deessAmount, 35.0f }, { compThresh, -24.0f },
                { compRatio, 5.0f }, { compAttack, 3.0f }, { compRelease, 80.0f }, { compMakeup, 6.0f },
                { lowCut, 110.0f }, { body, 1.0f }, { presence, 4.0f }, { air, 2.0f } }),
        make ("R&B Silk", "Vocal Chain",
              { { speed, 40.0f }, { amount, 80.0f }, { deessAmount, 30.0f }, { compThresh, -20.0f },
                { compRatio, 3.0f }, { compAttack, 12.0f }, { compRelease, 160.0f }, { compMakeup, 3.0f },
                { body, 2.0f }, { presence, 1.0f }, { air, 5.0f } }),
        make ("Rock Vocal Grit", "Vocal Chain",
              { { speed, 50.0f }, { amount, 70.0f }, { deessAmount, 25.0f }, { compThresh, -28.0f },
                { compRatio, 6.0f }, { compAttack, 2.0f }, { compRelease, 90.0f }, { compMakeup, 7.0f },
                { lowCut, 120.0f }, { body, 2.0f }, { presence, 5.0f }, { air, -1.0f } }),
        make ("Choir Blend", "Vocal Chain",
              { { speed, 80.0f }, { amount, 60.0f }, { deessAmount, 25.0f }, { compRatio, 2.0f },
                { compThresh, -16.0f }, { body, -2.0f }, { air, 3.0f } }),
        make ("Podcast Voice", "Vocal Chain",
              { { amount, 0.0f }, { deessAmount, 45.0f }, { deessFreq, 6000.0f }, { compThresh, -26.0f },
                { compRatio, 4.0f }, { compAttack, 8.0f }, { compRelease, 150.0f }, { compMakeup, 6.0f },
                { lowCut, 90.0f }, { body, 2.0f }, { presence, 3.0f }, { air, 1.0f } }),
        make ("Broadcast Voice", "Vocal Chain",
              { { amount, 0.0f }, { tracking, kLow }, { deessAmount, 40.0f }, { compThresh, -30.0f },
                { compRatio, 6.0f }, { compAttack, 5.0f }, { compRelease, 120.0f }, { compMakeup, 9.0f },
                { lowCut, 70.0f }, { body, 3.0f }, { presence, 4.0f }, { air, 2.0f } }),

        // ---- Creative: shifting, blends and odd scales.
        make ("Octave Down Monster", "Creative",
              { { transpose, -12.0f }, { speed, 0.0f }, { body, 4.0f }, { air, -4.0f } }),
        make ("Octave Up Sprite", "Creative",
              { { transpose, 12.0f }, { speed, 10.0f }, { lowCut, 200.0f }, { air, -3.0f } }),
        make ("Fifth Above Blend", "Creative",
              { { transpose, 7.0f }, { speed, 10.0f }, { mix, 50.0f } }),
        make ("Fourth Below Blend", "Creative",
              { { transpose, -5.0f }, { speed, 10.0f }, { mix, 45.0f } }),
        make ("Whole-Tone Dream", "Creative",
              { { scale, scaleIndex (ST::WholeTone) }, { speed, 0.0f }, { mix, 80.0f }, { air, 4.0f } }),
        make ("Power Notes (C F G)", "Creative",
              { { speed, 0.0f }, { sensitivity, 60.0f } }, customNotes ({ C, F, G })),
        make ("Telephone Voice", "Creative",
              { { amount, 0.0f }, { compThresh, -30.0f }, { compRatio, 8.0f }, { compMakeup, 8.0f },
                { lowCut, 400.0f }, { body, -12.0f }, { presence, 8.0f }, { air, -12.0f } }),
    };
}

} // namespace intone
