#include "WavetableIO.h"

#include "FFT.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>

namespace verdant
{

namespace
{
    constexpr uint16_t kFormatPcm        = 1;
    constexpr uint16_t kFormatFloat      = 3;
    constexpr uint16_t kFormatExtensible = 0xFFFE;

    constexpr int kMinClmFrameSize = 2;       // anything smaller cannot hold a cycle
    constexpr int kMaxClmFrameSize = 65536;   // the FFT's largest size; bigger values are treated as unparsable
    constexpr int kMaxHarmonic     = kFrameSize / 2 - 1;   // 1023: the highest harmonic a 2048 frame keeps (Nyquist bin dropped)

    uint16_t readU16 (const uint8_t* p) noexcept { return (uint16_t) (p[0] | (p[1] << 8)); }
    uint32_t readU32 (const uint8_t* p) noexcept
    {
        return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
    }

    bool idIs (const uint8_t* p, const char* id) noexcept { return std::memcmp (p, id, 4) == 0; }

    void writeU16 (std::vector<uint8_t>& v, uint16_t x) { v.push_back ((uint8_t) (x & 0xFF)); v.push_back ((uint8_t) (x >> 8)); }
    void writeU32 (std::vector<uint8_t>& v, uint32_t x)
    {
        for (int b = 0; b < 4; ++b) v.push_back ((uint8_t) ((x >> (8 * b)) & 0xFF));
    }
    void writeId (std::vector<uint8_t>& v, const char* id) { v.insert (v.end(), id, id + 4); }

    /** The integer right after "<!>" in a clm chunk, or 0 if there is none. Tolerates whitespace and trailing text. */
    int parseClmFrameSize (const uint8_t* p, size_t n)
    {
        const std::string text (reinterpret_cast<const char*> (p), n);
        const auto marker = text.find ("<!>");
        if (marker == std::string::npos)
            return 0;

        size_t i = marker + 3;
        while (i < text.size() && std::isspace ((unsigned char) text[i]))
            ++i;

        long value = 0;
        bool any = false;
        while (i < text.size() && std::isdigit ((unsigned char) text[i]) && value <= kMaxClmFrameSize)
        {
            value = value * 10 + (text[i] - '0');
            any = true;
            ++i;
        }
        if (! any || value < kMinClmFrameSize || value > kMaxClmFrameSize)
            return 0;
        return (int) value;
    }

    /** One sample of the first channel as float (-1..1 for PCM). */
    float decodeSample (const uint8_t* p, uint16_t format, int bytesPerSample) noexcept
    {
        if (format == kFormatFloat)
        {
            if (bytesPerSample == 4)
                return std::bit_cast<float> (readU32 (p));

            uint64_t bits = 0;
            for (int b = 0; b < 8; ++b) bits |= (uint64_t) p[b] << (8 * b);
            return (float) std::bit_cast<double> (bits);
        }

        switch (bytesPerSample)
        {
            case 1:  return ((float) p[0] - 128.0f) / 128.0f;   // 8-bit WAV is unsigned
            case 2:  return (float) (int16_t) readU16 (p) / 32768.0f;
            case 3:
            {
                // Sign-extend the 24-bit little-endian value through the top of an int32.
                const int32_t v = (int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8;
                return (float) v / 8388608.0f;
            }
            default: return (float) ((double) (int32_t) readU32 (p) / 2147483648.0);
        }
    }

    /** Resamples one single-cycle frame of `n` samples to kFrameSize samples spectrally, so harmonic k stays
        harmonic k. Harmonics above 1023 and the source Nyquist bin (its phase is ambiguous) are dropped; DC is kept. */
    void resampleFrame (const float* src, int n, float* dst)
    {
        if (n == kFrameSize)
        {
            std::copy (src, src + kFrameSize, dst);
            return;
        }

        const int maxK = std::min ((n % 2 == 0) ? n / 2 - 1 : (n - 1) / 2, kMaxHarmonic);
        std::vector<float> re ((size_t) kFrameSize / 2 + 1, 0.0f), im ((size_t) kFrameSize / 2 + 1, 0.0f);
        const double scale = (double) kFrameSize / (double) n;   // keeps amplitudes: bins scale with the transform size

        if (n >= 2 && (n & (n - 1)) == 0)
        {
            const FFT fft (n);
            std::vector<float> fr ((size_t) n / 2 + 1), fi ((size_t) n / 2 + 1);
            fft.realForward (src, fr.data(), fi.data());
            for (int k = 0; k <= maxK; ++k)
            {
                re[(size_t) k] = (float) (fr[(size_t) k] * scale);
                im[(size_t) k] = (float) (fi[(size_t) k] * scale);
            }
        }
        else
        {
            // Direct DFT for sizes the radix-2 FFT cannot take (odd clm sizes, a short whole-file cycle).
            std::vector<double> cosT ((size_t) n), sinT ((size_t) n);
            for (int j = 0; j < n; ++j)
            {
                const double w = 2.0 * 3.14159265358979323846 * (double) j / (double) n;
                cosT[(size_t) j] = std::cos (w);
                sinT[(size_t) j] = std::sin (w);
            }
            for (int k = 0; k <= maxK; ++k)
            {
                double accRe = 0.0, accIm = 0.0;
                int idx = 0;
                for (int j = 0; j < n; ++j)
                {
                    accRe += (double) src[j] * cosT[(size_t) idx];
                    accIm -= (double) src[j] * sinT[(size_t) idx];
                    idx += k;
                    if (idx >= n) idx -= n;
                }
                re[(size_t) k] = (float) (accRe * scale);
                im[(size_t) k] = (float) (accIm * scale);
            }
        }

        im[0] = 0.0f;
        const FFT out (kFrameSize);
        out.realInverse (re.data(), im.data(), dst);
    }

    WavImportResult failWith (std::string message)
    {
        WavImportResult r;
        r.error = std::move (message);
        return r;
    }
}

WavImportResult importWavetableWav (const uint8_t* bytes, size_t numBytes, const std::string& name)
{
    if (bytes == nullptr || numBytes < 12)
        return failWith ("Not a WAV file: too short");
    if (! idIs (bytes, "RIFF") || ! idIs (bytes + 8, "WAVE"))
        return failWith ("Not a WAV file: missing RIFF/WAVE header");

    // The RIFF size field is often wrong in the wild (streamed writers); use it only when it is plausible.
    const size_t riffField = (size_t) readU32 (bytes + 4) + 8;
    const size_t riffEnd = riffField >= 12 ? std::min (numBytes, riffField) : numBytes;

    bool haveFmt = false;
    uint16_t format = 0, channels = 0, bitsPerSample = 0, blockAlign = 0;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;
    int clmFrameSize = 0;

    size_t pos = 12;
    while (pos + 8 <= riffEnd)
    {
        const uint8_t* chunk = bytes + pos;
        const size_t size = readU32 (chunk + 4);
        const size_t bodyStart = pos + 8;
        const size_t available = riffEnd - bodyStart;

        if (idIs (chunk, "fmt "))
        {
            if (size < 16 || size > available)
                return failWith ("Malformed WAV: truncated 'fmt ' chunk");
            format        = readU16 (chunk + 8);
            channels      = readU16 (chunk + 10);
            blockAlign    = readU16 (chunk + 20);
            bitsPerSample = readU16 (chunk + 22);
            if (format == kFormatExtensible)
            {
                if (size < 40)
                    return failWith ("Malformed WAV: WAVE_FORMAT_EXTENSIBLE without a sub-format");
                format = readU16 (chunk + 8 + 24);   // first two bytes of the sub-format GUID
            }
            haveFmt = true;
        }
        else if (idIs (chunk, "data"))
        {
            // A data chunk that claims more than the file holds (unfinished recording): use what is there.
            data = bytes + bodyStart;
            dataBytes = std::min (size, available);
        }
        else if (idIs (chunk, "clm "))
        {
            if (const int fs = parseClmFrameSize (chunk + 8, std::min (size, available)); fs > 0)
                clmFrameSize = fs;
        }

        if (size > available)
            break;
        pos = bodyStart + size + (size & 1);   // chunks are word aligned: odd sizes carry a pad byte
    }

    if (! haveFmt)
        return failWith ("Malformed WAV: no 'fmt ' chunk");
    if (data == nullptr)
        return failWith ("Malformed WAV: no 'data' chunk");
    if (channels < 1)
        return failWith ("Malformed WAV: zero channels");

    const bool pcmOk   = format == kFormatPcm && (bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32);
    const bool floatOk = format == kFormatFloat && (bitsPerSample == 32 || bitsPerSample == 64);
    if (! pcmOk && ! floatOk)
        return failWith ("Unsupported WAV format (format " + std::to_string (format) + ", " + std::to_string (bitsPerSample)
                         + " bit); use PCM 8/16/24/32-bit or float 32/64-bit");

    const int bytesPerSample = bitsPerSample / 8;
    const size_t frameBytes = std::max ((size_t) blockAlign, (size_t) channels * (size_t) bytesPerSample);
    const size_t numSamples = dataBytes / frameBytes;
    if (numSamples == 0)
        return failWith ("WAV file contains no audio");

    std::vector<float> mono (numSamples);
    for (size_t i = 0; i < numSamples; ++i)
    {
        const float s = decodeSample (data + i * frameBytes, format, bytesPerSample);
        mono[i] = std::isfinite (s) ? s : 0.0f;
    }

    WavImportResult r;
    r.hadClmChunk = clmFrameSize > 0;
    r.frameSize = r.hadClmChunk ? clmFrameSize : kFrameSize;

    // A file shorter than one frame is taken as one cycle of its own length.
    int frameSize = r.frameSize;
    size_t fileFrames = numSamples / (size_t) frameSize;
    if (fileFrames == 0)
    {
        frameSize = (int) numSamples;
        fileFrames = 1;
    }

    const int outFrames = (int) std::min (fileFrames, (size_t) kMaxFrames);
    std::vector<float> frames ((size_t) outFrames * (size_t) kFrameSize);
    for (int f = 0; f < outFrames; ++f)
    {
        // Thinning picks evenly spaced frames, first and last included.
        const size_t src = (size_t) outFrames == fileFrames || outFrames < 2
                         ? (size_t) f
                         : (size_t) std::llround ((double) f * (double) (fileFrames - 1) / (double) (outFrames - 1));
        resampleFrame (mono.data() + src * (size_t) frameSize, frameSize, frames.data() + (size_t) f * (size_t) kFrameSize);
    }

    r.table = Wavetable::fromFrames (frames.data(), outFrames, name);
    if (r.table == nullptr)
        return failWith ("Could not build a wavetable from the WAV data");
    return r;
}

std::vector<uint8_t> exportWavetableWav (const Wavetable& table)
{
    static constexpr char kClm[] = "<!>2048 01000000 wavetable (verdant)";
    const uint32_t clmSize = (uint32_t) (sizeof (kClm) - 1);
    const int numFrames = std::max (0, table.numFrames());
    const uint32_t dataSize = (uint32_t) numFrames * (uint32_t) kFrameSize * 4u;

    std::vector<uint8_t> v;
    v.reserve (12 + 24 + 8 + clmSize + 1 + 8 + dataSize);

    writeId (v, "RIFF");
    writeU32 (v, 0);   // patched below
    writeId (v, "WAVE");

    writeId (v, "fmt ");
    writeU32 (v, 16);
    writeU16 (v, kFormatFloat);
    writeU16 (v, 1);              // mono
    writeU32 (v, 44100);
    writeU32 (v, 44100u * 4u);    // byte rate
    writeU16 (v, 4);              // block align
    writeU16 (v, 32);

    writeId (v, "clm ");
    writeU32 (v, clmSize);
    v.insert (v.end(), kClm, kClm + clmSize);
    if ((clmSize & 1u) != 0) v.push_back (0);

    writeId (v, "data");
    writeU32 (v, dataSize);
    for (int f = 0; f < numFrames; ++f)
    {
        const float* fr = table.frame (f);
        for (int i = 0; i < kFrameSize; ++i)
            writeU32 (v, std::bit_cast<uint32_t> (fr[i]));
    }

    const uint32_t riffSize = (uint32_t) v.size() - 8u;
    for (int b = 0; b < 4; ++b)
        v[(size_t) (4 + b)] = (uint8_t) ((riffSize >> (8 * b)) & 0xFF);
    return v;
}

} // namespace verdant
