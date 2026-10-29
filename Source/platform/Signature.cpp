#include "Signature.h"
#include "Process.h"
#include "ProfileEngine.h"
#include "StateTools.h"
#include "SystemCheck.h"
#include "Tools.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace dojo
{
std::vector<Signature> Signature::all (const Paths& paths)
{
    std::vector<Signature> out;
    for (const auto& f : folder (paths).findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("SIGNATURE"))
            continue;
        Signature s;
        s.file = f;
        s.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
        s.description = xml->getStringAttribute ("description");
        s.vocalChain = paths.reaperChains().getChildFile (xml->getStringAttribute ("vocalChain"));
        s.masterChain = paths.reaperChains().getChildFile (xml->getStringAttribute ("masterChain"));
        s.referenceLufs = (float) xml->getDoubleAttribute ("referenceLufs", -70.0);
        s.referenceTake = xml->getStringAttribute ("referenceTake");
        for (auto* t : xml->getChildWithTagNameIterator ("TARGET"))
            s.targets.push_back ({ (float) t->getDoubleAttribute ("lufs", -14.0), (float) t->getDoubleAttribute ("masterInput", 0.0) });
        if (s.usable())
            out.push_back (std::move (s));
    }
    std::sort (out.begin(), out.end(), [] (const Signature& a, const Signature& b) { return a.name < b.name; });
    return out;
}

float signatureGainDb (const Signature& s, float takeLufs) { return juce::jlimit (-30.0f, 40.0f, s.referenceLufs - takeLufs); }

juce::String signatureMasterChain (const Paths& paths, const Signature& s, const Signature::Target& t, juce::String& error)
{
    auto chain = tools::loadChain (paths, s.masterChain, 48000.0);
    if (! chain.ok())
    {
        error = chain.problems.joinIntoString ("; ");
        return {};
    }
    bool found = false;
    for (auto& b : chain.stages)
        if (b.product->key == "master")
        {
            const auto text = (t.masterInputDb > 0.0f ? "+" : "") + juce::String (t.masterInputDb, 1) + " dB";
            juce::String why;
            if (! b.plugin->set ("Input", text, &why))
            {
                error = "Axell Master: " + why;
                return {};
            }
            ChainWriter::capture (b, b.presetName.isNotEmpty() ? b.presetName : b.plugin->programName());
            found = true;
        }
    if (! found)
    {
        error = "The master chain holds no Axell Master to set the loudness with.";
        return {};
    }
    return ChainWriter (paths).chainText (chain.stages);   // the other stages: their saved state, byte for byte
}

juce::String signatureProjectText (const Signature& s, const juce::File& take, double seconds, float gainDb,
                                   const juce::String& vocalChain, const juce::String& masterChain)
{
    const auto ext = take.getFileExtension().toLowerCase();
    const juce::String source = ext == ".flac" ? "FLAC" : ext == ".mp3" ? "MP3" : ext == ".ogg" ? "VORBIS" : "WAVE";
    auto indent = [] (const juce::String& text, const juce::String& pad)
    {
        juce::String o;
        for (auto line : juce::StringArray::fromLines (text))
            if (line.isNotEmpty())
                o << pad << line << "\n";
        return o;
    };
    auto q = [] (const juce::String& v) { return juce::String (reaper::quote (v.toStdString())); };
    juce::String t;
    t << "<REAPER_PROJECT 0.1 \"7.0/linux-x86_64\" " << juce::String (juce::Time::currentTimeMillis() / 1000) << "\n"
      << "  MASTER_NCH 2 2\n"
      << "  <MASTERFXLIST\n    SHOW 0\n    LASTSEL 0\n    DOCKED 0\n"
      << indent (masterChain, "    ") << "  >\n"
      << "  <TRACK\n"
      << "    NAME " << q ("Vocal - " + s.name) << "\n"
      << "    <FXCHAIN\n      SHOW 0\n      LASTSEL 0\n      DOCKED 0\n"
      << indent (vocalChain, "      ") << "    >\n"
      << "    <ITEM\n"
      << "      POSITION 0\n"
      << "      LENGTH " << juce::String (seconds, 6) << "\n"
      << "      NAME " << q (take.getFileName()) << "\n"
      // the take's volume: the gain match, before the track's FX
      << "      VOLPAN " << juce::String (juce::Decibels::decibelsToGain (gainDb), 6) << " 0 1 -1\n"
      << "      <SOURCE " << source << "\n"
      << "        FILE " << q (take.getFullPathName()) << "\n"
      << "      >\n"
      << "    >\n"
      << "  >\n>\n";
    return t;
}

SignatureSession makeSignatureSession (const Paths& paths, const Signature& s, const juce::File& take, float targetLufs, bool open)
{
    SignatureSession r;
    const Signature::Target* target = nullptr;
    for (const auto& t : s.targets)
        if (std::abs (t.lufs - targetLufs) < 0.05f)
            target = &t;
    if (target == nullptr)
    {
        r.message = s.name + " has no " + juce::String (targetLufs, 1) + " LUFS target.";
        return r;
    }
    const auto ext = take.getFileExtension().toLowerCase();
    if (! take.existsAsFile() || ! juce::StringArray { ".wav", ".flac", ".mp3", ".ogg" }.contains (ext))
    {
        r.message = "Choose a WAV, FLAC, MP3 or Ogg take.";
        return r;
    }
    // the take's level, measured as the reference was (BS.1770 over up to 10 minutes; mono counted as dual mono)
    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    juce::String err;
    if (! ProfileEngine::loadTake (take, audio, rate, err, 600.0))
    {
        r.message = err;
        return r;
    }
    double seconds = audio.getNumSamples() / rate;
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (take)); rd != nullptr && rd->sampleRate > 0.0)
            seconds = (double) rd->lengthInSamples / rd->sampleRate;
    }
    r.takeLufs = ProfileEngine::lufs (audio, rate);
    audio.setSize (0, 0);
    if (r.takeLufs < -60.0f)
    {
        r.message = "The take is silent or almost silent (" + juce::String (r.takeLufs, 1) + " LUFS).";
        return r;
    }
    r.gainDb = signatureGainDb (s, r.takeLufs);
    const auto master = signatureMasterChain (paths, s, *target, err);
    if (master.isEmpty())
    {
        r.message = err;
        return r;
    }
    const auto folder = paths.home.getChildFile ("Documents/Axell Signature");
    if (! folder.createDirectory())
    {
        r.message = "Cannot write to " + folder.getFullPathName();
        return r;
    }
    const auto base = juce::File::createLegalFileName (take.getFileNameWithoutExtension() + " - " + s.name + " ("
                                                       + juce::String (juce::roundToInt (target->lufs)) + " LUFS)");
    r.project = folder.getChildFile (base + ".RPP");
    for (int i = 2; r.project.exists(); ++i)
        r.project = folder.getChildFile (base + " " + juce::String (i) + ".RPP");
    const auto text = signatureProjectText (s, take, seconds, r.gainDb, s.vocalChain.loadFileAsString(), master);
    if (! r.project.replaceWithText (text, false, false, "\n"))
    {
        r.message = "Cannot write " + r.project.getFullPathName();
        return r;
    }
    const auto how = "take at " + juce::String (r.takeLufs, 1) + " LUFS, matched by " + (r.gainDb >= 0.0f ? "+" : "")
                     + juce::String (r.gainDb, 1) + " dB to " + s.name + "'s level; master at " + juce::String (juce::roundToInt (target->lufs)) + " LUFS";
    r.ok = true;
    if (! open || paths.isSandbox)
    {
        r.message = "Session written: " + r.project.getFileName() + " (" + how + ").";
        return r;
    }
    const auto reaperApp = SystemCheck::findReaper (paths);
    if (reaperApp == juce::File())
    {
        r.message = "REAPER was not found. The session is ready to open: " + r.project.getFullPathName() + " (" + how + ").";
        return r;
    }
    const auto run = Process::run ({ "setsid", "-f", reaperApp.getFullPathName(), r.project.getFullPathName() }, 10000);
    r.message = run.ok() ? "REAPER opens \"" + r.project.getFileName() + "\" (" + how + ")."
                         : "REAPER could not be started. The session is ready to open: " + r.project.getFullPathName();
    return r;
}
} // namespace dojo
