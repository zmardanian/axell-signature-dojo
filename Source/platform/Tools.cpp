#include "Tools.h"
#include "StateTools.h"

#include <juce_cryptography/juce_cryptography.h>

namespace dojo::tools
{
//== chain loading ===============================================================================================
LoadedChain loadChain (const Paths& paths, const juce::File& rfx, double rate)
{
    LoadedChain out;
    if (! rfx.existsAsFile())
    {
        out.problems.add ("No chain file at " + rfx.getFullPathName());
        return out;
    }
    juce::StringArray lines;
    rfx.readLines (lines);
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto line = lines[i].trim();
        if (! line.startsWith ("<VST "))
            continue;
        std::vector<std::string> body;
        for (++i; i < lines.size() && lines[i].trim() != ">"; ++i)
            body.push_back (lines[i].toStdString());
        // the plugin by its VST3 class id ({...} in the header line), so a renamed file still matches
        const auto cid = line.fromFirstOccurrenceOf ("{", false, false).upToFirstOccurrenceOf ("}", false, false);
        const Product* prod = nullptr;
        for (const auto& p : Catalog::products())
            if (p.classId() == cid)
                prod = &p;
        if (prod == nullptr)
        {
            out.problems.add ("Not an Axell plugin: " + line.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false));
            continue;
        }
        const auto parsed = reaper::parseBlock (body);
        if (! parsed.ok)
        {
            out.problems.add (prod->name + ": its saved state is damaged.");
            continue;
        }
        BuiltStage b;
        b.product = prod;
        juce::String err;
        b.plugin = HostedPlugin::load (paths.vst3.getChildFile (prod->bundle), rate, 512, err);
        if (b.plugin == nullptr)
        {
            out.problems.add (prod->name + " is not installed.");
            continue;
        }
        b.plugin->setVst3State ({ parsed.component, parsed.controller });
        b.component = parsed.component;
        b.controller = parsed.controller;
        b.presetName = juce::String (parsed.program);
        out.stages.push_back (std::move (b));
    }
    if (out.stages.empty() && out.problems.isEmpty())
        out.problems.add ("The chain holds no plugins.");
    return out;
}

//== updating saved chains =======================================================================================
namespace
{
ChainUpdate updateOne (const Paths& paths, const juce::File& file, bool named, const juce::File& backupDir)
{
    ChainUpdate u;
    u.file = file;
    ChainWriter cw (paths);
    const auto name = file.getFileNameWithoutExtension();
    std::vector<BuiltStage> stages;
    const ChainTemplate* tmpl = nullptr;
    for (const auto& t : ChainTemplate::all())
        if (juce::File::createLegalFileName (t.name) == name)
            tmpl = &t;
    if (tmpl != nullptr)
    {
        juce::StringArray problems;
        stages = cw.build (*tmpl, problems);
        if (! problems.isEmpty() || stages.size() != tmpl->stages.size())
        {
            u.message = problems.joinIntoString ("; ");
            return u;
        }
        // the file already holds exactly these states: nothing to do
        auto existing = loadChain (paths, file, 48000.0);
        bool same = existing.ok() && existing.stages.size() == stages.size();
        for (size_t i = 0; same && i < stages.size(); ++i)
            same = existing.stages[i].component == stages[i].component && existing.stages[i].presetName == stages[i].presetName;
        if (same)
        {
            u.ok = true;
            u.message = "already current";
            return u;
        }
        u.changes.add ("rebuilt from the template with the installed plugins' presets");
    }
    else
    {
        auto loaded = loadChain (paths, file, 48000.0);
        if (! loaded.ok())
        {
            u.message = loaded.problems.joinIntoString ("; ");
            return u;
        }
        bool vocal = false;
        for (const auto& b : loaded.stages)
            vocal |= b.product->key == "dereverb" || b.product->key == "gate" || b.product->key == "velvet";
        for (auto& b : loaded.stages)
        {
            const auto saved = state::treeFromComponent (b.component);
            for (const auto& [param, text] : currentFeatureSettings (b.product->key, *b.plugin, saved, vocal))
                if (b.plugin->text (param) != text)
                {
                    juce::String why;
                    if (! b.plugin->set (param, text, &why))
                    {
                        u.message = b.product->name + ": " + why;
                        return u;
                    }
                    u.changes.add (b.product->name + ": " + param + " " + text);
                }
            ChainWriter::capture (b, b.presetName.isNotEmpty() ? b.presetName : b.plugin->programName());
        }
        if (u.changes.isEmpty())   // nothing to gain: the file stays exactly as it is
        {
            u.ok = true;
            u.message = "already current";
            return u;
        }
        stages = std::move (loaded.stages);
    }
    if (! backupDir.createDirectory() || ! file.copyFileTo (backupDir.getChildFile (file.getFileName())))
    {
        u.message = "Cannot back up " + file.getFileName() + " into " + backupDir.getFullPathName();
        return u;
    }
    const auto w = cw.write (name, cw.chainText (stages), named);
    u.ok = w.ok;
    u.changed = w.ok;
    u.message = w.ok ? "updated (the old file is in " + backupDir.getFullPathName() + ")" : w.message;
    return u;
}
} // namespace

std::vector<ChainUpdate> updateSavedChains (const Paths& paths, const juce::Array<juce::File>& files)
{
    std::vector<ChainUpdate> out;
    const auto backupDir = paths.dojoData().getChildFile ("chain-backups").getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"));
    ChainWriter cw (paths);
    if (files.isEmpty())
    {
        for (const auto& f : paths.reaperChains().findChildFiles (juce::File::findFiles, false, "*.RfxChain"))
            if (cw.wroteBefore (f))
                out.push_back (updateOne (paths, f, false, backupDir));
    }
    else
        for (const auto& f : files)
        {
            if (! f.isAChildOf (paths.reaperChains()))
            {
                out.push_back ({ f, false, false, "not in REAPER's FX chain folder (" + paths.reaperChains().getFullPathName() + ")", {} });
                continue;
            }
            out.push_back (updateOne (paths, f, true, backupDir));
        }
    return out;
}

//== Batch Render ================================================================================================
std::vector<RenderedFile> batchRender (const Paths& paths, const juce::File& rfx, const juce::Array<juce::File>& files,
                                       const juce::File& outFolder, const Progress& progress, const std::atomic<bool>* cancel)
{
    std::vector<RenderedFile> results;
    if (! outFolder.createDirectory())
    {
        results.push_back ({ {}, {}, false, "Cannot write to " + outFolder.getFullPathName() });
        return results;
    }
    const auto chainName = rfx.getFileNameWithoutExtension();
    double loadedRate = 0.0;
    LoadedChain chain;
    for (int fi = 0; fi < files.size(); ++fi)
    {
        if (cancel != nullptr && cancel->load())
            break;
        RenderedFile r;
        r.source = files[fi];
        if (progress)
            progress ((float) fi / (float) files.size(), "Rendering " + r.source.getFileName());
        juce::AudioBuffer<float> audio;
        double rate = 48000.0;
        juce::String err;
        if (! ProfileEngine::loadTake (r.source, audio, rate, err, 3600.0))
        {
            r.message = err;
            results.push_back (std::move (r));
            continue;
        }
        if (std::abs (rate - loadedRate) > 0.5)   // plugins are prepared at the file's own rate
        {
            chain = loadChain (paths, rfx, rate);
            loadedRate = rate;
        }
        if (! chain.ok())
        {
            r.message = chain.problems.joinIntoString (" ");
            results.push_back (std::move (r));
            continue;
        }
        r.inLufs = ProfileEngine::lufs (audio, rate);
        for (auto& st : chain.stages)
            audio = st.plugin->render (audio, cancel);
        if (cancel != nullptr && cancel->load())
            break;
        r.outLufs = ProfileEngine::lufs (audio, rate);
        const float* p[2] = { audio.getReadPointer (0), audio.getReadPointer (1) };
        r.outTruePeakDbtp = streaming::measure (p, 2, (size_t) audio.getNumSamples(), rate).truePeakDbtp;
        const auto base = juce::File::createLegalFileName (r.source.getFileNameWithoutExtension() + " - " + chainName);
        r.output = outFolder.getChildFile (base + ".wav");
        for (int i = 2; r.output.exists(); ++i)
            r.output = outFolder.getChildFile (base + " " + juce::String (i) + ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (r.output.createOutputStream());
        auto writer = os != nullptr ? wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24))
                                    : nullptr;
        r.ok = writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer.reset();
        r.message = r.ok ? juce::String ("Rendered") + (r.outTruePeakDbtp > 0.0f ? ": its peaks go over 0 dBFS, lower the chain's output" : ".")
                         : "Cannot write " + r.output.getFullPathName();
        results.push_back (std::move (r));
    }
    if (progress)
        progress (1.0f, "Done");
    return results;
}

//== Streaming Preview ===========================================================================================
MasterPreview previewMaster (const juce::File& f)
{
    MasterPreview m;
    if (! ProfileEngine::loadTake (f, m.audio, m.sampleRate, m.error, 1800.0))
        return m;
    const float* p[2] = { m.audio.getReadPointer (0), m.audio.getReadPointer (1) };
    m.measurement = streaming::measure (p, 2, (size_t) m.audio.getNumSamples(), m.sampleRate);
    m.services = streaming::playback (m.measurement);
    m.ok = true;
    return m;
}

//== Backup & Move ===============================================================================================
std::vector<BackupItem> backupItems (const Paths& paths)
{
    std::vector<BackupItem> items;
    for (const auto& p : Catalog::products())
        if (p.hasUserPresets())
            for (const auto& f : paths.presetFolder (p.presetFolder).findChildFiles (juce::File::findFiles, false, "*" + p.presetExt))
                items.push_back ({ f, "presets/" + p.presetFolder + "/" + f.getFileName() });
    for (const auto& f : paths.profiles().findChildFiles (juce::File::findFiles, false, "*.json"))
        items.push_back ({ f, "profiles/" + f.getFileName() });
    for (const auto& f : paths.reaperChains().findChildFiles (juce::File::findFiles, false, "*.RfxChain"))
        items.push_back ({ f, "chains/" + f.getFileName() });
    return items;
}

namespace
{
juce::String hashOf (const juce::File& f) { return juce::SHA256 (f).toHexString().removeCharacters (" ").toLowerCase(); }

// where an item lives on this computer, from its relative path; {} for anything unexpected
juce::File targetFor (const Paths& paths, const juce::String& rel)
{
    if (rel.contains ("..") || rel.startsWithChar ('/') || rel.containsChar ('\\'))
        return {};
    const auto parts = juce::StringArray::fromTokens (rel, "/", "");
    if (parts.size() == 3 && parts[0] == "presets")
        for (const auto& p : Catalog::products())
            if (p.presetFolder == parts[1] && parts[2].endsWith (p.presetExt))
                return paths.presetFolder (p.presetFolder).getChildFile (parts[2]);
    if (parts.size() == 2 && parts[0] == "profiles" && parts[1].endsWith (".json"))
        return paths.profiles().getChildFile (parts[1]);
    if (parts.size() == 2 && parts[0] == "chains" && parts[1].endsWith (".RfxChain"))
        return paths.reaperChains().getChildFile (parts[1]);
    return {};
}
} // namespace

StepResult exportBackup (const Paths& paths, const juce::File& destination, juce::File* folderOut)
{
    const auto items = backupItems (paths);
    if (items.empty())
        return { false, "There is nothing to back up yet: no presets, profiles or chains." };
    auto folder = destination.getChildFile ("Axell Signature Backup " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"));
    for (int i = 2; folder.exists(); ++i)
        folder = destination.getChildFile ("Axell Signature Backup " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M") + " " + juce::String (i));
    if (! folder.createDirectory())
        return { false, "Cannot write to " + destination.getFullPathName() };
    juce::String manifest = "# Axell Signature backup: SHA-256 and path of every file\n";
    for (const auto& it : items)
    {
        const auto to = folder.getChildFile (it.relative);
        to.getParentDirectory().createDirectory();
        if (! it.file.copyFileTo (to))
            return { false, "Cannot copy " + it.file.getFileName() };
        manifest << hashOf (to) << "  " << it.relative << "\n";
    }
    if (! folder.getChildFile ("manifest.txt").replaceWithText (manifest, false, false, "\n"))
        return { false, "Cannot write the manifest." };
    if (folderOut != nullptr)
        *folderOut = folder;
    return { true, juce::String ((int) items.size()) + " files backed up to " + folder.getFullPathName() + "." };
}

StepResult importBackup (const Paths& paths, const juce::File& folder)
{
    juce::StringArray lines;
    folder.getChildFile ("manifest.txt").readLines (lines);
    int copied = 0, kept = 0, damaged = 0, unknown = 0;
    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;
        const auto hash = line.upToFirstOccurrenceOf (" ", false, false);
        const auto rel = line.fromFirstOccurrenceOf (" ", false, false).trimStart();
        const auto target = targetFor (paths, rel);
        const auto source = folder.getChildFile (rel);
        if (target == juce::File() || ! source.isAChildOf (folder))
        {
            ++unknown;
            continue;
        }
        if (! source.existsAsFile() || hashOf (source) != hash)
        {
            ++damaged;
            continue;
        }
        if (target.exists())
        {
            ++kept;
            continue;
        }
        target.getParentDirectory().createDirectory();
        copied += source.copyFileTo (target) ? 1 : 0;
    }
    if (copied + kept + damaged + unknown == 0)
        return { false, "This folder is not an Axell Signature backup (no manifest.txt)." };
    juce::String msg = juce::String (copied) + " files restored";
    if (kept > 0) msg << ", " << kept << " already here (left as they are)";
    if (damaged > 0) msg << ", " << damaged << " damaged (skipped)";
    if (unknown > 0) msg << ", " << unknown << " not recognised (skipped)";
    return { damaged == 0, msg + "." };
}
} // namespace dojo::tools
