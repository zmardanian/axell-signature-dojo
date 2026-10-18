#pragma once
// Minimal WAV reader/writer for the Dojo's JUCE-free tests and calibration tools: PCM 16/24/32-bit and float 32,
// WAVE_FORMAT_EXTENSIBLE included. Not used by the app (the app reads audio through JUCE).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wavio
{
struct Audio
{
    double rate = 0.0;
    std::vector<std::vector<float>> ch;   // [channel][sample]
    size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

inline bool read (const std::string& path, Audio& a)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    std::vector<uint8_t> d;
    uint8_t buf[1 << 16];
    size_t got;
    while ((got = std::fread (buf, 1, sizeof (buf), f)) > 0)
        d.insert (d.end(), buf, buf + got);
    std::fclose (f);
    auto u16 = [&] (size_t p) { return (uint32_t) d[p] | ((uint32_t) d[p + 1] << 8); };
    auto u32 = [&] (size_t p) { return u16 (p) | (u16 (p + 2) << 16); };
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0)
        return false;
    int fmt = 0, nch = 0, bits = 0;
    size_t pos = 12, dataPos = 0, dataLen = 0;
    while (pos + 8 <= d.size())
    {
        const uint32_t len = u32 (pos + 4);
        if (std::memcmp (d.data() + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= d.size())
        {
            fmt = (int) u16 (pos + 8);
            nch = (int) u16 (pos + 10);
            a.rate = u32 (pos + 12);
            bits = (int) u16 (pos + 22);
            if (fmt == 0xFFFE && len >= 40)
                fmt = (int) u16 (pos + 8 + 24);   // sub-format GUID's first two bytes
        }
        else if (std::memcmp (d.data() + pos, "data", 4) == 0)
        {
            dataPos = pos + 8;
            dataLen = std::min<size_t> (len, d.size() - dataPos);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (dataPos == 0 || nch < 1 || (fmt != 1 && fmt != 3) || a.rate <= 0)
        return false;
    const int bps = bits / 8;
    const size_t n = dataLen / (size_t) (bps * nch);
    a.ch.assign ((size_t) nch, std::vector<float> (n));
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < nch; ++c)
        {
            const uint8_t* p = d.data() + dataPos + (i * (size_t) nch + (size_t) c) * (size_t) bps;
            float v = 0.0f;
            if (fmt == 3 && bits == 32)
                std::memcpy (&v, p, 4);
            else if (bits == 16)
                v = (float) (int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
            else if (bits == 24)
            {
                int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                if (s & 0x800000)
                    s -= 0x1000000;
                v = (float) s / 8388608.0f;
            }
            else if (bits == 32)
            {
                int32_t s;
                std::memcpy (&s, p, 4);
                v = (float) ((double) s / 2147483648.0);
            }
            else
                return false;
            a.ch[(size_t) c][i] = v;
        }
    return true;
}

// float 32 WAV
inline bool write (const std::string& path, const Audio& a)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const uint32_t nch = (uint32_t) a.ch.size(), n = (uint32_t) a.frames(), rate = (uint32_t) a.rate;
    const uint32_t dataLen = n * nch * 4;
    auto w32 = [f] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [f] (uint32_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataLen); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (3); w16 (nch); w32 (rate); w32 (rate * nch * 4); w16 (nch * 4); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (dataLen);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t c = 0; c < nch; ++c)
            std::fwrite (&a.ch[c][i], 4, 1, f);
    return std::fclose (f) == 0;
}
} // namespace wavio
