#include "ProfileEngine.h"
#include "StateTools.h"

#include <axell/dsp/Loudness.h>
#include <axell/dsp/SibilanceDetector.h>

namespace dojo
{
namespace
{
float textDb (const juce::String& t) { return (float) t.retainCharacters ("0123456789.-+").getDoubleValue(); }

juce::AudioBuffer<float> slice (const juce::AudioBuffer<float>& a, int start, int n)
{
    juce::AudioBuffer<float> b (a.getNumChannels(), n);
    for (int c = 0; c < a.getNumChannels(); ++c)
        b.copyFrom (c, 0, a, c, start, n);
    return b;
}

float pctOf (std::vector<float> v, double p)
{
    if (v.empty())
        return 0.0f;
    const auto k = (size_t) std::clamp (p * (double) (v.size() - 1) + 0.5, 0.0, (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}
// the esses' level (D23): the sibilant hops of what Velvet receives (Velvet 3.0's network, p > 0.5, within 30 dB of
// the loudest hop) and the voice body's level (90th percentile over clearly voiced hops); a hop's ess level is its
// 2-17 kHz energy re that body. The same measure on 111 natural voices gives kNaturalEssP90Db.
struct EssMap
{
    std::vector<int> hops;      // hop indices of the sibilant hops
    float bodyRefDb = -140.0f;
};

std::vector<axell::voice::DetFrame> detectorFrames (const juce::AudioBuffer<float>& a, double rate)
{
    axell::voice::SibilanceDetector d;
    d.prepare (rate, axell::voice::DetectorConfig::velvet3());
    std::vector<axell::voice::DetFrame> out;
    const float* l = a.getReadPointer (0);
    const float* r = a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1));
    for (int i = 0; i < a.getNumSamples(); ++i)
        if (d.push (0.5f * (l[i] + r[i])))
            out.push_back (d.out());
    return out;
}

float hfDb (const axell::voice::DetFrame& f)
{
    double e = 0.0;
    for (int b = 0; b < axell::voice::kNumBands; ++b)
        e += f.E[b];
    return (float) (10.0 * std::log10 (e + 1.0e-30));
}

EssMap essMap (const std::vector<axell::voice::DetFrame>& in)
{
    EssMap m;
    float loud = -140.0f;
    for (const auto& f : in)
        loud = std::max (loud, f.levelDb);
    std::vector<float> body;
    for (size_t k = 0; k < in.size(); ++k)
    {
        const auto& f = in[k];
        if (f.p > 0.5f && f.levelDb > loud - 30.0f)
            m.hops.push_back ((int) k);
        else if (f.p < 0.3f && f.apRaw < 0.25f && f.levelDb > loud - 30.0f)
            body.push_back (f.bodyDb);
    }
    if (! body.empty())
        m.bodyRefDb = pctOf (body, 0.9);
    return m;
}

// the 90th percentile of the ess level (dB re the input's voice body) in `out`, at the input's sibilant hops
float essP90Db (const EssMap& m, const std::vector<axell::voice::DetFrame>& out)
{
    std::vector<float> lv;
    for (int k : m.hops)
        if (k < (int) out.size())
            lv.push_back (hfDb (out[(size_t) k]) - m.bodyRefDb);
    return lv.empty() ? -99.0f : pctOf (lv, 0.9);
}
} // namespace

bool ProfileEngine::loadTake (const juce::File& f, juce::AudioBuffer<float>& audio, double& rate, juce::String& error, double maxSeconds)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
    {
        error = "The file could not be read as audio (WAV, AIFF, FLAC, Ogg or MP3).";
        return false;
    }
    rate = r->sampleRate;
    if (rate < 22050.0 || rate > 192000.0)
    {
        error = "Unsupported sample rate " + juce::String (juce::roundToInt (rate)) + " Hz.";
        return false;
    }
    const auto n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (maxSeconds * rate));
    if (n < (int) (5.0 * rate))
    {
        error = "The take is shorter than 5 seconds: record at least 30 seconds of your voice.";
        return false;
    }
    audio.setSize (2, n);
    r->read (&audio, 0, n, 0, true, r->numChannels > 1);
    if (r->numChannels == 1)
        audio.copyFrom (1, 0, audio, 0, 0, n);
    return true;
}

float ProfileEngine::lufs (const juce::AudioBuffer<float>& a, double rate)
{
    axell::LoudnessMeter m;
    m.prepare (rate, a.getNumChannels());
    const float* p[2] = { a.getReadPointer (0), a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1)) };
    m.process (p, a.getNumChannels(), a.getNumSamples());
    const float l = m.integratedLufs();
    return std::isfinite (l) ? l : -70.0f;
}

std::vector<float> ProfileEngine::envelopeDb (const juce::AudioBuffer<float>& a, double rate, double ms)
{
    const int h = juce::jmax (1, (int) std::lround (ms * 0.001 * rate));
    std::vector<float> e ((size_t) (a.getNumSamples() / h));
    for (size_t f = 0; f < e.size(); ++f)
    {
        double s = 0.0;
        for (int i = 0; i < h; ++i)
        {
            const double m = 0.5 * ((double) a.getSample (0, (int) f * h + i) + a.getSample (juce::jmin (1, a.getNumChannels() - 1), (int) f * h + i));
            s += m * m;
        }
        e[f] = (float) (10.0 * std::log10 (s / h + 1.0e-20));
    }
    return e;
}

ProfileOutcome ProfileEngine::run (const juce::File& take, bool tune, const Progress& progress, const std::atomic<bool>* cancel,
                                   const juce::File& reference, float amount) const
{
    ProfileOutcome out;
    out.take = take;
    auto say = [&] (float f, const juce::String& s) { if (progress) progress (f, s); };
    auto cancelled = [&] { return cancel != nullptr && cancel->load(); };

    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    say (0.0f, "Reading the take");
    if (! loadTake (take, audio, rate, out.error))
        return out;
    out.sampleRate = rate;

    const float* ch[2] = { audio.getReadPointer (0), audio.getReadPointer (1) };
    out.profile = voice::analyse (ch, 2, (size_t) audio.getNumSamples(), rate, [&] (float f)
    {
        say (0.25f * f, "Listening to your voice");
        return ! cancelled();
    });
    if (cancelled())
    {
        out.error = "Cancelled.";
        return out;
    }
    if (out.profile.activeSeconds < 8.0)
    {
        out.error = "Only " + juce::String (out.profile.activeSeconds, 1) + " seconds of voice were found. Record at least 30 seconds.";
        return out;
    }
    out.plan = voice::plan (out.profile);
    if (adjustPlan)
        adjustPlan (out.plan);
    for (const auto& pp : out.plan.chain)
    {
        StageOutcome s;
        s.product = Catalog::find (pp.plugin);
        s.program = pp.program;
        for (auto& w : pp.why)
            s.why.add (w);
        for (auto& st : pp.settings)
            s.values.push_back ({ st.param, st.text });
        out.stages.push_back (std::move (s));
    }
    // the reference's voiced spectrum, and the bells that move the take toward it
    voice::Curve refLtas;
    if (reference.existsAsFile())
    {
        juce::AudioBuffer<float> ref;
        double refRate = 48000.0;
        juce::String err;
        say (0.25f, "Listening to the reference");
        if (! loadTake (reference, ref, refRate, err))
        {
            out.error = "The reference: " + err;
            return out;
        }
        const float* rch[2] = { ref.getReadPointer (0), ref.getReadPointer (1) };
        refLtas = voice::analyse (rch, 2, (size_t) ref.getNumSamples(), refRate).ltas;
        out.reference = reference;
        out.toneBeforeDb = tone::match (out.profile.ltas, refLtas, 1.0f).targetRmsDb;
        out.bells = tone::match (out.profile.ltas, refLtas, juce::jlimit (0.0f, 1.0f, amount)).bells;
        for (auto& st : out.stages)
            if (st.product != nullptr && st.product->key == "penta")
                st.why.add ("Reference Match with " + reference.getFileName() + ": " + juce::String ((int) out.bells.size())
                            + " bells move your tone toward it (difference " + juce::String (out.toneBeforeDb, 1) + " dB RMS before).");
    }
    if (! tune)
    {
        out.ok = true;
        return out;
    }

    // the busiest 20 s carry the tuning; the whole take goes through the tuned chain once per stage
    const int exLen = juce::jmin (audio.getNumSamples(), (int) (20.0 * rate));
    int exStart = 0;
    {
        const auto& env = out.profile.envelope;   // 20 ms levels
        const int win = exLen / (int) std::lround (0.02 * rate);
        const float thr = out.profile.voiceLevelDb - 20.0f;
        int best = -1, count = 0;
        for (int i = 0; i < (int) env.size(); ++i)
        {
            count += env[(size_t) i] > thr ? 1 : 0;
            if (i >= win)
                count -= env[(size_t) (i - win)] > thr ? 1 : 0;
            if (i >= win - 1 && count > best)
            {
                best = count;
                exStart = (i - win + 1) * (int) std::lround (0.02 * rate);
            }
        }
        exStart = juce::jlimit (0, audio.getNumSamples() - exLen, exStart);
    }
    auto full = audio;                       // the take as it reaches the current stage
    auto excerpt = slice (audio, exStart, exLen);

    ChainWriter writer (paths);
    const int nStages = (int) out.plan.chain.size();
    for (int si = 0; si < nStages; ++si)
    {
        const auto& pp = out.plan.chain[(size_t) si];
        auto& so = out.stages[(size_t) si];
        const float base = 0.25f + 0.75f * (float) si / (float) nStages, span = 0.75f / (float) nStages;
        if (so.product == nullptr)
            continue;
        say (base, "Tuning " + so.product->name);
        BuiltStage b;
        b.product = so.product;
        juce::String err;
        b.plugin = HostedPlugin::load (paths.vst3.getChildFile (so.product->bundle), rate, 512, err);
        if (b.plugin == nullptr)
        {
            so.problem = so.product->name + " is not installed, so this step is skipped.";
            continue;
        }
        so.available = true;
        auto& h = *b.plugin;
        const juce::String program (pp.program);
        if (program.isNotEmpty() && ! h.selectProgram (program))
            so.problem = "No program \"" + program + "\" in the installed " + so.product->name + ".";
        for (const auto& st : pp.settings)
        {
            juce::String why;
            if (! h.set (st.param, st.text, &why))
                so.problem = why;
        }
        if (pp.plugin == "penta" && ! refLtas.hz.empty())
        {
            // fitted to what this EQ actually receives (the take after the stages before it), not to the raw take
            const float* ic[2] = { full.getReadPointer (0), full.getReadPointer (1) };
            const auto inLtas = voice::analyse (ic, 2, (size_t) full.getNumSamples(), rate).ltas;
            out.toneBeforeDb = tone::match (inLtas, refLtas, 1.0f).targetRmsDb;
            out.bells = tone::match (inLtas, refLtas, juce::jlimit (0.0f, 1.0f, amount)).bells;
        }
        if (pp.plugin == "penta")
            for (size_t k = 0; k < out.bells.size() && k < 6; ++k)
            {
                const auto& bell = out.bells[k];
                const juce::String band = "Band " + juce::String ((int) k + 1) + " ";
                juce::String why;
                bool ok = h.set (band + "Active", "On") && h.set (band + "Type", "Bell", &why) && h.set (band + "Mode", "Clean", &why)
                          && h.set (band + "Frequency", juce::String (bell.hz, 1) + " Hz", &why)
                          && h.set (band + "Gain", (bell.gainDb >= 0.0f ? "+" : "") + juce::String (bell.gainDb, 1) + " dB", &why)
                          && h.set (band + "Q", juce::String (bell.q, 2), &why);
                if (! ok)
                    so.problem = "Reference Match: " + why;
                so.values.push_back ({ band.trim(), h.text (band + "Frequency") + " " + h.text (band + "Gain") + " Q " + h.text (band + "Q") });
            }
        so.inLufs = lufs (full, rate);
        const float exIn = lufs (excerpt, rate);
        const auto inEnv = envelopeDb (excerpt, rate);
        const float loud = pctOf (inEnv, 0.95) - 20.0f;   // the frames that carry the voice

        for (const auto& t : pp.tunes)
        {
            if (cancelled())
                break;
            switch (t.kind)
            {
                case voice::Tune::Kind::fetPeakGr:
                {
                    // the FET alone, unity everywhere after it: GR = input + FET input - output
                    const juce::String keep[] = { "Opto On", "Limiter On", "Mix", "Output", "FET Output" };
                    std::vector<juce::String> saved;
                    for (auto& k : keep)
                        saved.push_back (h.text (k));
                    h.set ("Opto On", "Off"); h.set ("Limiter On", "Off"); h.set ("Mix", "100 %");
                    h.set ("Output", "0.0 dB"); h.set ("FET Output", "0.0 dB");
                    auto measure = [&] (float in)
                    {
                        h.set (t.param, juce::String (in, 1) + " dB");
                        const auto o = envelopeDb (h.render (excerpt, cancel), rate);
                        std::vector<float> gr;
                        for (size_t i = 0; i < std::min (o.size(), inEnv.size()); ++i)
                            if (inEnv[i] > loud)
                                gr.push_back (inEnv[i] + in - o[i]);
                        return pctOf (gr, 0.95);
                    };
                    float lo = -12.0f, hi = 30.0f;
                    for (int it = 0; it < 12 && ! cancelled(); ++it)
                    {
                        const float mid = 0.5f * (lo + hi);
                        (measure (mid) < t.target ? lo : hi) = mid;
                        say (base + span * 0.4f * (float) it / 12.0f, "Tuning " + so.product->name + ": FET");
                    }
                    const float best = std::round (5.0f * 0.5f * (lo + hi)) / 5.0f;
                    h.set (t.param, juce::String (best, 1) + " dB");
                    for (size_t k = 0; k < saved.size(); ++k)
                        h.set (keep[k], saved[k]);
                    so.values.push_back ({ t.param, h.text (t.param) });
                    so.why.add (t.why);
                    break;
                }
                case voice::Tune::Kind::optoMedianGr:
                {
                    // the opto's own work: the chain with the opto off against with it on, both at unity after it
                    const juce::String keep[] = { "Opto Gain", "Limiter On", "Mix", "Output" };
                    std::vector<juce::String> saved;
                    for (auto& k : keep)
                        saved.push_back (h.text (k));
                    h.set ("Limiter On", "Off"); h.set ("Mix", "100 %"); h.set ("Output", "0.0 dB"); h.set ("Opto Gain", "0.0 dB");
                    h.set ("Opto On", "Off");
                    const auto ref = envelopeDb (h.render (excerpt, cancel), rate);
                    h.set ("Opto On", "On");
                    auto measure = [&] (float pr)
                    {
                        h.set (t.param, juce::String (pr, 1));
                        const auto o = envelopeDb (h.render (excerpt, cancel), rate);
                        std::vector<float> gr;
                        for (size_t i = 0; i < std::min (o.size(), ref.size()); ++i)
                            if (inEnv[i] > loud)
                                gr.push_back (ref[i] - o[i]);
                        return pctOf (gr, 0.5);
                    };
                    float lo = 0.0f, hi = 100.0f;
                    for (int it = 0; it < 12 && ! cancelled(); ++it)
                    {
                        const float mid = 0.5f * (lo + hi);
                        (measure (mid) < t.target ? lo : hi) = mid;
                        say (base + span * (0.4f + 0.3f * (float) it / 12.0f), "Tuning " + so.product->name + ": opto");
                    }
                    h.set (t.param, juce::String (std::round (0.5f * (lo + hi)), 1));
                    for (size_t k = 0; k < saved.size(); ++k)
                        h.set (keep[k], saved[k]);
                    so.values.push_back ({ t.param, h.text (t.param) });
                    so.why.add (t.why);
                    break;
                }
                case voice::Tune::Kind::learnLevel:
                {
                    // what Velvet's LEARN sets: -18 LUFS minus the integrated loudness at its input
                    const float level = juce::jlimit (-24.0f, 36.0f, t.target - so.inLufs);
                    h.set (t.param, juce::String (level, 1) + " dB");
                    so.values.push_back ({ t.param, h.text (t.param) });
                    so.why.add (t.why);
                    break;
                }
                case voice::Tune::Kind::deessAmount:
                {
                    // the gentlest AMOUNT (30-70 %; above, a lisp becomes likely) that brings the esses' loud end to where
                    // natural speech has it, measured on the rendered excerpt; few esses: the plan's 50 % stays
                    const auto ess = essMap (detectorFrames (excerpt, rate));
                    auto show = [&]
                    {
                        for (auto& v : so.values)
                            if (v.first == juce::String (t.param))
                            {
                                v.second = h.text (t.param);
                                return;
                            }
                        so.values.push_back ({ t.param, h.text (t.param) });
                    };
                    if ((int) ess.hops.size() < 40)
                    {
                        show();
                        so.why.add ("AMOUNT left at " + h.text (t.param) + ": the busiest 20 s hold too few esses ("
                                    + juce::String ((int) ess.hops.size()) + " hops) to tune it on.");
                        break;
                    }
                    const auto saved = h.text (t.param);
                    auto measure = [&] (float a)
                    {
                        h.set (t.param, juce::String (a, 1) + " %");
                        return essP90Db (ess, detectorFrames (h.render (excerpt, cancel), rate));
                    };
                    const float before = measure (0.0f);
                    float chosen = 70.0f, after = 99.0f;
                    for (float a = 30.0f; a <= 70.0f && ! cancelled(); a += 5.0f)
                    {
                        after = measure (a);
                        say (base + span * (0.1f + 0.6f * (a - 30.0f) / 40.0f), "Tuning " + so.product->name + ": de-esser");
                        if (after <= t.target)
                        {
                            chosen = a;
                            break;
                        }
                    }
                    if (cancelled())
                    {
                        h.set (t.param, saved);
                        break;
                    }
                    h.set (t.param, juce::String (chosen, 1) + " %");
                    show();
                    const juce::String lvl = "the loud end of your esses " + juce::String (before, 1) + " -> " + juce::String (after, 1)
                                             + " dB re your voice body (natural speech: " + juce::String (t.target, 1) + " dB, 111 voices)";
                    if (before <= t.target)
                        so.why.add ("AMOUNT 30 %, a light touch: your esses already sit where natural speech has them - " + lvl + ".");
                    else if (after <= t.target)
                        so.why.add ("AMOUNT " + juce::String ((int) chosen) + " %: the gentlest setting that brings your esses to where "
                                    "natural speech has them - " + lvl + ".");
                    else
                        so.why.add ("AMOUNT 70 % (the most before a lisp becomes likely) - " + lvl + ". A bright mic or a close, "
                                    "on-axis position makes esses this strong: angle the mic slightly or move back a little.");
                    break;
                }
                case voice::Tune::Kind::matchLoudness:
                {
                    for (int it = 0; it < 4 && ! cancelled(); ++it)
                    {
                        const float diff = exIn - lufs (h.render (excerpt, cancel), rate);
                        if (std::abs (diff) <= 0.1f)
                            break;
                        h.set (t.param, juce::String (textDb (h.text (t.param)) + diff, 1) + " dB");
                    }
                    so.values.push_back ({ t.param, h.text (t.param) });
                    so.why.add (t.why);
                    break;
                }
            }
        }
        if (cancelled())
        {
            out.error = "Cancelled.";
            return out;
        }
        say (base + span * 0.85f, "Rendering your take through " + so.product->name);
        auto rendered = h.render (full, cancel);
        so.outLufs = lufs (rendered, rate);
        // the excerpt matched the loudness; the whole take is what counts: one exact correction of the output gain
        // (a plain gain after everything else) when the whole take landed elsewhere
        for (const auto& t : pp.tunes)
            if (t.kind == voice::Tune::Kind::matchLoudness && std::abs (so.inLufs - so.outLufs) > 0.05f)
            {
                h.set (t.param, juce::String (textDb (h.text (t.param)) + (so.inLufs - so.outLufs), 1) + " dB");
                for (auto& v : so.values)
                    if (v.first == juce::String (t.param))
                        v.second = h.text (t.param);
                rendered = h.render (full, cancel);
                so.outLufs = lufs (rendered, rate);
            }
        if (pp.plugin == "penta" && ! refLtas.hz.empty())
        {
            const float* rc[2] = { rendered.getReadPointer (0), rendered.getReadPointer (1) };
            out.toneAfterDb = tone::match (voice::analyse (rc, 2, (size_t) rendered.getNumSamples(), rate).ltas, refLtas, 1.0f).targetRmsDb;
            so.why.add ("Measured on your rendered take: tone difference to the reference " + juce::String (out.toneBeforeDb, 1) + " -> "
                        + juce::String (out.toneAfterDb, 1) + " dB RMS.");
        }
        full = std::move (rendered);
        if (full.getNumSamples() <= (int) (300.0 * rate))
            out.stageAudio.push_back (full);
        excerpt = slice (full, exStart, exLen);
        out.built.push_back (std::move (b));
    }
    out.processed = std::move (full);
    out.ok = true;
    say (1.0f, "Done");
    return out;
}

juce::StringArray ProfileEngine::preview (const ProfileOutcome& o, const WriteOptions& w) const
{
    juce::StringArray lines;
    if (w.presets)
        for (const auto& b : o.built)
            if (b.product->hasUserPresets())
                lines.add (b.product->name + " preset: " + paths.presetFolder (b.product->presetFolder)
                                                              .getChildFile (juce::File::createLegalFileName (w.name) + b.product->presetExt).getFullPathName());
    if (w.chain && ! o.built.empty())
        lines.add ("REAPER FX chain: " + paths.reaperChains().getChildFile (juce::File::createLegalFileName (w.name + " - Vocal Chain") + ".RfxChain").getFullPathName());
    if (w.chain && w.tracking && ! o.built.empty())
        lines.add ("REAPER tracking chain: " + paths.reaperChains().getChildFile (juce::File::createLegalFileName (w.name + " - Tracking Chain") + ".RfxChain").getFullPathName());
    if (w.profile)
        lines.add ("Profile: " + paths.profiles().getChildFile (juce::File::createLegalFileName (w.name) + ".json").getFullPathName());
    return lines;
}

juce::StringArray ProfileEngine::write (ProfileOutcome& o, const WriteOptions& w) const
{
    juce::StringArray lines;
    const auto name = w.name.trim().isEmpty() ? juce::String ("My Voice") : w.name.trim();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
    for (auto& b : o.built)
        ChainWriter::capture (b, name);
    if (w.presets)
        for (const auto& b : o.built)
        {
            if (! b.product->hasUserPresets())
                continue;
            juce::String desc = "Made by Axell Signature Dojo from " + o.take.getFileName() + " on " + stamp + ".";
            for (const auto& s : o.stages)
                if (s.product == b.product)
                    for (auto& [param, text] : s.values)
                        desc << " " << param << " " << text << ".";
            auto xml = state::presetXml (*b.product, state::treeFromComponent (b.component), name, desc);
            if (xml == nullptr)
            {
                lines.add (b.product->name + ": the plugin's state could not be read.");
                continue;
            }
            lines.add (state::writePreset (paths, *b.product, *xml, name).message);
        }
    if (w.chain && ! o.built.empty())
    {
        ChainWriter cw (paths);
        lines.add (cw.write (name + " - Vocal Chain", cw.chainText (o.built)).message);
    }
    if (w.chain && w.tracking && ! o.built.empty())
    {
        // the tracking chain (D24): the same tuned plugins set for monitoring - Velvet's LIVE de-esser and Gate's LIVE
        // speed (no look-ahead), De-Reverb left out (it needs a look-ahead of its own); each plugin's latency read
        // after it was prepared in that setting. The mixing settings are restored afterwards.
        ChainWriter cw (paths);
        std::vector<const BuiltStage*> sel;
        std::vector<std::pair<BuiltStage*, std::pair<juce::String, juce::String>>> restore;   // stage, (param, text)
        int latency = 0;
        const double rate = o.sampleRate;
        juce::StringArray parts, missing;
        auto toLive = [&] (BuiltStage& b, const juce::String& param, const juce::String& text, const juce::String& what)
        {
            const auto was = b.plugin->text (param);
            if (b.plugin->set (param, text))
                restore.push_back ({ &b, { param, was } });
            else
                missing.add (b.product->name + " has no " + what + " (an older version is installed)");
        };
        for (auto& b : o.built)
        {
            if (b.product->key == "dereverb")
                continue;
            if (b.product->key == "velvet")
                toLive (b, "De-Ess Mode", "Live", "zero-latency de-esser");
            if (b.product->key == "gate")
                toLive (b, "Speed", "Live", "LIVE speed");
            juce::AudioBuffer<float> blip (2, 512);
            blip.clear();
            b.plugin->render (blip, nullptr);
            const int lat = juce::jmax (0, b.plugin->latency());
            latency += lat;
            parts.add (b.product->name + " " + juce::String (1000.0 * lat / rate, 1) + " ms");
            ChainWriter::capture (b, name + " Tracking");
            sel.push_back (&b);
        }
        auto res = cw.write (name + " - Tracking Chain", cw.chainText (sel));
        lines.add (res.message);
        if (res.ok)
            lines.add ("Tracking chain: " + juce::String (1000.0 * latency / rate, 1) + " ms of plugin latency (" + parts.joinIntoString (", ")
                       + "), plus your audio buffer (Workstation page). De-Reverb stays in the vocal chain for mixing."
                       + (missing.isEmpty() ? juce::String() : " " + missing.joinIntoString ("; ") + "."));
        for (auto& [b, pv] : restore)
            b->plugin->set (pv.first, pv.second);
        for (auto* b : sel)
            ChainWriter::capture (*const_cast<BuiltStage*> (b), name);
    }
    if (w.profile)
    {
        auto* obj = new juce::DynamicObject();
        const auto& p = o.profile;
        obj->setProperty ("name", name);
        obj->setProperty ("take", o.take.getFullPathName());
        obj->setProperty ("date", stamp);
        auto num = [] (float v) { return juce::var (std::round (v * 100.0) / 100.0); };
        obj->setProperty ("loudnessLufs", num (p.integratedLufs));
        obj->setProperty ("peakDbfs", num (p.peakDbfs));
        obj->setProperty ("noiseFloorDb", num (p.noiseFloorDb));
        obj->setProperty ("snrDb", num (p.snrDb));
        obj->setProperty ("f0MedianHz", num (p.f0Median));
        obj->setProperty ("f0LowHz", num (p.f0Low));
        obj->setProperty ("f0HighHz", num (p.f0High));
        obj->setProperty ("essShHz", num (p.shHz));
        obj->setProperty ("essSHz", num (p.sHz));
        obj->setProperty ("rt60s", num (p.rt60));
        obj->setProperty ("rumbleDb", num (p.rumbleDb));
        obj->setProperty ("processedTake", p.processedTake);
        juce::Array<juce::var> stages;
        for (const auto& s : o.stages)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("plugin", s.product != nullptr ? s.product->name : juce::String());
            so->setProperty ("program", s.program);
            auto* vals = new juce::DynamicObject();
            for (auto& [param, text] : s.values)
                vals->setProperty (param, text);
            so->setProperty ("settings", juce::var (vals));
            so->setProperty ("why", s.why.joinIntoString (" "));
            stages.add (juce::var (so));
        }
        obj->setProperty ("chain", stages);
        juce::Array<juce::var> notes;
        for (auto& n : o.plan.notes)
            notes.add (juce::String (n));
        obj->setProperty ("notes", notes);
        const auto f = paths.profiles().getChildFile (juce::File::createLegalFileName (name) + ".json");
        paths.profiles().createDirectory();
        lines.add (f.replaceWithText (juce::JSON::toString (juce::var (obj), false)) ? "Profile \"" + name + "\" saved."
                                                                                    : "Cannot write " + f.getFullPathName());
    }
    return lines;
}
} // namespace dojo
