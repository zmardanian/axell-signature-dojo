#pragma once
// Axell Signature Dojo - Streaming Preview: what the big streaming services do to a master's level (docs/DESIGN.md,
// D17). Framework independent.
//
// A master is measured once - integrated loudness (ITU-R BS.1770-4 / EBU R128, axell::LoudnessMeter), true peak (8x
// interpolation, axell::TruePeakMeter), loudness range, the loudest 3 s - and each service's published normalisation
// is applied: a loud master is turned down to the service's reference; a quiet one is turned up only by services that
// do so, and only as far as its true peak allows (Spotify's normal mode: no further than -1 dBTP). The result is the
// gain each service applies, so the master can be heard as listeners there will hear it.
#include <axell/dsp/Loudness.h>
#include <axell/dsp/TruePeak.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace dojo::streaming
{
struct Service
{
    const char* name;
    float referenceLufs;
    bool turnsUp;            // raises quiet masters (limited by the true peak)
    float peakCeilingDbtp;   // how far up a quiet master may go
    const char* note;
};

// the services' published playback normalisation (default settings)
inline const std::vector<Service>& services()
{
    static const std::vector<Service> list {
        { "Spotify", -14.0f, true, -1.0f, "Normal mode: louder masters are turned down; quieter ones up while the true peak stays under -1 dBTP." },
        { "Apple Music", -16.0f, true, -1.0f, "Sound Check: masters are moved to -16 LUFS, quieter ones only as far as their peaks allow." },
        { "YouTube", -14.0f, false, 0.0f, "Louder videos are turned down to -14 LUFS; quieter ones are left as they are." },
        { "Tidal", -14.0f, false, 0.0f, "Louder masters are turned down to -14 LUFS; quieter ones are left as they are." },
        { "Amazon Music", -14.0f, false, 0.0f, "Louder masters are turned down to about -14 LUFS; quieter ones are left as they are." },
    };
    return list;
}

struct Measurement
{
    float integratedLufs = -70.0f, truePeakDbtp = -144.0f, rangeLu = 0.0f, maxShortTermLufs = -70.0f;
    double seconds = 0.0;
};

struct Playback
{
    const Service* service = nullptr;
    float gainDb = 0.0f;            // what the service applies
    float playedLufs = -70.0f;      // how loud listeners hear it
    float headroomLostDb = 0.0f;    // loudness given away to normalisation (the master was louder than needed)
};

inline Measurement measure (const float* const* x, int numChannels, size_t n, double fs)
{
    Measurement m;
    m.seconds = (double) n / fs;
    const int ch = std::clamp (numChannels, 1, 2);
    axell::LoudnessMeter lm;
    lm.prepare (fs, ch);
    axell::TruePeakMeter tp;
    tp.prepare();
    for (size_t i = 0; i < n; i += 4096)
    {
        const int k = (int) std::min ((size_t) 4096, n - i);
        const float* p[2] = { x[0] + i, x[std::min (1, ch - 1)] + i };
        lm.process (p, ch, k);
        tp.process (p, ch, k);
    }
    auto finite = [] (float v, float fallback) { return std::isfinite (v) ? v : fallback; };
    m.integratedLufs = finite (lm.integratedLufs(), -70.0f);
    m.rangeLu = finite (lm.loudnessRangeLu(), 0.0f);
    m.maxShortTermLufs = finite (lm.maxShortTermLufs(), -70.0f);
    m.truePeakDbtp = (float) (20.0 * std::log10 (std::max (1.0e-9f, tp.maxTruePeak())));
    return m;
}

inline std::vector<Playback> playback (const Measurement& m)
{
    std::vector<Playback> out;
    for (const auto& s : services())
    {
        Playback p;
        p.service = &s;
        float g = s.referenceLufs - m.integratedLufs;
        if (g > 0.0f)
            g = s.turnsUp ? std::max (0.0f, std::min (g, s.peakCeilingDbtp - m.truePeakDbtp)) : 0.0f;
        p.gainDb = g;
        p.playedLufs = m.integratedLufs + g;
        p.headroomLostDb = std::max (0.0f, -g);
        out.push_back (p);
    }
    return out;
}
} // namespace dojo::streaming
