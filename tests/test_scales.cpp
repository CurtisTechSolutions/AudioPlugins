#include "TestHarness.h"

#include <ap/Math.h>
#include <ap/Scales.h>

using namespace ap;

AP_TEST (midi_hz_round_trip)
{
    CHECK_NEAR (midiToHz (69.0f), 440.0f, 1e-3);
    CHECK_NEAR (midiToHz (60.0f), 261.6256f, 1e-2);
    CHECK_NEAR (hzToMidi (880.0f), 81.0f, 1e-4);
    for (int n = 24; n < 108; ++n)
        CHECK_NEAR (hzToMidi (midiToHz ((float) n)), (float) n, 1e-3);
}

AP_TEST (db_gain_round_trip)
{
    CHECK_NEAR (dbToGain (0.0f), 1.0f, 1e-6);
    CHECK_NEAR (dbToGain (-6.0206f), 0.5f, 1e-4);
    CHECK_NEAR (gainToDb (2.0f), 6.0206f, 1e-3);
    CHECK_NEAR (gainToDb (0.0f), -120.0f, 1e-6);
}

AP_TEST (scale_membership)
{
    const auto cMajor = Scale::fromType (ScaleType::Major, 0);
    CHECK (cMajor.contains (60));     // C
    CHECK (! cMajor.contains (61));   // C#
    CHECK (cMajor.contains (64));     // E
    CHECK (cMajor.contains (71));     // B
    CHECK (cMajor.contains (72));     // C (octave)

    const auto aMinor = Scale::fromType (ScaleType::NaturalMinor, 9);
    for (int n = 57; n < 69; ++n)     // A minor and C major share pitch classes
        CHECK_EQ ((int) aMinor.contains (n), (int) cMajor.contains (n));

    const auto fSharpMajorPent = Scale::fromType (ScaleType::MajorPentatonic, 6);
    CHECK (fSharpMajorPent.contains (66));   // F#
    CHECK (fSharpMajorPent.contains (68));   // G#
    CHECK (! fSharpMajorPent.contains (67)); // G
}

AP_TEST (scale_nearest)
{
    const auto cMajor = Scale::fromType (ScaleType::Major, 0);
    CHECK_EQ (cMajor.nearest (60.0f), 60);
    CHECK_EQ (cMajor.nearest (60.9f), 60);    // 61 is C#, not in scale; closer to 60 than 62
    CHECK_EQ (cMajor.nearest (61.2f), 62);
    CHECK_EQ (cMajor.nearest (61.0f), 60);    // exact tie goes to the lower note
    CHECK_EQ (cMajor.nearest (63.4f), 64);

    const auto chromatic = Scale::fromType (ScaleType::Chromatic, 0);
    CHECK_EQ (chromatic.nearest (63.4f), 63);
    CHECK_EQ (chromatic.nearest (63.6f), 64);

    // Custom mask with only the root enabled -> always snaps to a C.
    const auto onlyC = Scale::fromType (ScaleType::Custom, 0, 0x001);
    CHECK_EQ (onlyC.nearest (65.0f), 60);
    CHECK_EQ (onlyC.nearest (67.0f), 72);

    // Empty custom mask is treated as chromatic rather than exploding.
    const auto empty = Scale::fromType (ScaleType::Custom, 0, 0x000);
    CHECK_EQ (empty.nearest (65.3f), 65);
}
