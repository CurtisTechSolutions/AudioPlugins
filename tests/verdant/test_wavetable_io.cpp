// WavetableIO: WAV parsing (PCM 8/16/24/32, float 32/64, extensible, stereo,
// odd chunks, clm frame sizes incl. resampling), frame count rules, export ->
// import bit-exact round trip and rejection of malformed input.

#include "TestHarness.h"

#include "engine/WavetableIO.h"

#include <ap/Math.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    struct WavSpec
    {
        uint16_t format = 1;          // 1 PCM, 3 float, 0xFFFE extensible
        uint16_t subFormat = 1;       // for extensible
        int channels = 1;
        int bits = 16;
        std::string clm;              // empty = no clm chunk
        bool oddJunkChunk = false;    // an unknown odd-sized chunk before the data (tests padding)
    };

    void put16 (std::vector<uint8_t>& v, uint32_t x) { v.push_back ((uint8_t) (x & 0xFF)); v.push_back ((uint8_t) ((x >> 8) & 0xFF)); }
    void put32 (std::vector<uint8_t>& v, uint32_t x) { for (int b = 0; b < 4; ++b) v.push_back ((uint8_t) ((x >> (8 * b)) & 0xFF)); }
    void putId (std::vector<uint8_t>& v, const char* id) { v.insert (v.end(), id, id + 4); }

    void putSample (std::vector<uint8_t>& v, const WavSpec& s, double x)
    {
        const bool isFloat = s.format == 3 || (s.format == 0xFFFE && s.subFormat == 3);
        if (isFloat && s.bits == 32) { put32 (v, std::bit_cast<uint32_t> ((float) x)); return; }
        if (isFloat && s.bits == 64)
        {
            const uint64_t b = std::bit_cast<uint64_t> (x);
            for (int i = 0; i < 8; ++i) v.push_back ((uint8_t) ((b >> (8 * i)) & 0xFF));
            return;
        }
        x = std::clamp (x, -1.0, 1.0);
        switch (s.bits)
        {
            case 8:  v.push_back ((uint8_t) std::clamp ((int) std::lround (x * 128.0) + 128, 0, 255)); break;
            case 16: put16 (v, (uint32_t) (int32_t) std::clamp ((long) std::lround (x * 32768.0), -32768L, 32767L)); break;
            case 24:
            {
                const int32_t q = (int32_t) std::clamp ((long) std::lround (x * 8388608.0), -8388608L, 8388607L);
                for (int i = 0; i < 3; ++i) v.push_back ((uint8_t) (((uint32_t) q >> (8 * i)) & 0xFF));
                break;
            }
            default: put32 (v, (uint32_t) (int32_t) std::clamp ((long long) std::llround (x * 2147483648.0), -2147483648LL, 2147483647LL)); break;
        }
    }

    /** channels[c][i]; every channel the same length. */
    std::vector<uint8_t> makeWav (const WavSpec& s, const std::vector<std::vector<double>>& channels)
    {
        std::vector<uint8_t> v;
        putId (v, "RIFF"); put32 (v, 0); putId (v, "WAVE");

        const bool ext = s.format == 0xFFFE;
        putId (v, "fmt ");
        put32 (v, ext ? 40 : 16);
        put16 (v, s.format);
        put16 (v, (uint32_t) s.channels);
        put32 (v, 48000);
        put32 (v, (uint32_t) (48000 * s.channels * s.bits / 8));
        put16 (v, (uint32_t) (s.channels * s.bits / 8));
        put16 (v, (uint32_t) s.bits);
        if (ext)
        {
            put16 (v, 22);                 // cbSize
            put16 (v, (uint32_t) s.bits);  // valid bits
            put32 (v, 0);                  // channel mask
            put16 (v, s.subFormat);        // GUID: first two bytes = format code
            const uint8_t rest[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
            v.insert (v.end(), rest, rest + 14);
        }

        if (s.oddJunkChunk)
        {
            putId (v, "junk"); put32 (v, 3);
            v.push_back ('a'); v.push_back ('b'); v.push_back ('c'); v.push_back (0);   // + pad byte
        }

        if (! s.clm.empty())
        {
            putId (v, "clm ");
            put32 (v, (uint32_t) s.clm.size());
            v.insert (v.end(), s.clm.begin(), s.clm.end());
            if (s.clm.size() & 1u) v.push_back (0);
        }

        putId (v, "data");
        const size_t n = channels.empty() ? 0 : channels[0].size();
        put32 (v, (uint32_t) (n * (size_t) s.channels * (size_t) s.bits / 8));
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < s.channels; ++c)
                putSample (v, s, channels[(size_t) c][i]);
        if (v.size() & 1u) v.push_back (0);

        const uint32_t riff = (uint32_t) v.size() - 8u;
        for (int b = 0; b < 4; ++b) v[(size_t) (4 + b)] = (uint8_t) ((riff >> (8 * b)) & 0xFF);
        return v;
    }

    /** n samples of frames back to back: frame f = 0.8 sin (2 pi (f + 1) i / frameSize) (harmonic f + 1). */
    std::vector<double> harmonicFrames (int frames, int frameSize, double amp = 0.8)
    {
        std::vector<double> x ((size_t) frames * (size_t) frameSize);
        for (int f = 0; f < frames; ++f)
            for (int i = 0; i < frameSize; ++i)
                x[(size_t) f * (size_t) frameSize + (size_t) i] = amp * std::sin (kTwoPi * (f + 1) * i / frameSize);
        return x;
    }

    double correlation (const float* a, const std::vector<double>& b)
    {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = 0; i < b.size(); ++i) { ab += a[i] * b[i]; aa += (double) a[i] * a[i]; bb += b[i] * b[i]; }
        return aa > 0 && bb > 0 ? ab / std::sqrt (aa * bb) : 0.0;
    }

    std::vector<double> sineCycle (int harmonic, double amp = 1.0, double phase = 0.0)
    {
        std::vector<double> v ((size_t) kFrameSize);
        for (int i = 0; i < kFrameSize; ++i) v[(size_t) i] = amp * std::sin (kTwoPi * harmonic * i / kFrameSize + phase);
        return v;
    }

    WavImportResult importBytes (const std::vector<uint8_t>& b, const std::string& name = "T")
    {
        return importWavetableWav (b.data(), b.size(), name);
    }
}

AP_TEST (wavio_pcm16_with_clm)
{
    WavSpec s; s.bits = 16; s.clm = "<!>2048 01000000 wavetable (www.serum.com)";
    const auto x = harmonicFrames (3, 2048);
    const auto r = importBytes (makeWav (s, { x }), "Three");
    CHECK (r.error.empty());
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK (r.hadClmChunk);
    CHECK_EQ (r.frameSize, 2048);
    CHECK_EQ (r.table->numFrames(), 3);
    CHECK (r.table->name() == "Three");

    double maxErr = 0.0;
    for (int f = 0; f < 3; ++f)
        for (int i = 0; i < kFrameSize; ++i)
            maxErr = std::max (maxErr, std::fabs ((double) r.table->frame (f)[i] - x[(size_t) f * 2048 + (size_t) i]));
    CHECK (maxErr <= 1.0 / 32768.0);   // within 1 LSB
}

AP_TEST (wavio_float32_without_clm_uses_2048)
{
    WavSpec s; s.format = 3; s.bits = 32;
    const auto x = harmonicFrames (2, 2048);
    const auto r = importBytes (makeWav (s, { x }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK (! r.hadClmChunk);
    CHECK_EQ (r.frameSize, 2048);
    CHECK_EQ (r.table->numFrames(), 2);
    for (int i = 0; i < kFrameSize; ++i)
        CHECK_EQ (r.table->frame (1)[i], (float) x[2048 + (size_t) i]);   // float data is taken verbatim
}

AP_TEST (wavio_partial_last_frame_dropped)
{
    WavSpec s; s.format = 3; s.bits = 32;
    auto x = harmonicFrames (2, 2048);
    x.resize (x.size() + 1000, 0.25);
    const auto r = importBytes (makeWav (s, { x }));
    CHECK (r.table != nullptr && r.table->numFrames() == 2);
}

AP_TEST (wavio_clm_1024_resampled_keeps_harmonics)
{
    WavSpec s; s.format = 3; s.bits = 32; s.clm = "<!>  1024 10000000 wavetable (other)";   // extra whitespace on purpose
    const auto x = harmonicFrames (3, 1024);   // frame 2 = harmonic 3
    const auto r = importBytes (makeWav (s, { x }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK (r.hadClmChunk);
    CHECK_EQ (r.frameSize, 1024);
    CHECK_EQ (r.table->numFrames(), 3);
    for (int f = 0; f < 3; ++f)
    {
        const double c = correlation (r.table->frame (f), sineCycle (f + 1));
        CHECK (c > 0.999);
        float peak = 0.0f;
        for (int i = 0; i < kFrameSize; ++i) peak = std::max (peak, std::fabs (r.table->frame (f)[i]));
        CHECK_NEAR (peak, 0.8, 0.01);   // amplitude kept through the resample
    }
}

AP_TEST (wavio_clm_non_power_of_two_and_large)
{
    {
        WavSpec s; s.format = 3; s.bits = 32; s.clm = "<!>600";
        const auto x = harmonicFrames (2, 600);
        const auto r = importBytes (makeWav (s, { x }));
        CHECK (r.table != nullptr && r.table->numFrames() == 2 && r.frameSize == 600);
        if (r.table != nullptr)
            CHECK (correlation (r.table->frame (1), sineCycle (2)) > 0.999);
    }
    {
        WavSpec s; s.format = 3; s.bits = 32; s.clm = "<!>4096 00000000";
        const auto x = harmonicFrames (2, 4096);
        const auto r = importBytes (makeWav (s, { x }));
        CHECK (r.table != nullptr && r.table->numFrames() == 2 && r.frameSize == 4096);
        if (r.table != nullptr)
            CHECK (correlation (r.table->frame (1), sineCycle (2)) > 0.999);
    }
}

AP_TEST (wavio_bad_clm_falls_back_to_2048)
{
    WavSpec s; s.format = 3; s.bits = 32; s.clm = "no marker here";
    const auto r = importBytes (makeWav (s, { harmonicFrames (2, 2048) }));
    CHECK (r.table != nullptr && r.table->numFrames() == 2);
    CHECK (! r.hadClmChunk);
}

AP_TEST (wavio_bit_depths_decode)
{
    const auto x = harmonicFrames (1, 2048, 0.7);
    struct Case { uint16_t format; int bits; double tol; };
    for (const Case c : { Case { 1, 8, 1.0 / 128.0 }, Case { 1, 24, 1.0 / 8388608.0 }, Case { 1, 32, 1.0e-7 }, Case { 3, 64, 1.0e-7 } })
    {
        WavSpec s; s.format = c.format; s.bits = c.bits;
        const auto r = importBytes (makeWav (s, { x }));
        CHECK (r.error.empty());
        CHECK (r.table != nullptr);
        if (r.table == nullptr) continue;
        double maxErr = 0.0;
        for (int i = 0; i < kFrameSize; ++i) maxErr = std::max (maxErr, std::fabs ((double) r.table->frame (0)[i] - x[(size_t) i]));
        CHECK (maxErr <= c.tol + 1.0e-7);
    }

    // 24-bit negative full scale and sign extension.
    WavSpec s; s.bits = 24;
    std::vector<double> edge ((size_t) kFrameSize, 0.0);
    edge[0] = -1.0; edge[1] = 8388607.0 / 8388608.0; edge[2] = -0.5;
    const auto r = importBytes (makeWav (s, { edge }));
    CHECK (r.table != nullptr);
    if (r.table != nullptr)
    {
        CHECK_EQ (r.table->frame (0)[0], -1.0f);
        CHECK_NEAR (r.table->frame (0)[1], 1.0, 1.0e-6);
        CHECK_EQ (r.table->frame (0)[2], -0.5f);
    }
}

AP_TEST (wavio_extensible_and_odd_chunk_padding)
{
    const auto x = harmonicFrames (2, 2048);
    {
        WavSpec s; s.format = 0xFFFE; s.subFormat = 3; s.bits = 32; s.oddJunkChunk = true; s.clm = "<!>2048";   // odd clm too (7 chars)
        const auto r = importBytes (makeWav (s, { x }));
        CHECK (r.error.empty());
        CHECK (r.table != nullptr && r.table->numFrames() == 2 && r.hadClmChunk);
        if (r.table != nullptr) CHECK_EQ (r.table->frame (1)[100], (float) x[2048 + 100]);
    }
    {
        WavSpec s; s.format = 0xFFFE; s.subFormat = 1; s.bits = 16;
        const auto r = importBytes (makeWav (s, { x }));
        CHECK (r.table != nullptr && r.table->numFrames() == 2);
    }
}

AP_TEST (wavio_stereo_uses_first_channel)
{
    WavSpec s; s.bits = 16; s.channels = 2;
    const auto left = harmonicFrames (1, 2048, 0.5);
    std::vector<double> right (left.size(), -0.9);
    const auto r = importBytes (makeWav (s, { left, right }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK_EQ (r.table->numFrames(), 1);
    CHECK (correlation (r.table->frame (0), sineCycle (1)) > 0.9999);
}

AP_TEST (wavio_short_file_is_one_frame)
{
    WavSpec s; s.format = 3; s.bits = 32;
    std::vector<double> x (300);
    for (int i = 0; i < 300; ++i) x[(size_t) i] = 0.6 * std::sin (kTwoPi * i / 300.0);
    const auto r = importBytes (makeWav (s, { x }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK_EQ (r.table->numFrames(), 1);
    CHECK (correlation (r.table->frame (0), sineCycle (1)) > 0.999);   // the 300-sample cycle stretched to 2048
}

AP_TEST (wavio_too_many_frames_thinned)
{
    WavSpec s; s.format = 3; s.bits = 32;
    std::vector<double> x ((size_t) 300 * 2048);
    for (int f = 0; f < 300; ++f)
        for (int i = 0; i < 2048; ++i) x[(size_t) f * 2048 + (size_t) i] = (double) f / 300.0;   // DC level = frame index
    const auto r = importBytes (makeWav (s, { x }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK_EQ (r.table->numFrames(), kMaxFrames);
    CHECK_EQ (r.table->frame (0)[0], 0.0f);
    CHECK_EQ (r.table->frame (kMaxFrames - 1)[0], (float) (299.0 / 300.0));   // first and last frames kept
    bool ascending = true;
    for (int f = 1; f < kMaxFrames; ++f) ascending = ascending && r.table->frame (f)[0] > r.table->frame (f - 1)[0];
    CHECK (ascending);
}

AP_TEST (wavio_export_import_round_trip_bit_exact)
{
    const int frames = 5;
    std::vector<float> data ((size_t) frames * kFrameSize);
    ap::Rng rng; rng.state = 0xC0FFEEu;
    for (auto& v : data) v = rng.nextBipolar() * 0.9f;
    data[3] = 1.0e-30f;   // a denormal-ish tiny value survives too
    const auto t = Wavetable::fromFrames (data.data(), frames, "Round");
    CHECK (t != nullptr);
    if (t == nullptr) return;

    const auto bytes = exportWavetableWav (*t);
    CHECK (bytes.size() > (size_t) frames * kFrameSize * 4);
    const auto r = importBytes (bytes, "Back");
    CHECK (r.error.empty());
    CHECK (r.hadClmChunk);
    CHECK_EQ (r.frameSize, 2048);
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK_EQ (r.table->numFrames(), frames);
    bool exact = true;
    for (int f = 0; f < frames; ++f)
        for (int i = 0; i < kFrameSize; ++i)
            exact = exact && std::bit_cast<uint32_t> (r.table->frame (f)[i]) == std::bit_cast<uint32_t> (data[(size_t) f * kFrameSize + (size_t) i]);
    CHECK (exact);

    // The clm text is the documented one.
    const std::string all (bytes.begin(), bytes.end());
    CHECK (all.find ("<!>2048 01000000 wavetable (verdant)") != std::string::npos);
}

AP_TEST (wavio_rejects_malformed)
{
    std::vector<uint8_t> garbage (500);
    ap::Rng rng; rng.state = 77u;
    for (auto& b : garbage) b = (uint8_t) (rng.nextU32() & 0xFF);
    auto r = importBytes (garbage);
    CHECK (! r.error.empty());
    CHECK (r.table == nullptr);

    r = importWavetableWav (nullptr, 0, "x");
    CHECK (! r.error.empty() && r.table == nullptr);

    // Valid header, no fmt chunk.
    std::vector<uint8_t> noFmt = { 'R', 'I', 'F', 'F', 12, 0, 0, 0, 'W', 'A', 'V', 'E', 'd', 'a', 't', 'a', 0, 0, 0, 0 };
    r = importBytes (noFmt);
    CHECK (! r.error.empty() && r.table == nullptr);

    // Unsupported format (A-law = 6).
    std::vector<uint8_t> alaw = makeWav (WavSpec { 1, 1, 1, 8, "", false }, { harmonicFrames (1, 2048) });
    alaw[20] = 6;   // patch the format tag
    r = importBytes (alaw);
    CHECK (! r.error.empty() && r.table == nullptr);

    // Truncated fmt chunk.
    auto truncated = makeWav (WavSpec {}, { harmonicFrames (1, 2048) });
    truncated.resize (30);
    r = importBytes (truncated);
    CHECK (! r.error.empty() && r.table == nullptr);

    // Empty data chunk.
    r = importBytes (makeWav (WavSpec {}, { std::vector<double> {} }));
    CHECK (! r.error.empty() && r.table == nullptr);
}

AP_TEST (wavio_truncated_data_chunk_is_tolerated)
{
    WavSpec s; s.format = 3; s.bits = 32;
    auto bytes = makeWav (s, { harmonicFrames (2, 2048) });
    bytes.resize (bytes.size() - 4096);   // the data chunk claims more than the file holds
    const auto r = importBytes (bytes);
    CHECK (r.table != nullptr && r.table->numFrames() == 1);
}

AP_TEST (wavio_survives_mutation_and_truncation)
{
    // Deterministic fuzzing: every result is either an error with a null table or a valid table, never a crash
    // (run under ASan/UBSan to make this bite).
    WavSpec s; s.bits = 16; s.clm = "<!>512 wavetable"; s.oddJunkChunk = true;
    const auto valid = makeWav (s, { harmonicFrames (4, 512) });
    ap::Rng rng; rng.state = 0xF00Du;
    int ok = 0, errors = 0;
    for (int iter = 0; iter < 3000; ++iter)
    {
        auto b = valid;
        const int flips = 1 + (int) (rng.nextU32() % 8);
        for (int f = 0; f < flips; ++f)
        {
            const size_t at = (size_t) (rng.nextU32() % (uint32_t) std::min<size_t> (b.size(), 120));   // headers and chunk sizes
            b[at] = (uint8_t) (rng.nextU32() & 0xFF);
        }
        if (iter % 3 == 0) b.resize ((size_t) (rng.nextU32() % (uint32_t) b.size()));
        const auto r = importBytes (b);
        if (r.table != nullptr)
        {
            ++ok;
            CHECK (r.error.empty());
            CHECK (r.table->numFrames() >= 1 && r.table->numFrames() <= kMaxFrames);
            bool finite = true;
            for (int i = 0; i < kFrameSize; ++i) finite = finite && std::isfinite (r.table->frame (0)[i]);
            CHECK (finite);
        }
        else
        {
            ++errors;
            CHECK (! r.error.empty());
        }
    }
    CHECK (ok > 0 && errors > 0);
}
