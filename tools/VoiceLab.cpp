// Axell Signature Dojo - Voice Profile calibration and validation (JUCE-free; docs/DESIGN.md, D9-D10).
//
//   VoiceLab reference <out.inc> <speaker dir>...      dry-voice low-mid reference (mean, spread) + leave-one-out
//   VoiceLab analyse <file.wav>...                      print a Voice Profile
// A speaker dir holds one speaker's WAV files; channel 1 is used (CMU ARCTIC's channel 2 is the EGG signal).
#include "../Source/model/VoiceAnalysis.h"
#include "../tests/WavIO.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace dojo::voice;

static std::vector<std::string> wavs (const std::string& dir)
{
    std::vector<std::string> v;
    for (auto& e : fs::directory_iterator (dir))
        if (e.is_regular_file() && e.path().extension() == ".wav")
            v.push_back (e.path().string());
    std::sort (v.begin(), v.end());
    return v;
}

struct Voice
{
    std::string name;
    double rate = 0.0;
    std::vector<float> x;
};

static Voice loadSpeaker (const std::string& dir, double seconds = 90.0)
{
    Voice v;
    v.name = fs::path (dir).filename().string() == "orig" ? fs::path (dir).parent_path().filename().string()
                                                           : fs::path (dir).filename().string();
    for (auto& f : wavs (dir))
    {
        wavio::Audio a;
        if (! wavio::read (f, a))
            continue;
        if (v.rate == 0.0)
            v.rate = a.rate;
        if (a.rate != v.rate)
            continue;
        v.x.insert (v.x.end(), a.ch[0].begin(), a.ch[0].end());
        if ((double) v.x.size() >= seconds * v.rate)
            break;
    }
    return v;
}

static Profile run (const std::vector<float>& x, double rate, const reference::Box& ref)
{
    const float* p[1] = { x.data() };
    return analyse (p, 1, x.size(), rate, {}, 600.0, ref);
}

// reference from curves (raw residuals against the neutral reference)
static reference::Box makeRef (const std::vector<std::vector<float>>& curves, int skip = -1)
{
    auto b = reference::kNeutral;
    for (int i = 0; i < reference::kBoxPoints; ++i)
    {
        double s = 0.0, s2 = 0.0;
        int n = 0;
        for (int k = 0; k < (int) curves.size(); ++k)
            if (k != skip)
            {
                s += curves[(size_t) k][(size_t) i];
                s2 += (double) curves[(size_t) k][(size_t) i] * curves[(size_t) k][(size_t) i];
                ++n;
            }
        const double m = s / n;
        b.mean[i] = (float) m;
        // sample standard deviation, floored at 0.5 dB so that a lucky agreement of a few voices does not make a
        // tiny deviation look significant
        b.sd[i] = (float) std::max (0.5, std::sqrt (std::max (0.0, (s2 - n * m * m) / std::max (1, n - 1))));
    }
    return b;
}

static void peakOf (const std::vector<float>& excess, const reference::Box& ref, float& hz, float& e, float& z)
{
    int best = -1;
    for (int i = 0; i < reference::kBoxPoints; ++i)
        if (ref.hz[i] >= reference::kSearchLo && ref.hz[i] <= reference::kSearchHi
            && (best < 0 || excess[(size_t) i] / ref.sd[i] > excess[(size_t) best] / ref.sd[best]))
            best = i;
    if (best < 0)
    {
        hz = 0.0f; e = 0.0f; z = 0.0f;
        return;
    }
    hz = ref.hz[best];
    e = excess[(size_t) best];
    z = e / std::max (0.1f, ref.sd[best]);
}

static std::vector<Voice> loadAll (char** dirs, int n)
{
    std::vector<Voice> v;
    for (int i = 0; i < n; ++i)
    {
        auto s = loadSpeaker (dirs[i]);
        if (s.x.size() > (size_t) (10 * s.rate))
            v.push_back (std::move (s));
        else
            std::fprintf (stderr, "skipped %s (too little audio)\n", dirs[i]);
    }
    return v;
}

static int cmdReference (const std::string& out, char** dirs, int n)
{
    auto voices = loadAll (dirs, n);
    std::vector<std::vector<float>> curves;
    for (auto& v : voices)
    {
        const auto p = run (v.x, v.rate, reference::kNeutral);
        curves.push_back (p.lowMid.db);
        std::printf ("%-12s %5.1f s  f0 %5.0f Hz  voiced hops %6d\n", v.name.c_str(), v.x.size() / v.rate, p.f0Median, p.voicedHops);
    }
    const auto ref = makeRef (curves);
    // leave-one-out: how a dry voice that is not part of the reference scores
    int fp = 0;
    std::vector<float> worst;
    for (int k = 0; k < (int) curves.size(); ++k)
    {
        const auto r = makeRef (curves, k);
        std::vector<float> ex (reference::kBoxPoints);
        for (int i = 0; i < reference::kBoxPoints; ++i)
            ex[(size_t) i] = curves[(size_t) k][(size_t) i] - r.mean[i];
        float hz, e, z;
        peakOf (ex, r, hz, e, z);
        const bool found = z >= reference::kOutsideZ;
        fp += found ? 1 : 0;
        worst.push_back (e);
        std::printf ("  held out %-18s: most unusual point %+5.2f dB (z %4.2f) at %4.0f Hz%s\n", voices[(size_t) k].name.c_str(), e, z, hz, found ? "  <- flagged" : "");
    }
    std::printf ("leave-one-out: dry voices flagged as outside the range (z >= %.1f): %d of %d\n", reference::kOutsideZ, fp, (int) curves.size());

    FILE* f = std::fopen (out.c_str(), "w");
    if (f == nullptr)
        return 1;
    auto arr = [&] (const char* name, const float* v)
    {
        std::fprintf (f, "    /* %s */ {", name);
        for (int i = 0; i < reference::kBoxPoints; ++i)
            std::fprintf (f, "%s%.3ff", i ? (i % 10 == 0 ? ",\n              " : ", ") : " ", v[i]);
        std::fprintf (f, " },\n");
    };
    std::fprintf (f, "// generated by tools/VoiceLab.cpp (reference) from %d dry studio voices; leave-one-out: %d of %d flagged\n",
                  (int) curves.size(), fp, (int) curves.size());
    std::fprintf (f, "inline constexpr Box kBox = {\n");
    arr ("hz", ref.hz);
    arr ("mean dB", ref.mean);
    arr ("spread dB", ref.sd);
    std::fprintf (f, "};\n");
    std::fclose (f);
    std::printf ("wrote %s\n", out.c_str());
    return 0;
}

static int cmdAnalyse (char** files, int n)
{
    for (int i = 0; i < n; ++i)
    {
        wavio::Audio a;
        if (! wavio::read (files[i], a))
        {
            std::fprintf (stderr, "cannot read %s\n", files[i]);
            continue;
        }
        std::vector<const float*> ptr;
        for (auto& c : a.ch)
            ptr.push_back (c.data());
        const auto p = analyse (ptr.data(), (int) ptr.size(), a.frames(), a.rate);
        std::printf ("%s\n  %.1f s (%.1f s active), %d ch, %.0f Hz\n", files[i], p.seconds, p.activeSeconds, p.channels, p.sampleRate);
        std::printf ("  loudness %.1f LUFS, peak %.1f dBFS, clipped runs %d, DC %.1f dB\n", p.integratedLufs, p.peakDbfs, p.clippedRuns, p.dcOffsetDb);
        std::printf ("  voice %.1f dB, noise floor %.1f dB, SNR %.1f dB\n", p.voiceLevelDb, p.noiseFloorDb, p.snrDb);
        std::printf ("  stereo side re mid: loud %.1f dB, pauses %.1f dB%s\n", p.sideLoudDb, p.sidePauseDb, p.processedTake ? "  -> PROCESSED (effects printed in)" : "");
        std::printf ("  f0 median %.0f Hz (10-90 %%: %.0f-%.0f), voiced hops %d\n", p.f0Median, p.f0Low, p.f0High, p.voicedHops);
        std::printf ("  esses: sh %.0f Hz, s %.0f Hz, s share %.2f, level re body %.1f dB, hops %d\n", p.shHz, p.sHz, p.sShare, p.essLevelDb, p.sibilantHops);
        std::printf ("  room: RT60 %.2f s (bands %.2f %.2f %.2f %.2f); low mids: most unusual %.0f Hz %+.1f dB (z %.1f)%s\n", p.rt60,
                     p.rt60Bands[0], p.rt60Bands[1], p.rt60Bands[2], p.rt60Bands[3], p.lowMidHz, p.lowMidExcessDb, p.lowMidZ,
                     p.lowMidOutside ? " OUTSIDE the studio-voice range" : "");
        std::printf ("  low end: rumble %.1f dB re body, LF bursts %.1f dB re voice\n", p.rumbleDb, p.lfBurstDb);
        if (std::getenv ("VOICELAB_CURVES") != nullptr)
        {
            std::printf ("  ess spectrum (1/6 oct, dB re peak):");
            for (size_t k = 0; k < p.essSpectrum.hz.size(); k += 2)
                std::printf (" %.0f:%.1f", p.essSpectrum.hz[k], p.essSpectrum.db[k]);
            std::printf ("\n  low mids:");
            for (size_t k = 0; k < p.lowMid.hz.size(); ++k)
                std::printf (" %.0f:%+.1f", p.lowMid.hz[k], p.lowMid.db[k]);
            std::printf ("\n");
        }
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "VoiceLab reference <out.inc> <dir>... | analyse <wav>...\n");
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "reference" && argc >= 4) return cmdReference (argv[2], argv + 3, argc - 3);
    if (cmd == "analyse") return cmdAnalyse (argv + 2, argc - 2);
    std::fprintf (stderr, "unknown command\n");
    return 1;
}
