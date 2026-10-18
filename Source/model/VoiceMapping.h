#pragma once
// Axell Signature Dojo - from a Voice Profile to the settings of the Axell vocal chain (docs/DESIGN.md, D12).
// Framework independent and pure: the same profile always gives the same plan.
//
// A plan says, per plugin, which factory program to start from, which parameters to set (by the name and the text
// the plugin itself shows, so a plan survives plugin updates) and why, and which values must be *measured* by
// rendering the take through the installed plugins (compression amounts, level matching, LEARN). Rules here are only
// those that follow from a measured property of the take; taste (saturation, air, colour) stays with the user.
#include "VoiceAnalysis.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace dojo::voice
{
struct Setting
{
    std::string param, text;   // e.g. "HP Frequency" = "85 Hz"
};

// a value found by rendering the take through the installed plugins (ProfileEngine)
struct Tune
{
    enum class Kind
    {
        fetPeakGr,      // FET Input so the 95th percentile of the FET's gain reduction is `target` dB
        optoMedianGr,   // Opto Peak Reduction so the opto's median gain reduction is `target` dB
        matchLoudness,  // `param` (an output gain) so the plugin's output loudness equals its input loudness
        learnLevel      // Velvet's LEVEL as its LEARN would set it: -18 LUFS minus the loudness at its input
    };
    Kind kind;
    std::string param;
    float target = 0.0f;
    std::string why;
};

struct PluginPlan
{
    std::string plugin;        // catalog key: "dereverb", "gate", "penta", "duo", "velvet"
    std::string program;       // factory program to start from ("" = the plugin's default)
    std::vector<Setting> settings;
    std::vector<std::string> why;
    std::vector<Tune> tunes;
};

struct Plan
{
    std::vector<PluginPlan> chain;      // in signal order
    std::vector<std::string> notes;     // about the take itself (recording advice)
};

namespace detail
{
inline std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof (b), f, v);
    return b;
}
inline double round5 (double v) { return 5.0 * std::round (v / 5.0); }
} // namespace detail

// The factory program names the plan starts from (as the plugins show them). Kept here so a test can check them
// against the installed plugins.
namespace programs
{
inline const char* const kGate = "Rap Lead - Clean Gaps";
inline const char* const kPenta = "Init (Flat)";
inline const char* const kDuo = "Rap Lead";
inline const char* const kVelvet = "Init";
} // namespace programs

inline Plan plan (const Profile& p)
{
    using detail::fmt;
    Plan out;
    const bool pitched = p.voicedHops > 500 && p.f0Low > 0.0f;
    const double f0Low = pitched ? p.f0Low : 100.0;   // a typical low male voice when pitch could not be measured

    // --- recording notes ---
    if (p.clippedRuns > 0)
        out.notes.push_back ("The take clips in " + std::to_string (p.clippedRuns)
                             + " places. Record about 6 dB lower: clipping cannot be undone by any plugin.");
    if (p.peakDbfs < -18.0f)
        out.notes.push_back ("The take peaks at " + fmt ("%.0f", p.peakDbfs)
                             + " dBFS. That is fine for 24-bit; the chain makes up the level.");
    if (p.snrDb < 35.0f)
        out.notes.push_back ("The background is only " + fmt ("%.0f", p.snrDb)
                             + " dB below the voice. Axell Gate's room model handles it, but a quieter room or a closer mic helps every plugin.");
    if (p.processedTake)
        out.notes.push_back ("This take has stereo effects printed in (the pauses carry a stereo tail "
                             + fmt ("%.0f", p.sidePauseDb) + " dB re the centre). Room-dependent settings use safe defaults; "
                             "a raw mono take measures your room.");
    if (p.lowMidOutside)
        out.notes.push_back ("Around " + fmt ("%.0f", p.lowMidHz) + " Hz your take is " + fmt ("%.1f", p.lowMidExcessDb)
                             + " dB above what 27 studio voices show. A voice's own tone and a boxy room look alike here, "
                             "so the Dojo does not cut it automatically: try a Penta EQ bell there and trust your ears.");

    // --- Axell De-Reverb: amount from the room's decay ---
    {
        PluginPlan d;
        d.plugin = "dereverb";
        double red = 85.0;
        if (p.processedTake)
            d.why.push_back ("Natural (85 %): the room could not be measured on a processed take.");
        else if (p.rt60 < 0.20f)
        {
            red = 60.0;
            d.why.push_back ("Gentle (60 %): your room decays in " + fmt ("%.2f", p.rt60) + " s, already dry.");
        }
        else if (p.rt60 <= 0.40f)
            d.why.push_back ("Natural (85 %): your room decays in " + fmt ("%.2f", p.rt60) + " s, a typical small room.");
        else
        {
            red = 100.0;
            d.why.push_back ("Clean (100 %): your room decays in " + fmt ("%.2f", p.rt60) + " s, a live room.");
        }
        d.settings.push_back ({ "Reduction", fmt ("%.0f %%", red) });
        d.tunes.push_back ({ Tune::Kind::matchLoudness, "Output", 0.0f, "Output matched to the input loudness." });
        out.chain.push_back (d);
    }

    // --- Axell Gate: key filter below the voice, depth from the noise floor ---
    {
        PluginPlan g;
        g.plugin = "gate";
        g.program = programs::kGate;
        const double keyHp = std::clamp (detail::round5 (0.7 * f0Low), 50.0, 150.0);
        g.settings.push_back ({ "Key High-Pass", fmt ("%.0f Hz", keyHp) });
        g.why.push_back ("Key high-pass " + fmt ("%.0f", keyHp) + " Hz: below your lowest notes ("
                         + fmt ("%.0f", f0Low) + " Hz), so rumble and pops never open the gate.");
        // close the gaps by about as much as the background sits under the voice (less would leave noise, more
        // makes the noise pump in and out)
        const double range = -std::clamp (std::round (p.snrDb - 6.0), 20.0, 60.0);
        g.settings.push_back ({ "Range", fmt ("%.1f dB", range) });
        g.why.push_back ("Range " + fmt ("%.0f", range) + " dB: the background is " + fmt ("%.0f", p.snrDb)
                         + " dB under your voice; closing further would make it pump.");
        out.chain.push_back (g);
    }

    // --- Axell Penta EQ: high-pass under the voice; level matched ---
    {
        PluginPlan e;
        e.plugin = "penta";
        e.program = programs::kPenta;
        const double hp = std::clamp (detail::round5 (0.75 * f0Low), 50.0, 120.0);
        e.settings.push_back ({ "HP On", "On" });
        e.settings.push_back ({ "HP Frequency", fmt ("%.1f Hz", hp) });
        e.settings.push_back ({ "HP Slope", "24 dB/oct" });
        e.why.push_back ("High-pass " + fmt ("%.0f", hp) + " Hz, 24 dB/oct: under your lowest notes ("
                         + fmt ("%.0f", f0Low) + " Hz); removes rumble (" + fmt ("%.0f", p.rumbleDb)
                         + " dB re the voice) without thinning the voice.");
        e.tunes.push_back ({ Tune::Kind::matchLoudness, "Output Gain", 0.0f, "Output matched to the input loudness." });
        out.chain.push_back (e);
    }

    // --- Axell Duo Compressor: measured on the take ---
    {
        PluginPlan c;
        c.plugin = "duo";
        c.program = programs::kDuo;
        const double sc = std::clamp (detail::round5 (0.8 * f0Low), 60.0, 150.0);
        c.settings.push_back ({ "Sidechain HPF", fmt ("%.0f Hz", sc) });
        c.why.push_back ("Side-chain high-pass " + fmt ("%.0f", sc) + " Hz: the detectors ignore what lies under your voice.");
        c.tunes.push_back ({ Tune::Kind::fetPeakGr, "FET Input", 6.0f,
                             "FET input set on your take: the loud syllables get about 6 dB of peak control." });
        c.tunes.push_back ({ Tune::Kind::optoMedianGr, "Opto Peak Reduction", 3.5f,
                             "Opto set on your take: about 3.5 dB of levelling most of the time." });
        c.tunes.push_back ({ Tune::Kind::matchLoudness, "Output", 0.0f, "Output matched to the input loudness." });
        out.chain.push_back (c);
    }

    // --- Axell Velvet: LEARN at its place in the chain; focus from the esses ---
    {
        PluginPlan v;
        v.plugin = "velvet";
        v.program = programs::kVelvet;
        const char* focus = "Auto";
        if (p.sibilantHops >= 50 && p.sShare >= 0.75f)
            focus = "S";
        else if (p.sibilantHops >= 50 && p.sShare <= 0.35f)
            focus = "SH";
        v.settings.push_back ({ "De-Ess Focus", focus });
        v.settings.push_back ({ "De-Ess Amount", "50 %" });
        v.why.push_back (std::string ("Focus ") + focus + ": your esses sit at " + fmt ("%.1f", p.shHz / 1000.0) + " kHz (sh) and "
                         + fmt ("%.1f", p.sHz / 1000.0) + " kHz (s), " + fmt ("%.0f", 100.0 * p.sShare) + " % of their energy in the s.");
        v.tunes.push_back ({ Tune::Kind::learnLevel, "Level", -18.0f,
                             "LEVEL learned at Velvet's place in the chain (what its LEARN button does)." });
        v.tunes.push_back ({ Tune::Kind::matchLoudness, "Output", 0.0f, "Output matched to the input loudness." });
        out.chain.push_back (v);
    }
    return out;
}
} // namespace dojo::voice
