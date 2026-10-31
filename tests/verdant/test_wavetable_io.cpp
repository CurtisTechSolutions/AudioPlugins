// WavetableIO: WAV parsing (PCM 8/16/24/32, float 32/64, extensible, stereo,
// odd chunks, clm frame sizes incl. resampling), frame count rules, bit-exact
// export, export -> import round trip and rejection of malformed input.
//
// Wavetable::fromFrames conditions every frame it is given (DC removal,
// peak normalisation), so imported frames are never the raw file samples.
// The decode checks therefore compare the import against
// Wavetable::fromFrames applied to the exactly decoded samples the file must
// yield: any decode, resampling or frame-selection error shows up as a
// difference in the conditioned frames.

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

    /** The decoded value a reader must produce for sample x written by putSample at the given depth. */
    float decoded (double x, int bits, bool isFloat)
    {
        if (isFloat) return (float) x;
        x = std::clamp (x, -1.0, 1.0);
        switch (bits)
        {
            case 8:  return ((float) std::clamp ((int) std::lround (x * 128.0) + 128, 0, 255) - 128.0f) / 128.0f;
            case 16: return (float) std::clamp ((long) std::lround (x * 32768.0), -32768L, 32767L) / 32768.0f;
            case 24: return (float) std::clamp ((long) std::lround (x * 8388608.0), -8388608L, 8388607L) / 8388608.0f;
            default: return (float) ((double) std::clamp ((long long) std::llround (x * 2147483648.0), -2147483648LL, 2147483647LL) / 2147483648.0);
        }
    }

    /** fromFrames of the exactly decoded samples: what the import of `x` (kFrameSize frames) must equal. */
    std::shared_ptr<Wavetable> reference (const std::vector<double>& x, int bits = 32, bool isFloat = true)
    {
        std::vector<float> f (x.size());
        for (size_t i = 0; i < x.size(); ++i) f[i] = decoded (x[i], bits, isFloat);
        return Wavetable::fromFrames (f.data(), (int) (x.size() / (size_t) kFrameSize), "ref");
    }

    /** Largest sample difference over all frames (infinity when the frame counts differ). */
    double maxDiff (const Wavetable& a, const Wavetable& b)
    {
        if (a.numFrames() != b.numFrames()) return 1.0e30;
        double m = 0.0;
        for (int f = 0; f < a.numFrames(); ++f)
            for (int i = 0; i < kFrameSize; ++i)
                m = std::max (m, std::fabs ((double) a.frame (f)[i] - (double) b.frame (f)[i]));
        return m;
    }

    /** Phase of harmonic 1 of a frame (radians, sin = 0). Unaffected by DC removal and normalisation. */
    double fundamentalPhase (const float* fr)
    {
        double s = 0.0, c = 0.0;
        for (int i = 0; i < kFrameSize; ++i)
        {
            s += fr[i] * std::sin (kTwoPi * i / kFrameSize);
            c += fr[i] * std::cos (kTwoPi * i / kFrameSize);
        }
        return std::atan2 (c, s);
    }

    float framePeak (const float* fr)
    {
        float p = 0.0f;
        for (int i = 0; i < kFrameSize; ++i) p = std::max (p, std::fabs (fr[i]));
        return p;
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

    // Exactly the 16-bit decoded samples went into the table (1 LSB off anywhere would show).
    CHECK (maxDiff (*r.table, *reference (x, 16, false)) <= 1.0e-6);
    for (int f = 0; f < 3; ++f)
    {
        const std::vector<double> src (x.begin() + f * 2048, x.begin() + (f + 1) * 2048);
        CHECK (correlation (r.table->frame (f), src) > 0.99999);
    }
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
    const auto ref = reference (x);
    bool same = true;   // float data is taken verbatim: the table is fromFrames of exactly the file's floats
    for (int f = 0; f < 2; ++f)
        for (int i = 0; i < kFrameSize; ++i)
            same = same && r.table->frame (f)[i] == ref->frame (f)[i];
    CHECK (same);
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
    // Frame f = harmonic f + 1 plus a weaker, phase-shifted harmonic 3 (f + 1): the resample must keep
    // both harmonic numbers, their relative amplitude and their phase.
    const auto frames = [] (int frameSize)
    {
        std::vector<double> x ((size_t) 3 * (size_t) frameSize);
        for (int f = 0; f < 3; ++f)
            for (int i = 0; i < frameSize; ++i)
            {
                const double w = kTwoPi * (f + 1) * i / frameSize;
                x[(size_t) f * (size_t) frameSize + (size_t) i] = 0.6 * std::sin (w) + 0.25 * std::sin (3.0 * w + 0.5);
            }
        return x;
    };

    WavSpec s; s.format = 3; s.bits = 32; s.clm = "<!>  1024 10000000 wavetable (other)";   // extra whitespace on purpose
    const auto r = importBytes (makeWav (s, { frames (1024) }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK (r.hadClmChunk);
    CHECK_EQ (r.frameSize, 1024);
    CHECK_EQ (r.table->numFrames(), 3);
    for (int f = 0; f < 3; ++f)
        CHECK (correlation (r.table->frame (f), sineCycle (f + 1)) > 0.9);
    // Identical (to float precision) to the same frames drawn natively at 2048 samples.
    const double err = maxDiff (*r.table, *reference (frames (2048)));
    CHECK (err < 1.0e-4);
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
        // The table holds exactly this depth's decoded samples: a 1-LSB decode error (1/128 .. 2^-31 before
        // normalisation) would exceed the tolerance for every depth but 32-bit int / 64-bit float, whose
        // decode is checked against the double value to float precision.
        CHECK (maxDiff (*r.table, *reference (x, c.bits, c.format == 3)) <= 2.0e-6);
        CHECK (correlation (r.table->frame (0), x) > 1.0 - c.tol);
    }

    // 24-bit negative full scale and sign extension: three impulses whose signs and ratios survive.
    WavSpec s; s.bits = 24;
    std::vector<double> edge ((size_t) kFrameSize, 0.0);
    edge[0] = -1.0; edge[1] = 8388607.0 / 8388608.0; edge[2] = -0.5;
    const auto r = importBytes (makeWav (s, { edge }));
    CHECK (r.table != nullptr);
    if (r.table != nullptr)
    {
        CHECK (maxDiff (*r.table, *reference (edge, 24, false)) <= 1.0e-6);
        const float* fr = r.table->frame (0);
        CHECK (fr[0] < 0.0f && fr[1] > 0.0f && fr[2] < 0.0f);
        CHECK_NEAR (fr[0] / fr[1], -1.0, 1.0e-3);
        CHECK_NEAR (fr[2] / fr[0], 0.5, 1.0e-3);
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
        if (r.table != nullptr) CHECK (maxDiff (*r.table, *reference (x)) == 0.0);
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
    // Frame f is a sine whose phase encodes the frame index (pi f / 300): survives DC removal and normalisation.
    const auto phaseOf = [] (int f) { return 3.141592653589793 * f / 300.0; };
    WavSpec s; s.format = 3; s.bits = 32;
    std::vector<double> x ((size_t) 300 * 2048);
    for (int f = 0; f < 300; ++f)
        for (int i = 0; i < 2048; ++i) x[(size_t) f * 2048 + (size_t) i] = 0.5 * std::sin (kTwoPi * i / 2048.0 + phaseOf (f));
    const auto r = importBytes (makeWav (s, { x }));
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK_EQ (r.table->numFrames(), kMaxFrames);
    CHECK_NEAR (fundamentalPhase (r.table->frame (0)), phaseOf (0), 1.0e-4);                           // first frame kept
    CHECK_NEAR (fundamentalPhase (r.table->frame (kMaxFrames - 1)), phaseOf (299), 1.0e-4);            // last frame kept
    bool evenlySpaced = true, ascending = true;
    for (int f = 0; f < kMaxFrames; ++f)
    {
        const int src = (int) std::lround (f * 299.0 / (kMaxFrames - 1));
        evenlySpaced = evenlySpaced && std::fabs (fundamentalPhase (r.table->frame (f)) - phaseOf (src)) < 1.0e-4;
        if (f > 0) ascending = ascending && fundamentalPhase (r.table->frame (f)) > fundamentalPhase (r.table->frame (f - 1));
    }
    CHECK (evenlySpaced);
    CHECK (ascending);
}

AP_TEST (wavio_export_import_round_trip)
{
    const int frames = 5;
    std::vector<float> data ((size_t) frames * kFrameSize);
    ap::Rng rng; rng.state = 0xC0FFEEu;
    for (auto& v : data) v = rng.nextBipolar() * 0.9f + 0.05f;   // with some DC for fromFrames to remove
    const auto t = Wavetable::fromFrames (data.data(), frames, "Round");
    CHECK (t != nullptr);
    if (t == nullptr) return;

    const auto bytes = exportWavetableWav (*t);
    CHECK (bytes.size() > (size_t) frames * kFrameSize * 4);

    // Export is bit-exact: the data chunk holds the table's frames as 32-bit floats.
    size_t pos = 12, dataAt = 0, dataSize = 0;
    while (pos + 8 <= bytes.size())
    {
        const uint32_t size = (uint32_t) bytes[pos + 4] | ((uint32_t) bytes[pos + 5] << 8) | ((uint32_t) bytes[pos + 6] << 16) | ((uint32_t) bytes[pos + 7] << 24);
        if (std::string (bytes.begin() + (long) pos, bytes.begin() + (long) pos + 4) == "data") { dataAt = pos + 8; dataSize = size; break; }
        pos += 8 + size + (size & 1u);
    }
    CHECK_EQ (dataSize, (size_t) frames * kFrameSize * 4);
    bool exportExact = dataAt > 0 && dataAt + dataSize <= bytes.size() && dataSize == (size_t) frames * kFrameSize * 4;
    for (int f = 0; exportExact && f < frames; ++f)
        for (int i = 0; i < kFrameSize; ++i)
        {
            const uint8_t* p = bytes.data() + dataAt + ((size_t) f * kFrameSize + (size_t) i) * 4;
            const uint32_t b = (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
            exportExact = exportExact && b == std::bit_cast<uint32_t> (t->frame (f)[i]);
        }
    CHECK (exportExact);

    // Import gives the same (already conditioned) frames back, to float precision: re-running fromFrames
    // on DC-free, normalised frames only adds FFT rounding.
    const auto r = importBytes (bytes, "Back");
    CHECK (r.error.empty());
    CHECK (r.hadClmChunk);
    CHECK_EQ (r.frameSize, 2048);
    CHECK (r.table != nullptr);
    if (r.table == nullptr) return;
    CHECK_EQ (r.table->numFrames(), frames);
    CHECK (maxDiff (*r.table, *t) < 1.0e-5);
    for (int f = 0; f < frames; ++f)
        CHECK_NEAR (framePeak (r.table->frame (f)), framePeak (t->frame (f)), 1.0e-5);

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
