#include "ProfileEngine.h"
#include "StateTools.h"

#include <axell/dsp/Loudness.h>

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

ProfileOutcome ProfileEngine::run (const juce::File& take, bool tune, const Progress& progress, const std::atomic<bool>* cancel) const
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
        full = std::move (rendered);
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
