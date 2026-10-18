// Axell Signature Dojo - model tests (JUCE-free: the model headers exactly as the app compiles them).
// Run from the repository root: build/DojoCoreTests
#include "model/ReaperChunk.h"
#include "model/Streaming.h"
#include "model/ToneMatch.h"
#include "model/Tar.h"
#include "model/Version.h"
#include "model/VoiceAnalysis.h"
#include "model/VoiceMapping.h"
#include "WavIO.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

static int passed = 0, failed = 0;
static void check (bool ok, const std::string& what)
{
    (ok ? passed : failed)++;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}
static std::string num (double v, int d = 2)
{
    char b[64];
    std::snprintf (b, sizeof (b), "%.*f", d, v);
    return b;
}

//== versions ====================================================================================================
static void testVersion()
{
    using dojo::Version;
    check (Version::parse ("2.0.1").valid && Version::parse ("2.0.1").toString() == "2.0.1", "version: 2.0.1");
    check (Version::parse ("v2.1.0") == Version::parse ("2.1.0"), "version: v prefix");
    check (Version::parse ("1.2").valid && Version::parse ("1.2").patch == 0, "version: two parts");
    check (! Version::parse ("2.0.0-beta").valid && ! Version::parse ("").valid && ! Version::parse ("1..2").valid
               && ! Version::parse ("2.0.").valid && ! Version::parse ("x2.0.0").valid,
           "version: rejects pre-releases and junk");
    check (Version::parse ("2.0.9") < Version::parse ("2.0.10") && ! (Version::parse ("2.0.10") < Version::parse ("2.0.9")),
           "version: numeric order (2.0.10 after 2.0.9)");
}

//== tar =========================================================================================================
namespace tarw
{
static void octal (char* p, size_t n, unsigned long long v)
{
    std::snprintf (p, n, "%0*llo", (int) n - 1, v);
}
// a ustar header (+ data) for `name`; `type` '0' file, '5' dir, '2' symlink
static std::string entry (const std::string& name, const std::string& data, char type = '0', bool badSum = false)
{
    char h[512];
    std::memset (h, 0, sizeof (h));
    std::memcpy (h, name.data(), std::min<size_t> (name.size(), 100));
    octal (h + 100, 8, 0644);
    octal (h + 108, 8, 0);
    octal (h + 116, 8, 0);
    octal (h + 124, 12, data.size());
    octal (h + 136, 12, 0);
    h[156] = type;
    std::memcpy (h + 257, "ustar", 6);
    std::memcpy (h + 263, "00", 2);
    std::memset (h + 148, ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h)
        sum += c;
    octal (h + 148, 7, sum + (badSum ? 1 : 0));
    std::string out (h, 512);
    out += data;
    out.append ((512 - data.size() % 512) % 512, '\0');
    return out;
}
static std::string end() { return std::string (1024, '\0'); }
} // namespace tarw

static dojo::tar::Result readTar (const std::string& s)
{
    return dojo::tar::read (reinterpret_cast<const uint8_t*> (s.data()), s.size());
}

static void testTar()
{
    using namespace tarw;
    {
        const auto t = entry ("A-1.0.0/", "", '5') + entry ("A-1.0.0/B.vst3/Contents/x.so", "ELF-binary") + entry ("./A-1.0.0/README.txt", "hi") + end();
        const auto r = readTar (t);
        check (r.ok() && r.entries.size() == 3 && r.entries[1].path == "A-1.0.0/B.vst3/Contents/x.so" && r.entries[1].size == 10
                   && std::string (t.data() + r.entries[1].offset, 10) == "ELF-binary" && r.entries[2].path == "A-1.0.0/README.txt",
               "tar: reads files and directories, normalises ./");
    }
    check (! readTar (entry ("", "", '5') + end()).ok(), "tar: a directory entry with an empty name is rejected (it used to read past an empty string)");
    check (! readTar (entry ("", "abc") + end()).ok(), "tar: a file entry with an empty name is rejected");
    check (! readTar (entry ("../evil", "x") + end()).ok(), "tar: rejects .. paths");
    check (! readTar (entry ("a/../../evil", "x") + end()).ok(), "tar: rejects .. inside a path");
    check (! readTar (entry ("/etc/passwd", "x") + end()).ok(), "tar: rejects absolute paths");
    check (! readTar (entry ("link", "", '2') + end()).ok(), "tar: rejects symlinks");
    check (! readTar (entry ("dev", "", '3') + end()).ok(), "tar: rejects devices");
    check (! readTar (entry ("a.txt", "x", '0', true) + end()).ok(), "tar: rejects a bad header checksum");
    {
        auto t = entry ("a.txt", std::string (2000, 'x'));
        t.resize (900);   // cut inside the data
        check (! readTar (t).ok(), "tar: rejects a truncated archive");
    }
    check (! readTar (entry ("a.txt", "x")).ok(), "tar: rejects a missing end marker");
    {
        // GNU long name
        const std::string longName = std::string (120, 'n') + "/file.bin";
        const auto t = entry ("././@LongLink", longName + '\0', 'L') + entry ("short", "data") + end();
        const auto r = readTar (t);
        check (r.ok() && r.entries.size() == 1 && r.entries[0].path == longName, "tar: GNU long names");
    }
    {
        // pax path record
        const std::string rec = "path=pax/name.txt\n";
        const std::string full = std::to_string (rec.size() + 3) + " " + rec;
        const auto t = entry ("PaxHeader", full, 'x') + entry ("ignored", "abc") + end();
        const auto r = readTar (t);
        check (r.ok() && r.entries.size() == 1 && r.entries[0].path == "pax/name.txt", "tar: pax path records");
    }
    {
        // a real release archive, unpacked with gzip outside this test
        std::ifstream f ("build/test-release.tar", std::ios::binary);
        if (f)
        {
            std::string s ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
            const auto r = readTar (s);
            bool hasSo = false;
            for (auto& e : r.entries)
                hasSo |= e.path.find (".vst3/Contents/x86_64-linux/") != std::string::npos && e.path.size() > 3
                         && e.path.compare (e.path.size() - 3, 3, ".so") == 0;
            check (r.ok() && hasSo, "tar: a published Axell release archive (" + std::to_string (r.entries.size()) + " entries)");
        }
    }
}

//== REAPER chunks ===============================================================================================
static std::vector<std::string> fixture (const char* path)
{
    std::vector<std::string> lines;
    std::ifstream f (path);
    for (std::string l; std::getline (f, l);)
        lines.push_back (l);
    return lines;
}

static void testReaper()
{
    using namespace dojo::reaper;
    {
        std::mt19937 rng (7);
        bool ok = true;
        for (int len = 0; len < 300 && ok; ++len)
        {
            std::vector<uint8_t> v ((size_t) len);
            for (auto& b : v)
                b = (uint8_t) rng();
            std::vector<uint8_t> back;
            ok = base64Decode (base64Encode (v.data(), v.size()), back) && back == v;
        }
        std::vector<uint8_t> tmp;
        check (ok && ! base64Decode ("abc", tmp) && ! base64Decode ("ab=c", tmp) && ! base64Decode ("a*bc", tmp),
               "base64: round trip 0-299 bytes, strict on malformed input");
    }
    // real blocks written by REAPER 7
    for (auto [file, uid, ins, prog] : { std::tuple { "tests/fixtures/duo-block.txt", 1854355463, 4, "Rap Lead" },
                                         std::tuple { "tests/fixtures/penta-block.txt", 42278411, 4, "Init (Flat)" } })
    {
        auto lines = fixture (file);
        if (lines.size() < 3)
        {
            check (false, std::string ("fixture missing: ") + file);
            continue;
        }
        const std::vector<std::string> body (lines.begin() + 1, lines.end() - 1);
        const auto p = parseBlock (body);
        check (p.ok && p.uid == uid && p.inputs == ins && p.outputs == 2 && p.program == prog && ! p.component.empty(),
               std::string ("REAPER block parsed: ") + file + " (component " + std::to_string (p.component.size()) + " bytes)");
        // re-encode from the parsed parts: byte-identical text
        Vst3Plugin v;
        v.uid = p.uid;
        v.inputs = p.inputs;
        v.outputs = p.outputs;
        v.component = p.component;
        v.controller = p.controller;
        v.program = p.program;
        const auto first = lines.front();
        const auto indent = first.substr (0, first.find ('<'));
        const auto q1 = first.find ('"') + 1, q2 = first.find ('"', q1);
        v.name = first.substr (q1 + 6, q2 - q1 - 6);   // after "VST3: "
        const auto q3 = first.find ('"', q2 + 1) + 1, q4 = first.find ('"', q3);
        v.file = first.substr (q3, q4 - q3);
        v.classId = first.substr (first.find ('{') + 1, 32);
        const auto text = vstBlock (v, indent);
        std::ostringstream want;
        for (auto& l : lines)
            want << l << "\n";
        check (text == want.str(), std::string ("REAPER block re-encoded byte-identical: ") + file);
    }
    {
        Vst3Plugin v;
        v.name = "Axell Gate (Axell Yobag)";
        v.file = "Axell Gate.vst3";
        v.uid = -123;
        v.classId = "ABCDEF019182FAEB4178796247746531";
        v.inputs = 4;
        v.component.assign (96, 7);   // exactly one full line of base64: the split must still be right
        v.program = "My \"Voice\"";
        std::vector<std::string> body;
        std::istringstream in (vstBlock (v));
        std::string l;
        std::getline (in, l);
        while (std::getline (in, l))
            if (l != ">")
                body.push_back (l);
        const auto p = parseBlock (body);
        check (p.ok && p.component == v.component && p.program == v.program && p.uid == -123 && p.inputs == 4,
               "REAPER block: a state filling its last line exactly, negative uid, quotes in the name");
        check (quote ("a\"b") == "'a\"b'" && quote ("a\"b'c") == "`a\"b'c`", "REAPER quoting rules");
        const auto chain = fxChain ({ { v, false, "{0864C772-E9E8-C9BF-2AE3-3F10CB32B614}" } });
        check (chain.rfind ("BYPASS 0 0 0\n<VST \"VST3: Axell Gate (Axell Yobag)\" \"Axell Gate.vst3\" 0 \"\" -123{ABCDEF019182FAEB4178796247746531} \"\"\n", 0) == 0
                   && chain.find ("PRESETNAME 'My \"Voice\"'\nFLOATPOS 0 0 0 0\nFXID {0864C772-E9E8-C9BF-2AE3-3F10CB32B614}\nWAK 0 0\n") != std::string::npos,
               "REAPER FX chain text");
    }
}

//== voice analysis ==============================================================================================
struct Sig
{
    double fs = 48000.0;
    std::vector<float> l, r;
};

// a "voice": syllables of a harmonic tone with a formant-ish spectrum, separated by pauses, over a noise floor
static Sig syllables (double f0, double seconds, double noiseDb, unsigned seed = 1, bool stereoTail = false)
{
    Sig s;
    const size_t n = (size_t) (seconds * s.fs);
    s.l.assign (n, 0.0f);
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0.0, 1.0);
    const double noise = std::pow (10.0, noiseDb / 20.0);
    double ph = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / s.fs;
        const double cyc = std::fmod (t, 0.5);                 // 0.3 s syllable, 0.2 s pause
        const double env = cyc < 0.3 ? std::sin (3.14159265358979 * cyc / 0.3) : 0.0;
        const double f = f0 * (1.0 + 0.03 * std::sin (2.0 * 3.14159265358979 * 0.7 * t));   // gentle intonation
        ph += 2.0 * 3.14159265358979 * f / s.fs;
        double v = 0.0;
        for (int k = 1; k <= 30; ++k)
            if (k * f < 0.45 * s.fs)
                v += std::sin (k * ph) / (1.0 + std::pow ((k * f - 600.0) / 900.0, 2.0)) / k;
        s.l[i] = (float) (0.25 * env * v + noise * g (rng));
    }
    s.r = s.l;
    if (stereoTail)   // a decorrelated reverb-like tail on the right channel only
    {
        double y = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            y = 0.9995 * y + 0.02 * s.l[i] * g (rng);
            s.r[i] = (float) (s.l[i] + y);
        }
    }
    return s;
}

static dojo::voice::Profile run (const Sig& s, int ch = 2)
{
    const float* p[2] = { s.l.data(), s.r.data() };
    return dojo::voice::analyse (p, ch, s.l.size(), s.fs);
}

static void testAnalysis()
{
    using namespace dojo::voice;
    {
        // EBU Tech 3341 style: 1 kHz sine at -23 dBFS on one channel (mono) reads -23 LUFS; on both, -20 LUFS
        Sig s;
        const size_t n = (size_t) (20 * s.fs);
        s.l.resize (n);
        for (size_t i = 0; i < n; ++i)
            s.l[i] = (float) (std::pow (10.0, -23.0 / 20.0) * std::sqrt (2.0) * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / s.fs));
        s.r = s.l;
        const auto mono = run (s, 1), stereo = run (s, 2);
        check (std::abs (mono.integratedLufs + 23.0f) < 0.1f && std::abs (stereo.integratedLufs + 20.0f) < 0.1f,
               "loudness: -23 dBFS 1 kHz sine = -23 LUFS mono, -20 LUFS on two channels (got " + num (mono.integratedLufs)
                   + " / " + num (stereo.integratedLufs) + ")");
        check (std::abs (mono.peakDbfs - (-23.0f + 3.0103f)) < 0.05f && mono.clippedRuns == 0, "peak and no clipping");
    }
    for (double f0 : { 95.0, 140.0, 210.0 })
    {
        const auto p = run (syllables (f0, 12.0, -80.0));
        check (std::abs (p.f0Median / f0 - 1.0) < 0.02 && p.f0Low > 0.9 * f0 && p.f0High < 1.1 * f0,
               "pitch: f0 " + num (f0, 0) + " Hz (+-3 %) read as median " + num (p.f0Median, 1) + " (" + num (p.f0Low, 1) + "-"
                   + num (p.f0High, 1) + ")");
    }
    {
        const auto p = run (syllables (140.0, 12.0, -70.0));
        check (std::abs (p.noiseFloorDb - (-70.0f)) < 3.0f && p.snrDb > 30.0f,
               "noise floor: -70 dBFS noise read as " + num (p.noiseFloorDb, 1) + " dB (SNR " + num (p.snrDb, 1) + " dB)");
        check (! p.processedTake && p.sidePauseDb < -100.0f, "a dual-mono take is not flagged as processed");
    }
    {
        const auto p = run (syllables (140.0, 12.0, -70.0, 3, true));
        check (p.processedTake, "a stereo reverb tail in the pauses is flagged (side re mid in pauses " + num (p.sidePauseDb, 1) + " dB)");
    }
    {
        auto s = syllables (140.0, 12.0, -80.0);
        for (size_t i = 1000; i < 1010; ++i)
            s.l[i] = s.r[i] = 1.0f;
        for (size_t i = 5000; i < 5002; ++i)   // 2 samples: not a run of 3
            s.l[i] = s.r[i] = -1.0f;
        const auto p = run (s);
        check (p.clippedRuns == 2, "clipping: one 10-sample run counted once per channel, a 2-sample spike not (got "
                                       + std::to_string (p.clippedRuns) + ")");
    }
    {
        // rumble: a 40 Hz tone 20 dB under the voice body is measured near there
        auto s = syllables (140.0, 12.0, -80.0);
        const auto base = run (s);
        for (size_t i = 0; i < s.l.size(); ++i)
            s.l[i] = s.r[i] = s.l[i] + 0.004f * (float) std::sin (2.0 * 3.14159265358979 * 40.0 * i / s.fs);
        const auto p = run (s);
        check (p.rumbleDb > base.rumbleDb + 15.0f, "rumble: an added 40 Hz hum raises the measure from " + num (base.rumbleDb, 1)
                                                        + " to " + num (p.rumbleDb, 1) + " dB");
    }
    {
        Sig s;
        s.l.assign ((size_t) s.fs, 0.0f);
        s.r = s.l;
        const auto p = run (s);
        check (p.seconds == 1.0 && p.voicedHops == 0 && p.integratedLufs <= -70.0f, "a 1 s silent file is handled (too short)");
        s.l.assign ((size_t) (10 * s.fs), 0.0f);
        s.r = s.l;
        const auto q = run (s);
        check (q.voicedHops == 0 && q.integratedLufs <= -70.0f && std::isfinite (q.snrDb), "10 s of digital silence is handled");
    }
    {
        // the artist's own dry take, when present: the numbers measured by Axell Velvet and Gate before
        wavio::Audio a;
        const char* home = std::getenv ("HOME");
        if (home != nullptr && wavio::read (std::string (home) + "/Desktop/dry vocal.wav", a) && a.ch.size() == 2)
        {
            const float* p2[2] = { a.ch[0].data(), a.ch[1].data() };
            const auto p = analyse (p2, 2, a.frames(), a.rate);
            check (std::abs (p.integratedLufs + 32.9f) < 0.15f, "artist's dry take: -32.9 LUFS as Velvet's LEARN measured (got " + num (p.integratedLufs) + ")");
            check (std::abs (p.f0Low - 131.0f) < 4.0f && std::abs (p.f0High - 187.0f) < 4.0f,
                   "artist's dry take: pitch 131-187 Hz as measured for Velvet (got " + num (p.f0Low, 0) + "-" + num (p.f0High, 0) + ")");
            check (std::abs (std::log2 (p.sHz / 8700.0)) < 1.0 / 6.0 && std::abs (std::log2 (p.shHz / 3250.0)) < 1.0 / 6.0,
                   "artist's dry take: ess clusters within 1/6 octave of Velvet's (s " + num (p.sHz, 0) + ", sh " + num (p.shHz, 0) + " Hz)");
            check (p.rt60 > 0.2f && p.rt60 < 0.45f, "artist's dry take: room RT60 near the measured 0.33 s (got " + num (p.rt60) + " s)");
        }
        else
            std::printf ("skip  artist's dry take not present\n");
    }
}

//== mapping =====================================================================================================
static void testMapping()
{
    using namespace dojo::voice;
    Profile p;
    p.voicedHops = 5000;
    p.f0Low = 120.0f;
    p.snrDb = 50.0f;
    p.rt60 = 0.33f;
    p.sibilantHops = 600;
    p.sShare = 0.8f;
    p.peakDbfs = -6.0f;
    auto find = [] (const PluginPlan& pp, const std::string& name)
    {
        for (auto& s : pp.settings)
            if (s.param == name)
                return s.text;
        return std::string ("<none>");
    };
    auto pl = plan (p);
    check (pl.chain.size() == 5 && pl.chain[0].plugin == "dereverb" && pl.chain[4].plugin == "velvet", "plan: five plugins in signal order");
    check (find (pl.chain[0], "Reduction") == "85 %", "plan: a 0.33 s room gets De-Reverb Natural (85 %)");
    check (find (pl.chain[1], "Key High-Pass") == "85 Hz" && find (pl.chain[1], "Range") == "-44.0 dB", "plan: gate key 0.7 x f0 low, range from the SNR");
    check (find (pl.chain[2], "HP Frequency") == "90.0 Hz", "plan: EQ high-pass 0.75 x f0 low (rounded to 5 Hz)");
    check (find (pl.chain[3], "Sidechain HPF") == "95 Hz", "plan: compressor side-chain 0.8 x f0 low");
    check (find (pl.chain[4], "De-Ess Focus") == "S", "plan: s-heavy esses get focus S");
    check (pl.notes.empty(), "plan: a clean take gets no recording notes");
    p.rt60 = 0.1f;
    check (find (plan (p).chain[0], "Reduction") == "60 %", "plan: a dry booth gets Gentle (60 %)");
    p.processedTake = true;
    p.clippedRuns = 3;
    auto pl2 = plan (p);
    check (find (pl2.chain[0], "Reduction") == "85 %" && pl2.notes.size() == 2, "plan: a processed, clipped take: safe De-Reverb default and two notes");
    p = Profile();
    check (find (plan (p).chain[2], "HP Frequency") == "75.0 Hz", "plan: no pitch measured -> a safe 75 Hz high-pass");
}

//== Reference Match =============================================================================================
static void testToneMatch()
{
    using namespace dojo;
    // a take curve and a reference that is the same voice with three known bells: the fit must find them
    voice::Curve take;
    for (float f = 40.0f; f <= 16000.0f; f *= std::pow (2.0f, 1.0f / 12.0f))
    {
        take.hz.push_back (f);
        take.db.push_back (-6.0f * std::log2 (f / 500.0f) * (f > 500.0f ? 1.0f : -0.5f));   // a voice-like tilt
    }
    const std::vector<tone::Bell> truth { { 300.0f, -3.0f, 1.2f }, { 3000.0f, 4.0f, 1.0f }, { 9000.0f, 2.5f, 0.8f } };
    voice::Curve ref = take;
    for (size_t i = 0; i < ref.hz.size(); ++i)
        for (const auto& b : truth)
            ref.db[i] += (float) tone::detail::bellDb (ref.hz[i], b);
    const auto m = tone::match (take, ref);
    check (m.bells.size() >= 3 && m.residualRmsDb < 0.35f * m.targetRmsDb,
           "Reference Match: three known bells recovered (" + std::to_string (m.bells.size()) + " bells, difference " + num (m.targetRmsDb)
               + " -> " + num (m.residualRmsDb) + " dB RMS)");
    bool found3k = false;
    for (const auto& b : m.bells)
        found3k |= std::abs (std::log2 (b.hz / 3000.0f)) < 0.25f && b.gainDb > 2.0f;
    check (found3k, "Reference Match: the +4 dB at 3 kHz is found near 3 kHz");
    const auto same = tone::match (take, take);
    check (same.bells.empty() && same.targetRmsDb < 0.01f, "Reference Match: the same voice needs no bells");
    // loudness never matters: the reference 10 dB louder everywhere gives the same plan
    auto louder = ref;
    for (auto& d : louder.db)
        d += 10.0f;
    check (std::abs (tone::match (take, louder).targetRmsDb - m.targetRmsDb) < 0.01f, "Reference Match: only the shape counts, not the level");
    const auto half = tone::match (take, ref, 0.5f);
    check (std::abs (half.targetRmsDb - 0.5f * m.targetRmsDb) < 0.05f, "Reference Match: amount 50 % halves the target");
}

//== Streaming Preview ===========================================================================================
static void testStreaming()
{
    using namespace dojo::streaming;
    const double fs = 48000.0;
    auto sine = [&] (float dbfs, double seconds)
    {
        std::vector<float> v ((size_t) (seconds * fs));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (std::pow (10.0, dbfs / 20.0) * std::sqrt (2.0) * std::sin (2.0 * 3.14159265358979 * 997.0 * i / fs));
        return v;
    };
    // a loud master: -8 LUFS stereo (two channels of a -11 dBFS RMS sine)
    {
        auto x = sine (-11.0f, 20.0);
        const float* p[2] = { x.data(), x.data() };
        const auto m = measure (p, 2, x.size(), fs);
        check (std::abs (m.integratedLufs + 8.0f) < 0.15f && std::abs (m.truePeakDbtp - (-11.0f + 3.01f)) < 0.1f,
               "streaming: -8 LUFS, true peak " + num (m.truePeakDbtp) + " dBTP measured");
        const auto pb = playback (m);
        check (std::abs (pb[0].gainDb + 6.0f) < 0.2f && std::abs (pb[1].gainDb + 8.0f) < 0.2f && std::abs (pb[2].gainDb + 6.0f) < 0.2f,
               "streaming: a loud master is turned down (Spotify " + num (pb[0].gainDb) + ", Apple " + num (pb[1].gainDb) + " dB)");
    }
    // a quiet master: -20 LUFS with a high crest: Spotify turns it up only to -1 dBTP, YouTube leaves it
    {
        auto x = sine (-23.0f, 20.0);
        for (size_t i = 0; i < x.size(); i += 4800)
            x[i] = 0.5f;   // short peaks at -6 dBFS
        const float* p[2] = { x.data(), x.data() };
        const auto m = measure (p, 2, x.size(), fs);
        const auto pb = playback (m);
        check (pb[0].gainDb > 0.0f && m.truePeakDbtp + pb[0].gainDb <= -1.0f + 0.01f && pb[2].gainDb == 0.0f,
               "streaming: a quiet master: Spotify +" + num (pb[0].gainDb) + " dB (peak stays under -1 dBTP), YouTube 0 dB");
    }
}

//== confidence ==================================================================================================
static void testConfidence()
{
    using namespace dojo::voice;
    Profile p;
    p.sampleRate = 48000.0; p.seconds = 60.0; p.activeSeconds = 45.0; p.voicedHops = 15000; p.sibilantHops = 900; p.snrDb = 50.0f;
    bool allHigh = true;
    for (auto& c : confidence (p))
        allHigh &= c.level == Confidence::Level::high;
    check (allHigh, "confidence: a full, clean take is trusted everywhere");
    p.processedTake = true;
    p.sibilantHops = 40;
    p.clippedRuns = 2;
    const auto c = confidence (p);
    check (c[0].level == Confidence::Level::low && c[3].level == Confidence::Level::low && c[4].level == Confidence::Level::low
               && c[2].level == Confidence::Level::high, "confidence: clipping, few esses and printed effects lower exactly those measures");
}

int main()
{
    testVersion();
    testTar();
    testReaper();
    testAnalysis();
    testMapping();
    testToneMatch();
    testStreaming();
    testConfidence();
    std::printf ("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
