#pragma once
// Axell Signature Dojo - Voice Profile analysis: what a take says about the voice, the microphone chain and the room.
// Framework independent (axell-core DSP only), deterministic, offline.
//
// Every measure is chosen so that it can be checked against something independent (docs/DESIGN.md, D7-D12):
//  - loudness:   ITU-R BS.1770-4 integrated loudness (axell::LoudnessMeter, EBU Tech 3341 conformant), on the
//                channels as they reach a plugin on the track (a stereo track sums both channels)
//  - noise:      the quiet frames (10th percentile of 20 ms levels, digital zeros excluded) against the voice's loud
//                frames (95th percentile)
//  - pitch:      YIN f0 (de Cheveigne & Kawahara 2002) from the shipping sibilance detector of Axell Velvet / core
//  - sibilance:  the same detector's network decides which 2 ms hops are esses; their averaged spectrum gives the
//                "sh" peak (2-5 kHz) and the "s" peak (5-12 kHz)
//  - room:       blind RT60 from free decays (Axell Gate 2.0's room model)
//  - low mids:   the voiced long-term spectrum's low-mid balance against 27 dry studio voices (VoiceReference.h);
//                shown, never corrected automatically (D9)
//  - low end:    rumble (20-60 Hz against the 150 Hz-1 kHz body, all non-silent audio) and low-frequency bursts
//                (pops, handling: the 99.5th percentile of the 20-100 Hz envelope against the voice)
//  - recording:  sample peak, clipped runs, DC offset
#include "RoomModel.h"
#include "VoiceReference.h"

#include <axell/dsp/Fft.h>
#include <axell/dsp/Filters.h>
#include <axell/dsp/Loudness.h>
#include <axell/dsp/SibilanceDetector.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <vector>

namespace dojo::voice
{
using RoomModel = axg::RoomModel;

// a curve on a log-frequency grid
struct Curve
{
    std::vector<float> hz, db;
};

struct Profile
{
    // the take
    double sampleRate = 48000.0, seconds = 0.0, activeSeconds = 0.0;
    int channels = 1;
    // level and recording quality
    float integratedLufs = -70.0f, peakDbfs = -144.0f, dcOffsetDb = -144.0f;
    int clippedRuns = 0;                  // runs of >= 3 samples at or above -0.1 dBFS
    // a recorded microphone take is mono: stereo content in the pauses means reverb or other effects are printed in
    // (the room then cannot be measured, D11)
    float sideLoudDb = -144.0f, sidePauseDb = -144.0f;   // side re mid, median over loud frames / over pauses
    bool processedTake = false;
    float noiseFloorDb = -144.0f, voiceLevelDb = -144.0f, snrDb = 0.0f;
    // pitch
    float f0Median = 0.0f, f0Low = 0.0f, f0High = 0.0f;   // Hz: median, 10th and 90th percentile of voiced hops
    int voicedHops = 0;
    // sibilance
    float sHz = 0.0f, shHz = 0.0f;        // centres of the ess clusters ("s" 4.5-12 kHz, "sh" 2-5.5 kHz); 0 = too few
    float sShare = 0.5f;                  // share of the sibilant energy whose centroid is above 5 kHz
    float essLevelDb = -60.0f;            // median sibilant hop level against the voice body (detector's own body)
    int sibilantHops = 0;
    // room
    float rt60 = 0.0f;                    // s, blind (room model)
    float rt60Bands[RoomModel::kBands] {};
    // low-mid balance against 27 dry studio voices (informational, D9)
    float lowMidHz = 0.0f, lowMidExcessDb = 0.0f, lowMidZ = 0.0f;   // the most unusual point, 250-900 Hz
    bool lowMidOutside = false;                                        // z >= 2.5: outside what dry voices show
    // low end
    float rumbleDb = -60.0f, lfBurstDb = -60.0f;
    // curves for display
    Curve ltas;                           // long-term spectrum of the voiced audio, 1/3 octave, normalised to 0 dB peak
    Curve essSpectrum;                    // average sibilant spectrum, 1/6 octave, normalised
    Curve lowMid;                         // low-mid balance minus the dry-voice mean (dB), 150 Hz-1.4 kHz
    std::vector<float> lowMidSpread;      // the dry voices' spread (1 standard deviation, dB) at the same points
    std::vector<float> pitchHist;         // voiced hops per semitone, 36 (C2 = 65.4 Hz) .. 84 (C6)
    std::vector<float> envelope;          // 20 ms levels (dBFS) for the waveform overview
};

using Progress = std::function<bool (float)>;   // 0..1; return false to cancel

namespace detail
{
inline float pct (std::vector<float> v, double p)
{
    if (v.empty())
        return 0.0f;
    const auto k = (size_t) std::clamp (p * (double) (v.size() - 1) + 0.5, 0.0, (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

inline float db (double power) { return (float) (10.0 * std::log10 (power + 1.0e-30)); }

// log grid from lo to hi with `perOct` points per octave
inline std::vector<float> logGrid (double lo, double hi, int perOct)
{
    std::vector<float> g;
    for (double f = lo; f <= hi * 1.0001; f *= std::pow (2.0, 1.0 / perOct))
        g.push_back ((float) f);
    return g;
}

// mean power of linear-frequency bins (bin k at k * df) over [f 2^-w/2, f 2^+w/2] for every grid point
inline std::vector<double> smoothOct (const std::vector<double>& power, double df, const std::vector<float>& grid, double w)
{
    std::vector<double> out (grid.size(), 0.0);
    // prefix sums for O(1) band means
    std::vector<double> pre (power.size() + 1, 0.0);
    for (size_t k = 0; k < power.size(); ++k)
        pre[k + 1] = pre[k] + power[k];
    for (size_t i = 0; i < grid.size(); ++i)
    {
        const double lo = grid[i] * std::pow (2.0, -w / 2), hi = grid[i] * std::pow (2.0, w / 2);
        auto a = (size_t) std::clamp ((long) std::ceil (lo / df), 1L, (long) power.size() - 1);
        auto b = (size_t) std::clamp ((long) std::floor (hi / df), (long) a, (long) power.size() - 1);
        out[i] = (pre[b + 1] - pre[a]) / (double) (b - a + 1);
    }
    return out;
}

// Welch accumulator over chosen frames (Hann, power spectrum)
struct Welch
{
    explicit Welch (int n) : size (n), win ((size_t) n), buf ((size_t) n), spec ((size_t) n + 2), acc ((size_t) n / 2 + 1, 0.0)
    {
        fft.prepare (n);
        for (int i = 0; i < n; ++i)
            win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (i + 0.5) / n));
    }
    void add (const float* x, double weight = 1.0)
    {
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = x[i] * win[(size_t) i];
        fft.forward (buf.data(), spec.data());
        for (int k = 0; k <= size / 2; ++k)
            acc[(size_t) k] += weight * ((double) spec[(size_t) (2 * k)] * spec[(size_t) (2 * k)]
                                         + (double) spec[(size_t) (2 * k + 1)] * spec[(size_t) (2 * k + 1)]);
        frames += weight;
    }
    int size;
    std::vector<float> win, buf, spec;
    std::vector<double> acc;
    double frames = 0.0;
    axell::RealFft fft;
};
} // namespace detail

// x: one pointer per channel, n samples each. Analyses at most the first `maxSeconds`. `ref` is the dry-voice
// reference the low-mid balance is measured against (the embedded one; calibration passes a neutral one).
inline Profile analyse (const float* const* x, int numChannels, size_t n, double fs, const Progress& progress = {},
                        double maxSeconds = 600.0, const reference::Box& ref = reference::kBox)
{
    using namespace detail;
    Profile p;
    p.sampleRate = fs;
    p.channels = std::clamp (numChannels, 1, 2);
    n = std::min (n, (size_t) (maxSeconds * fs));
    p.seconds = (double) n / fs;
    if (n < (size_t) (fs * 2.0) || numChannels < 1)
        return p;   // too short to say anything

    auto report = [&] (float f) { return ! progress || progress (f); };

    // the mid signal every voice measure works on
    std::vector<float> mono (n);
    double dc = 0.0, peak = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (int c = 0; c < p.channels; ++c)
        {
            const double v = std::isfinite (x[c][i]) ? x[c][i] : 0.0;
            s += v;
            peak = std::max (peak, std::abs (v));
        }
        mono[i] = (float) (s / p.channels);
        dc += mono[i];
    }
    p.peakDbfs = (float) (20.0 * std::log10 (peak + 1.0e-12));
    p.dcOffsetDb = (float) (20.0 * std::log10 (std::abs (dc / (double) n) + 1.0e-12));

    // clipped runs: >= 3 consecutive samples at or above -0.1 dBFS on any channel
    {
        const float t = 0.98855f;
        for (int c = 0; c < p.channels; ++c)
        {
            int run = 0;
            for (size_t i = 0; i < n; ++i)
            {
                run = std::abs (x[c][i]) >= t ? run + 1 : 0;
                if (run == 3)
                    ++p.clippedRuns;
            }
        }
    }

    // integrated loudness, as the plugins on the track measure it
    {
        axell::LoudnessMeter lm;
        lm.prepare (fs, p.channels);
        for (size_t i = 0; i < n; i += 4096)
        {
            const int m = (int) std::min ((size_t) 4096, n - i);
            const float* ptr[2] = { x[0] + i, x[std::min (1, p.channels - 1)] + i };
            lm.process (ptr, p.channels, m);
        }
        const float l = lm.integratedLufs();
        p.integratedLufs = std::isfinite (l) ? l : -70.0f;
    }
    if (! report (0.05f))
        return p;

    // 20 ms frame levels: noise floor, voice level, activity
    const int fr = (int) std::lround (0.02 * fs);
    const size_t nFrames = n / (size_t) fr;
    std::vector<float> frameDb (nFrames), nonZero;
    for (size_t f = 0; f < nFrames; ++f)
    {
        double e = 0.0;
        bool zero = true;
        for (int i = 0; i < fr; ++i)
        {
            const double v = mono[f * (size_t) fr + (size_t) i];
            e += v * v;
            zero = zero && ! (std::abs (v) > 0.0);   // exact digital silence
        }
        frameDb[f] = db (e / fr);
        if (! zero)
            nonZero.push_back (frameDb[f]);
    }
    p.envelope = frameDb;
    p.noiseFloorDb = pct (nonZero, 0.10);
    p.voiceLevelDb = pct (nonZero, 0.95);
    p.snrDb = p.voiceLevelDb - p.noiseFloorDb;
    if (p.channels == 2)
    {
        std::vector<float> loudSide, pauseSide;
        for (size_t f = 0; f < nFrames; ++f)
        {
            double e = 0.0;
            for (int i = 0; i < fr; ++i)
            {
                const size_t k = f * (size_t) fr + (size_t) i;
                const double v = 0.5 * ((double) x[0][k] - (double) x[1][k]);
                e += v * v;
            }
            const float sd = db (e / fr) - frameDb[f];
            if (frameDb[f] > p.voiceLevelDb - 12.0f)
                loudSide.push_back (sd);
            else if (frameDb[f] < p.voiceLevelDb - 30.0f && frameDb[f] > p.noiseFloorDb + 6.0f)
                pauseSide.push_back (sd);
        }
        if (! loudSide.empty())
            p.sideLoudDb = std::max (-144.0f, pct (loudSide, 0.5));
        if (pauseSide.size() > 20)
            p.sidePauseDb = std::max (-144.0f, pct (pauseSide, 0.5));
        p.processedTake = p.sidePauseDb > -20.0f;
    }
    const float activeThr = std::max (p.voiceLevelDb - 30.0f, p.noiseFloorDb + 10.0f);
    size_t active = 0;
    for (float v : frameDb)
        active += v > activeThr ? 1u : 0u;
    p.activeSeconds = (double) active * fr / fs;

    // detector pass: pitch, sibilance; room model on the 60 Hz high-passed signal
    axell::voice::SibilanceDetector det;
    det.prepare (fs, axell::voice::DetectorConfig::velvet3());   // Velvet 3.0's network: 111 voices (D22)
    RoomModel room;
    room.prepare (fs, det.hopSize());
    axell::Biquad roomHp;
    roomHp.setHighPass (fs, 60.0, 0.7071);
    std::vector<float> f0s;
    p.pitchHist.assign (49, 0.0f);
    struct Ess { int64_t centre; float hiShare; };
    std::vector<Ess> ess;
    std::vector<float> essLevels;
    const float pitchedThr = p.voiceLevelDb - 25.0f;
    for (size_t i = 0; i < n; ++i)
    {
        room.push ((float) roomHp.process (mono[i], 0));
        if (! det.push (mono[i]))
            continue;
        const auto& o = det.out();
        if (o.p > 0.5f && o.levelDb > activeThr)
        {
            double lo = 0.0, hi = 0.0;
            for (int b = 0; b < axell::voice::kNumBands; ++b)
                (axell::voice::kEdges[b] >= 5000.0 ? hi : lo) += o.E[b];
            ess.push_back ({ o.center, (float) (hi / (hi + lo + 1.0e-30)) });
            essLevels.push_back (o.levelDb - o.bodyDb);
        }
        else if (o.f0 > 60.0f && o.f0 < 520.0f && o.apRaw < 0.2f && o.levelDb > pitchedThr)
        {
            f0s.push_back (o.f0);
            const int semi = (int) std::lround (12.0 * std::log2 (o.f0 / 440.0) + 69.0);
            if (semi >= 36 && semi <= 84)
                p.pitchHist[(size_t) (semi - 36)] += 1.0f;
        }
        if ((i & 0x3fff) == 0 && ! report (0.05f + 0.55f * (float) i / (float) n))
            return p;
    }
    p.voicedHops = (int) f0s.size();
    if (! f0s.empty())
    {
        p.f0Median = pct (f0s, 0.5);
        p.f0Low = pct (f0s, 0.1);
        p.f0High = pct (f0s, 0.9);
    }
    const auto ro = room.at (0);
    p.rt60 = ro.rt60;
    for (int b = 0; b < RoomModel::kBands; ++b)
        p.rt60Bands[b] = room.bandRt60 (b);

    // sibilance spectrum and peaks
    p.sibilantHops = (int) ess.size();
    if (! ess.empty())
    {
        p.essLevelDb = pct (essLevels, 0.5);
        double hiE = 0.0;
        for (const auto& e : ess)
            hiE += e.hiShare;
        p.sShare = (float) (hiE / (double) ess.size());
        const int N = 1 << (int) std::lround (std::log2 (0.016 * fs));   // ~16 ms
        // the two kinds of ess are measured on their own hops: averaged together, the stronger "sh" hides where the
        // "s" sits (measured on the artist's dry take: a 5.6-9 kHz plateau within 1 dB, docs/DESIGN.md D8)
        Welch wAll (N), wS (N), wSh (N);
        for (size_t k = 0; k < ess.size(); k += 2)   // every other hop: 4 ms apart is plenty
        {
            const auto c = ess[k].centre - N / 2;
            if (c < 0 || (size_t) (c + N) > n)
                continue;
            wAll.add (mono.data() + c);
            (ess[k].hiShare >= 0.5f ? wS : wSh).add (mono.data() + c);
        }
        const auto g = logGrid (2000.0, std::min (14000.0, 0.45 * fs), 24);
        // the centre of a cluster: the log-frequency centroid of the band where it is within 6 dB of its maximum
        auto centre = [&] (const Welch& w, double lo, double hi) -> float
        {
            if (w.frames < 8)
                return 0.0f;
            const auto sm = smoothOct (w.acc, fs / N, g, 1.0 / 6.0);
            double top = 0.0;
            for (size_t i = 0; i < g.size(); ++i)
                if (g[i] >= lo && g[i] <= hi)
                    top = std::max (top, sm[i]);
            double sw = 0.0, slog = 0.0;
            for (size_t i = 0; i < g.size(); ++i)
                if (g[i] >= lo && g[i] <= hi && sm[i] >= top * 0.251189)
                {
                    sw += sm[i];
                    slog += sm[i] * std::log (g[i]);
                }
            return sw > 0.0 ? (float) std::exp (slog / sw) : 0.0f;
        };
        p.sHz = centre (wS, 4500.0, 12000.0);
        p.shHz = centre (wSh, 2000.0, 5500.0);
        if (wAll.frames > 0)
        {
            const auto sm = smoothOct (wAll.acc, fs / N, g, 1.0 / 6.0);
            double top = 0.0;
            for (double v : sm)
                top = std::max (top, v);
            p.essSpectrum.hz = g;
            for (double v : sm)
                p.essSpectrum.db.push_back (db (v / top));
        }
    }
    if (! report (0.7f))
        return p;

    // long-term spectra: voiced frames (display, low-mid balance) and all non-silent frames (rumble)
    {
        const int N = 1 << (int) std::lround (std::log2 (0.085 * fs));      // ~85 ms: 0.5-1 Hz-wide bins at 48 kHz
        Welch voiced (N), all (N);
        const float silentThr = p.noiseFloorDb + 3.0f;
        int progressTick = 0;
        for (size_t s0 = 0; s0 + (size_t) N <= n; s0 += (size_t) N / 2)
        {
            // frame level from the 20 ms levels it spans
            float lv = -200.0f;
            int loud = 0, cnt = 0;
            for (size_t f = s0 / (size_t) fr; f < std::min (nFrames, (s0 + (size_t) N) / (size_t) fr); ++f, ++cnt)
            {
                lv = std::max (lv, frameDb[f]);
                loud += frameDb[f] > activeThr ? 1 : 0;
            }
            if (lv > silentThr)
                all.add (mono.data() + s0);
            if (cnt > 0 && loud == cnt && lv > p.voiceLevelDb - 20.0f)
                voiced.add (mono.data() + s0);
            if (++progressTick % 64 == 0 && ! report (0.7f + 0.25f * (float) s0 / (float) n))
                return p;
        }
        const double df = fs / N;
        auto bandPower = [&] (const std::vector<double>& acc, double lo, double hi)
        {
            double e = 0.0;
            for (auto k = (size_t) std::ceil (lo / df); k <= (size_t) std::floor (hi / df) && k < acc.size(); ++k)
                e += acc[k];
            return e;
        };
        if (all.frames > 0)
            p.rumbleDb = db (bandPower (all.acc, 20.0, 60.0) / (bandPower (all.acc, 150.0, 1000.0) + 1.0e-30));
        if (voiced.frames > 0)
        {
            const auto g = logGrid (40.0, std::min (16000.0, 0.45 * fs), 12);
            const auto s = smoothOct (voiced.acc, df, g, 1.0 / 3.0);
            double top = 0.0;
            for (double v : s)
                top = std::max (top, v);
            p.ltas.hz = g;
            for (double v : s)
                p.ltas.db.push_back (db (v / top));
            // low-mid balance against dry studio voices (D9): the 1/3-octave long-term spectrum against its own
            // 2-octave trend, minus the 27-voice reference mean, over the reference spread. Informational: a voice's
            // own formants and a booth's colouration look alike in a long-term spectrum (leave-one-out, 6 of 27 dry
            // voices exceed z 2; 1 of 27 exceeds z 2.5), so the Dojo shows it and never corrects it automatically.
            std::vector<float> rg (ref.hz, ref.hz + reference::kBoxPoints);
            const auto fine = smoothOct (voiced.acc, df, rg, 1.0 / 3.0);
            const auto broad = smoothOct (voiced.acc, df, rg, 2.0);
            int best = -1;
            for (int i = 0; i < reference::kBoxPoints; ++i)
            {
                const float excess = db (fine[(size_t) i] / broad[(size_t) i]) - ref.mean[i];
                p.lowMid.hz.push_back (rg[(size_t) i]);
                p.lowMid.db.push_back (excess);
                p.lowMidSpread.push_back (ref.sd[i]);
                if (rg[(size_t) i] >= reference::kSearchLo && rg[(size_t) i] <= reference::kSearchHi
                    && (best < 0 || excess / ref.sd[i] > p.lowMid.db[(size_t) best] / ref.sd[best]))
                    best = i;
            }
            if (best >= 0)
            {
                p.lowMidHz = rg[(size_t) best];
                p.lowMidExcessDb = p.lowMid.db[(size_t) best];
                p.lowMidZ = p.lowMidExcessDb / ref.sd[best];
                p.lowMidOutside = p.lowMidZ >= reference::kOutsideZ;
            }
        }
    }
    if (! report (0.9f))
        return p;

    // low-frequency bursts: 10 ms envelope of 20-100 Hz against the voice level
    {
        axell::Biquad hp, lp1, lp2;
        hp.setHighPass (fs, 20.0, 0.7071);
        lp1.setLowPass (fs, 100.0, 0.5412);   // 4th-order Butterworth low-pass
        lp2.setLowPass (fs, 100.0, 1.3066);
        const int h = (int) std::lround (0.01 * fs);
        std::vector<float> lev;
        double e = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double y = lp2.process (lp1.process (hp.process (mono[i], 0), 0), 0);
            e += y * y;
            if ((i + 1) % (size_t) h == 0)
            {
                const size_t f = i / (size_t) fr;
                if (f < nFrames && frameDb[f] > activeThr)
                    lev.push_back (db (e / h));
                e = 0.0;
            }
        }
        if (! lev.empty())
            p.lfBurstDb = pct (lev, 0.995) - p.voiceLevelDb;
    }
    report (1.0f);
    return p;
}
} // namespace dojo::voice
