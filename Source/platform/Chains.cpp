#include "Chains.h"
#include "StateTools.h"
#include "Process.h"
#include "SystemCheck.h"

#include <juce_cryptography/juce_cryptography.h>

namespace dojo
{
const std::vector<ChainTemplate>& ChainTemplate::all()
{
    using S = ChainStage;
    static const std::vector<ChainTemplate> list {
        { "rap-lead", "Rap Lead", "Vocals", "Room out, clean gaps, forward EQ, FET + opto, smooth esses with tape warmth.",
          { S { "dereverb", "Natural", {} }, S { "gate", "Rap Lead - Clean Gaps", {} }, S { "penta", "Rap Lead - Clean & Forward", {} },
            S { "duo", "Rap Lead", {} }, S { "velvet", "Rap Lead - Upfront", {} } } },
        { "boom-bap-lead", "Boom Bap Lead", "Vocals", "Warm, gritty and upfront: the boom bap lead sound.",
          { S { "dereverb", "Natural", {} }, S { "gate", "Rap Lead - Clean Gaps", {} }, S { "penta", "Boom Bap Lead - Warm Grit", {} },
            S { "duo", "Vocal: Upfront Hip-Hop", {} }, S { "velvet", "Boom Bap Grit", {} } } },
        { "tracking", "Tracking (Live)", "Vocals", "Record while you hear yourself through it: about 3 ms of plugin latency. "
          "De-Reverb LIVE (no latency), Gate at LIVE speed, forward EQ, FET + opto with VOICE HOLD, Velvet's zero-latency de-esser.",
          { S { "dereverb", "Natural", { { "Mode", "Live" } } }, S { "gate", "Rap Lead - Clean Gaps", { { "Speed", "Live" } } },
            S { "penta", "Rap Lead - Clean & Forward", {} },
            S { "duo", "Rap Lead", {} }, S { "velvet", "Tracking - Warm Monitor", {} } } },
        { "doubles", "Doubles", "Vocals", "Tucks doubles behind the lead: tighter gate, thinner low end, more control.",
          { S { "gate", "Ad-Libs & Doubles", {} }, S { "penta", "Doubles - Tuck Behind", {} }, S { "duo", "Boom Bap Double", {} },
            S { "velvet", "Doubles & Stacks", {} } } },
        { "ad-libs", "Ad-libs", "Vocals", "Hype and cut: ad-libs that poke through without fighting the lead.",
          { S { "gate", "Ad-Libs & Doubles", {} }, S { "penta", "Ad-libs - Hype Cut", {} }, S { "duo", "Vocal: Ad-libs & Hype", {} },
            S { "velvet", "Ad-Libs Thick", {} } } },
        { "sung-hook", "Sung Hook", "Vocals", "For melodic hooks: natural gate, lift, smooth levelling, silky top.",
          { S { "dereverb", "Natural", {} }, S { "gate", "Natural Singing", {} }, S { "penta", "Chorus / Hook Lift", {} },
            S { "duo", "Vocal: Sung Hook", {} }, S { "velvet", "R&B Silk", {} } } },
        { "beat-bus", "Beat Bus", "Beats", "Glue for the whole beat: polish, gentle bus compression, peak control.",
          { S { "penta", "Beat Bus - Glue Polish", {} }, S { "duo", "Mix Bus: Glue", {} }, S { "master", "Sample-Based Beat Glue", {} } } },
        { "drum-bus", "Drum Bus", "Beats", "Boom bap drums with thump and knock.",
          { S { "penta", "Drum Bus - Boom Bap Thump", {} }, S { "duo", "Drum Bus: Boom Bap Smash", {} },
            S { "master", "Boom Bap Drum Bus Knock", {} } } },
        { "master-streaming", "Master - Streaming", "Master", "Warm boom bap master at -14 LUFS, safe on every streaming service.",
          { S { "penta", "Master - Boom Bap Warmth", {} }, S { "duo", "Master: Transparent Leveling", {} },
            S { "master", "Boom Bap Streaming Master (-14)", {} } } },
        { "master-loud", "Master - Loud", "Master", "Loud boom bap master at -9 LUFS that stays clean after streaming codecs.",
          { S { "penta", "Master - Loudness Prep", {} }, S { "duo", "Master: Peak Catch", {} },
            S { "master", "Boom Bap Loud, Codec-Ready (-9)", {} } } },
        { "space-plate-send", "Vocal Plate (send)", "Vocal FX", "For a send / aux track: Axell Space's vocal plate, 100 % wet, "
          "the esses kept out of the tail and the reverb stepping back while you rap.",
          { S { "space", "Send: Vocal Plate (100 % wet)", {} } } },
        { "space-throw-send", "Delay Throw (send)", "Vocal FX", "For a send / aux track: a tempo-synced 1/4 delay, 100 % wet, "
          "darker and thinner with every repeat, the esses kept out.",
          { S { "space", "Send: 1/4 Delay (100 % wet)", {} } } },
    };
    return list;
}

std::vector<std::pair<juce::String, juce::String>> currentFeatureSettings (const juce::String& key, const HostedPlugin& p,
                                                                            const juce::ValueTree& saved, bool vocalChain)
{
    std::vector<std::pair<juce::String, juce::String>> out;
    // the saved state already knew this parameter (it holds a value for it)
    auto knew = [&saved] (const char* id) { return saved.getChildWithProperty ("id", id).isValid() || saved.hasProperty (id); };
    auto gap = [&] (const char* id, const char* param) { return ! knew (id) && p.hasParameter (param); };
    if (key == "dereverb" && gap ("trueTone", "True Tone"))
        out.push_back ({ "True Tone", "On" });
    else if (key == "gate" && gap ("pitchHold", "Pitch Hold"))
        out.push_back ({ "Pitch Hold", "On" });
    else if (key == "duo" || key == "master")
    {
        juce::String voice = p.hasParameter ("Voice Hold") ? p.text ("Voice Hold") : juce::String();
        if (vocalChain && gap ("voiceHold", "Voice Hold"))
            out.push_back ({ "Voice Hold", voice = "On" });
        if (gap ("essHold", "Ess Hold") && voice.isNotEmpty())
            out.push_back ({ "Ess Hold", vocalChain ? juce::String ("On") : voice });
    }
    return out;
}

std::vector<BuiltStage> ChainWriter::build (const ChainTemplate& t, juce::StringArray& problems) const
{
    std::vector<BuiltStage> out;
    for (const auto& s : t.stages)
    {
        BuiltStage b;
        b.product = Catalog::find (s.product);
        if (b.product == nullptr)
        {
            problems.add ("Unknown plugin " + s.product);
            continue;
        }
        const auto bundle = paths.vst3.getChildFile (b.product->bundle);
        if (! bundle.isDirectory())
        {
            problems.add (b.product->name + " is not installed.");
            continue;
        }
        juce::String err;
        b.plugin = HostedPlugin::load (bundle, 48000.0, 512, err);
        if (b.plugin == nullptr)
        {
            problems.add (b.product->name + ": " + err);
            continue;
        }
        if (s.program.isNotEmpty() && ! b.plugin->selectProgram (s.program))
            problems.add (b.product->name + " has no program \"" + s.program + "\" (installed version too old?)");
        for (const auto& [param, text] : s.settings)
        {
            juce::String why;
            if (! b.plugin->set (param, text, &why))
                problems.add (b.product->name + ": " + why);
        }
        capture (b, s.program.isNotEmpty() ? s.program : b.plugin->programName());
        out.push_back (std::move (b));
    }
    return out;
}

void ChainWriter::capture (BuiltStage& b, const juce::String& presetName)
{
    const auto st = b.plugin->vst3State();
    auto renamed = state::withPresetName (st.component, presetName);
    b.component = renamed.empty() ? st.component : std::move (renamed);
    b.controller = st.controller;
    b.presetName = presetName;
}

juce::String ChainWriter::chainText (const std::vector<BuiltStage>& stages) const
{
    std::vector<const BuiltStage*> all;
    for (const auto& b : stages)
        all.push_back (&b);
    return chainText (all);
}

juce::String ChainWriter::chainText (const std::vector<const BuiltStage*>& stages) const
{
    std::vector<reaper::ChainEntry> entries;
    for (const auto* bp : stages)
    {
        const auto& b = *bp;
        reaper::ChainEntry e;
        e.plugin.name = (b.product->name + " (" + Catalog::kVendor + ")").toStdString();
        e.plugin.file = b.product->bundle.toStdString();
        e.plugin.uid = state::reaperUid (paths, *b.product, b.plugin->description());
        e.plugin.classId = b.product->classId().toStdString();
        e.plugin.inputs = b.plugin->mainInputs() + b.plugin->sideInputs();
        e.plugin.outputs = b.plugin->mainOutputs();
        e.plugin.component = b.component;
        e.plugin.controller = b.controller;
        e.plugin.program = b.presetName.toStdString();
        e.fxid = state::newFxId().toStdString();
        entries.push_back (std::move (e));
    }
    return juce::String (reaper::fxChain (entries));
}

bool ChainWriter::wroteBefore (const juce::File& f) const
{
    auto xml = juce::XmlDocument::parse (ledger());
    if (xml == nullptr || ! f.existsAsFile())
        return false;
    const auto hash = juce::SHA256 (f).toHexString().removeCharacters (" ");
    for (auto* e : xml->getChildIterator())
        if (e->getStringAttribute ("path") == f.getFullPathName() && e->getStringAttribute ("sha256") == hash)
            return true;
    return false;
}

void ChainWriter::remember (const juce::File& f) const
{
    auto xml = juce::XmlDocument::parse (ledger());
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("WRITTEN");
    for (auto* e = xml->getFirstChildElement(); e != nullptr;)
    {
        auto* next = e->getNextElement();
        if (e->getStringAttribute ("path") == f.getFullPathName())
            xml->removeChildElement (e, true);
        e = next;
    }
    auto* e = xml->createNewChildElement ("FILE");
    e->setAttribute ("path", f.getFullPathName());
    e->setAttribute ("sha256", juce::SHA256 (f).toHexString().removeCharacters (" "));
    ledger().getParentDirectory().createDirectory();
    xml->writeTo (ledger());
}

ChainWriter::Written ChainWriter::write (const juce::String& fileName, const juce::String& text, bool replaceUnlisted) const
{
    Written w;
    const auto folder = paths.reaperChains();
    if (! folder.createDirectory())
    {
        w.message = "Cannot write to " + folder.getFullPathName();
        return w;
    }
    w.file = folder.getChildFile (juce::File::createLegalFileName (fileName) + ".RfxChain");
    if (w.file.existsAsFile() && ! replaceUnlisted && ! wroteBefore (w.file))
    {
        w.message = "\"" + w.file.getFileName() + "\" exists and was not written by the Dojo (or was edited since); it was left as it is.";
        return w;
    }
    const auto tmp = w.file.getSiblingFile (w.file.getFileName() + ".tmp");
    if (! tmp.replaceWithText (text, false, false, "\n") || ! tmp.moveFileTo (w.file))
    {
        tmp.deleteFile();
        w.message = "Cannot write " + w.file.getFullPathName();
        return w;
    }
    remember (w.file);
    w.ok = true;
    w.message = "REAPER chain \"" + w.file.getFileNameWithoutExtension() + "\" saved.";
    return w;
}

juce::String ChainWriter::projectText (const juce::String& trackName, const juce::String& chainText)
{
    juce::String t;
    t << "<REAPER_PROJECT 0.1 \"7.0/linux-x86_64\" " << juce::String (juce::Time::currentTimeMillis() / 1000) << "\n"
      << "  <TRACK\n"
      << "    NAME " << reaper::quote (trackName.toStdString()) << "\n"
      << "    REC 1 0 0 0 0 0 0 0\n"   // armed, input 1 (mono), monitoring off: no feedback through speakers
      << "    <FXCHAIN\n"
      << "      SHOW 0\n      LASTSEL 0\n      DOCKED 0\n";
    for (auto line : juce::StringArray::fromLines (chainText))
        if (line.isNotEmpty())
            t << "      " << line << "\n";
    t << "    >\n  >\n>\n";
    return t;
}

StepResult ChainWriter::openInReaper (const juce::File& chainFile, const juce::String& trackName, juce::File* projectOut) const
{
    if (! chainFile.existsAsFile())
        return { false, "The chain has not been saved yet." };
    const auto folder = paths.home.getChildFile ("Documents/Axell Signature");
    if (! folder.createDirectory())
        return { false, "Cannot write to " + folder.getFullPathName() };
    const auto base = juce::File::createLegalFileName (trackName + " Session");
    auto project = folder.getChildFile (base + ".RPP");
    for (int i = 2; project.exists(); ++i)
        project = folder.getChildFile (base + " " + juce::String (i) + ".RPP");
    if (! project.replaceWithText (projectText (trackName, chainFile.loadFileAsString()), false, false, "\n"))
        return { false, "Cannot write " + project.getFullPathName() };
    if (projectOut != nullptr)
        *projectOut = project;
    if (paths.isSandbox)
        return { true, "Sandbox: project written, REAPER not opened (" + project.getFileName() + ")." };
    const auto reaperApp = SystemCheck::findReaper (paths);
    if (reaperApp == juce::File())
        return { false, "REAPER was not found. The project is ready to open: " + project.getFullPathName() };
    // started in its own session so it keeps running after the Dojo closes
    const auto r = Process::run ({ "setsid", "-f", reaperApp.getFullPathName(), project.getFullPathName() }, 10000);
    if (! r.ok())
        return { false, "REAPER could not be started. The project is ready to open: " + project.getFullPathName() };
    return { true, "REAPER opens \"" + project.getFileName() + "\": the track \"" + trackName + "\" is armed with your chain." };
}
} // namespace dojo

