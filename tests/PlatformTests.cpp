// Axell Signature Dojo - platform tests (JUCE). Everything that writes goes to a fresh sandbox folder; the user's
// installed plugins are only read (loaded and rendered offline), never changed.
// Run from the repository root: build/DojoPlatformTests_artefacts/Release/DojoPlatformTests
#include "platform/Catalog.h"
#include "platform/Chains.h"
#include "platform/ProfileEngine.h"
#include "platform/Paths.h"
#include "platform/PluginHost.h"
#include "platform/Process.h"
#include "platform/StateTools.h"
#include "platform/SystemCheck.h"
#include "platform/Tools.h"
#include "WavIO.h"
#include "platform/Suite.h"

#include <cstdio>

using namespace dojo;

static int passed = 0, failed = 0;
static void check (bool ok, const juce::String& what)
{
    (ok ? passed : failed)++;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
}

static juce::File sandboxRoot;

//== processes ===================================================================================================
static void testProcess()
{
    check (Process::run ({ "true" }).ok() && ! Process::run ({ "false" }).ok() && Process::run ({ "false" }).exitCode == 1,
           "process: exit codes");
    const auto r = Process::run ({ "printf", "%s|", "a b", "c;$(echo x)", "'q'" });
    check (r.ok() && r.output == "a b|c;$(echo x)|'q'|", "process: arguments reach the program untouched (no shell): " + r.output);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto s = Process::run ({ "sleep", "5" }, 300);
    check (s.timedOut && juce::Time::getMillisecondCounterHiRes() - t0 < 2000, "process: timeout stops a hanging program");
    std::atomic<bool> cancel { true };
    check (Process::run ({ "sleep", "5" }, 10000, &cancel).timedOut, "process: cancel stops it");
    check (! Process::run ({ "no-such-program-dojo" }).started, "process: a missing program is reported, not run");
    check (! Process::run ({ "./relative-thing" }).started, "process: relative program paths are refused");
    const auto big = Process::run ({ "seq", "1", "60000" });
    check (big.ok() && big.output.length() > 300000 && big.output.endsWith ("60000\n"),
           "process: output larger than a pipe buffer does not deadlock (" + juce::String (big.output.length()) + " chars)");
}

//== suite: install / update / rollback / remove, in a sandbox ===================================================
static juce::File releaseFolder (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (name);
}

static void testSuite()
{
    const auto paths = Paths::sandbox (sandboxRoot.getChildFile ("suite"));
    Suite suite (paths);
    const auto& gate = *Catalog::find ("gate");
    check (paths.vst3.isAChildOf (sandboxRoot) && paths.reaperConfig().isAChildOf (sandboxRoot), "paths: the sandbox holds every folder");
    check (gate.classId() == "ABCDEF019182FAEB4178796247746531" && Catalog::find ("duo")->classId() == "ABCDEF019182FAEB44636D7044766331",
           "catalog: VST3 class ids as REAPER lists them");

    const auto src = releaseFolder ("axell-gate-release");
    if (! src.isDirectory())
    {
        std::printf ("skip  no local Axell Gate release folder\n");
        return;
    }
    // a "USB stick": a copy of the release folder inside the sandbox
    const auto usb = sandboxRoot.getChildFile ("usb");
    usb.createDirectory();
    for (auto f : src.findChildFiles (juce::File::findFiles, false, "*.tar.gz"))
        f.copyFileTo (usb.getChildFile (f.getFileName()));
    src.getChildFile ("SHA256SUMS.txt").copyFileTo (usb.getChildFile ("SHA256SUMS.txt"));
    auto rel = suite.scanFolder (gate, usb);
    // the folder's SHA256SUMS lists the newest archive only; an archive without a checksum line still shows
    check (rel.size() >= 1 && rel[0].version == Version::parse ("2.0.0"), "releases in a folder: newest first ("
                                                                             + juce::String (rel.size()) + " found)");
    check (! suite.installed (gate).present, "nothing installed in the sandbox yet");
    std::vector<juce::String> steps;
    auto r = suite.install (gate, rel[0], [&] (float, const juce::String& s) { steps.push_back (s); });
    const auto inst = suite.installed (gate);
    check (r.ok && inst.present && inst.version == Version::parse ("2.0.0") && inst.vendor == "Axell Yobag",
           "install from a folder: " + r.message);
    check (steps.size() >= 4, "install reports its steps (" + juce::String ((int) steps.size()) + ")");
    check (paths.staging().findChildFiles (juce::File::findFilesAndDirectories, false).isEmpty(), "install leaves no staging files");
    check (! paths.vst3.getChildFile ("install.sh").exists() && paths.vst3.findChildFiles (juce::File::findFilesAndDirectories, false).size() == 1,
           "only the plugin bundle reaches ~/.vst3 (the archive's scripts are never used)");

    // an unrelated plugin in ~/.vst3 is never touched
    const auto other = paths.vst3.getChildFile ("Other.vst3/Contents/marker.txt");
    other.create();
    other.replaceWithText ("keep me");

    // reinstall the same version: the old copy becomes a backup
    r = suite.install (gate, rel[0], {});
    check (r.ok && suite.backups (gate).size() == 1, "reinstall keeps the previous copy as a backup");

    // a damaged archive: checksum fails, nothing changes
    {
        auto bad = rel[0];
        const auto copy = sandboxRoot.getChildFile ("bad").getChildFile (bad.archiveFile.getFileName());
        copy.getParentDirectory().createDirectory();
        bad.archiveFile.copyFileTo (copy);
        juce::MemoryBlock mb;
        copy.loadFileAsData (mb);
        static_cast<char*> (mb.getData())[mb.getSize() / 2] ^= 0x5a;
        copy.replaceWithData (mb.getData(), mb.getSize());
        bad.archiveFile = copy;
        bad.sumsFile.copyFileTo (copy.getSiblingFile ("SHA256SUMS.txt"));
        bad.sumsFile = copy.getSiblingFile ("SHA256SUMS.txt");
        const auto before = suite.installed (gate);
        const auto b = suite.install (gate, bad, {});
        check (! b.ok && b.message.contains ("checksum") && suite.installed (gate).version == before.version
                   && suite.backups (gate).size() == 1, "a damaged archive is refused and nothing changes: " + b.message);
        // a checksum file without a line for this archive
        bad.sumsFile.replaceWithText ("0000000000000000000000000000000000000000000000000000000000000000  other.tar.gz\n");
        const auto c = suite.install (gate, bad, {});
        check (! c.ok && c.message.contains ("no checksum"), "an archive without a published checksum is refused");
    }
    // the library check must run: without ldd, or with a failing one, nothing is installed
    for (auto fake : { "/nonexistent/ldd", "false" })
    {
        Suite::lddProgram = fake;
        const auto before = suite.installed (gate);
        const auto nBackups = suite.backups (gate).size();
        const auto r2 = suite.install (gate, rel[0], {});
        check (! r2.ok && r2.message.contains ("could not be checked") && suite.installed (gate).version == before.version
                   && suite.backups (gate).size() == nBackups, juce::String ("library check that cannot run (") + fake + ") stops the install: " + r2.message);
    }
    Suite::lddProgram = "ldd";
    // an install stopped right after the swap (as if the Dojo were killed there) leaves a working plugin in place
    {
        Suite::stopAfterSwapForTest = true;
        const auto r3 = suite.install (gate, rel[0], {});
        Suite::stopAfterSwapForTest = false;
        const auto in = suite.installed (gate);
        bool ran = false;
        check (! r3.ok && in.present && in.version == Version::parse ("2.0.0") && Suite::missingLibraries (in.bundle, ran).isEmpty() && ran
                   && ! paths.vst3.getChildFile (gate.bundle).getChildFile ("Contents/x86_64-linux").findChildFiles (juce::File::findFiles, false, "*.so").isEmpty(),
               "killed right after the swap: the plugin folder still holds a complete, working plugin");
        paths.staging().deleteRecursively();   // what a stopped install leaves behind; the next install starts clean
        check (suite.install (gate, rel[0], {}).ok, "the next install after an interrupted one works");
    }
    // remove, then restore
    const int backupsBefore = suite.backups (gate).size();
    r = suite.uninstall (gate);
    check (r.ok && ! suite.installed (gate).present && suite.backups (gate).size() == juce::jmin (backupsBefore + 1, Suite::kBackupsKept),
           "remove keeps a copy: " + r.message);
    auto bk = suite.backups (gate);
    r = suite.restore (gate, bk[0]);
    check (r.ok && suite.installed (gate).present && suite.installed (gate).version == Version::parse ("2.0.0"), "restore: " + r.message);
    {
        // restoring over an installed version swaps them atomically and keeps the replaced one as a backup
        const auto n = suite.backups (gate).size();
        const auto rr = suite.restore (gate, suite.backups (gate)[0]);
        check (rr.ok && suite.installed (gate).present && suite.backups (gate).size() == n, "restore over an installed version keeps it as a backup: " + rr.message);
    }
    // backups are pruned to the newest few
    for (int i = 0; i < 5; ++i)
        suite.install (gate, rel[0], {});
    check (suite.backups (gate).size() == Suite::kBackupsKept, "backups are kept to the newest " + juce::String (Suite::kBackupsKept));
    check (other.loadFileAsString() == "keep me", "an unrelated plugin in ~/.vst3 was never touched");

    // a broken link in place of the bundle (a removed build folder) is replaced cleanly
    suite.uninstall (gate);
    sandboxRoot.getChildFile ("gone").createSymbolicLink (paths.vst3.getChildFile (gate.bundle), true);
    check (suite.installed (gate).brokenLink, "a broken link is recognised");
    r = suite.install (gate, rel[0], {});
    check (r.ok && suite.installed (gate).present && ! suite.installed (gate).brokenLink, "a broken link is replaced by the install");

    // legacy identity conflict
    paths.vst3.getChildFile ("Duo Vocal Comp.vst3").createDirectory();
    paths.vst3.getChildFile ("Axell Duo Compressor.vst3/Contents/Resources").createDirectory();
    paths.vst3.getChildFile ("Axell Duo Compressor.vst3/Contents/Resources/moduleinfo.json").replaceWithText ("{\"Version\": \"2.1.0\",}");
    check (suite.identityConflicts().size() == 1, "Duo Vocal Comp next to Axell Duo Compressor is reported");
}

//== hosting the installed plugins (read only) ===================================================================
static void testHosting()
{
    const auto real = Paths::fromEnvironment();
    for (const auto& p : Catalog::products())
    {
        const auto bundle = real.vst3.getChildFile (p.bundle);
        if (! bundle.isDirectory())
        {
            std::printf ("skip  %s is not installed\n", p.name.toRawUTF8());
            continue;
        }
        juce::String err;
        auto h = HostedPlugin::load (bundle, 48000.0, 512, err);
        check (h != nullptr, p.name + ": hosted (" + err + ")");
        if (h == nullptr)
            continue;
        const auto& d = h->description();
        const auto progs = h->programNames();
        std::printf ("      %s: %d programs, latency %d, pins in %d+%d out %d, REAPER uid %d\n", p.name.toRawUTF8(), progs.size(),
                     h->latency(), h->mainInputs(), h->sideInputs(), h->mainOutputs(), state::reaperUid (real, p, d));
        const auto st = h->vst3State();
        const auto tree = state::treeFromComponent (st.component);
        check (tree.isValid() && tree.getNumChildren() >= 3, p.name + ": its saved state parses (" + juce::String (tree.getNumChildren()) + " entries)");
        const auto renamed = state::withPresetName (st.component, "Dojo Test \"1\"");
        check (! renamed.empty() && state::treeFromComponent (renamed).getProperty ("presetName").toString() == "Dojo Test \"1\"",
               p.name + ": the preset name is replaced in the state");
        // the renamed state loads back into the plugin and comes out again with the name (De-Reverb has no name)
        h->setVst3State ({ renamed, st.controller });
        const auto back = state::treeFromComponent (h->vst3State().component);
        check (back.isValid() && (p.key == "dereverb" || back.getProperty ("presetName").toString() == "Dojo Test \"1\""),
               p.name + ": the renamed state loads into the plugin");
        if (p.hasUserPresets())
        {
            const auto paths = Paths::sandbox (sandboxRoot.getChildFile ("presets"));
            auto xml = state::presetXml (p, tree, "Dojo Test", "made by a test");
            const auto w = state::writePreset (paths, p, *xml, "Dojo Test");
            check (w.ok && w.file.isAChildOf (paths.dataHome) && w.file.hasFileExtension (p.presetExt), p.name + ": " + w.message);
            auto saved = juce::XmlDocument::parse (w.file);
            check (saved != nullptr && saved->getTagName() == tree.getType().toString() && saved->getChildByName ("AB") == nullptr
                       && saved->getChildByName ("UI") == nullptr && ! saved->hasAttribute ("bleedPath"),
                   p.name + ": preset file has the plugin's own root element, without A/B, layout or bleed data");
            // a preset the user saved under the same name is never replaced
            juce::XmlElement mine (*saved);
            mine.removeAttribute (state::kCreatedBy);
            mine.writeTo (w.file);
            const auto w2 = state::writePreset (paths, p, *xml, "Dojo Test");
            check (! w2.ok && juce::XmlDocument::parse (w.file)->getStringAttribute (state::kCreatedBy).isEmpty(),
                   p.name + ": a user's own preset with the same name is left alone");
        }
        if (p.key == "penta")
        {
            // render alignment: a flat EQ passes audio unchanged and in time
            h->selectProgram ("Init (Flat)");
            juce::AudioBuffer<float> in (2, 48000);
            juce::Random rnd (3);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (c, i, (rnd.nextFloat() - 0.5f) * 0.2f);
            const auto out = h->render (in);
            double err2 = 0.0, ref2 = 0.0;
            for (int i = 1000; i < 47000; ++i)
            {
                err2 += std::pow (out.getSample (0, i) - in.getSample (0, i), 2.0);
                ref2 += std::pow (in.getSample (0, i), 2.0);
            }
            check (10.0 * std::log10 (err2 / ref2 + 1e-30) < -100.0, "render: flat Penta EQ returns the input aligned (residual "
                                                                          + juce::String (10.0 * std::log10 (err2 / ref2 + 1e-30), 1) + " dB)");
        }
    }
}

//== chain templates (built from the installed plugins, written into the sandbox) =================================
static std::vector<std::string> blockBodies (const juce::String& text)
{
    // the base64 lines of each <VST> block, for reaper::parseBlock
    std::vector<std::string> out;
    juce::StringArray lines;
    lines.addLines (text);
    return out;
}

static int parseChain (const juce::String& text, juce::StringArray& programs)
{
    juce::StringArray lines;
    lines.addLines (text);
    int ok = 0;
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].trimStart().startsWith ("<VST "))
        {
            std::vector<std::string> body;
            for (++i; i < lines.size() && lines[i].trim() != ">"; ++i)
                body.push_back (lines[i].toStdString());
            const auto p = reaper::parseBlock (body);
            if (p.ok && ! p.component.empty() && state::treeFromComponent (p.component).isValid())
            {
                ++ok;
                programs.add (p.program);
            }
        }
    return ok;
}

static void testTemplates()
{
    const auto real = Paths::fromEnvironment();
    auto sandbox = Paths::sandbox (sandboxRoot.getChildFile ("templates"));
    sandbox.vst3 = real.vst3;   // the installed plugins (only read)
    // REAPER's plugin cache read from the real config: copy it into the sandbox
    sandbox.reaperConfig().createDirectory();
    real.reaperConfig().getChildFile ("reaper-vstplugins64.ini").copyFileTo (sandbox.reaperConfig().getChildFile ("reaper-vstplugins64.ini"));
    ChainWriter cw (sandbox);
    for (const auto& t : ChainTemplate::all())
    {
        juce::StringArray problems;
        auto stages = cw.build (t, problems);
        const auto text = cw.chainText (stages);
        juce::StringArray progs;
        const int n = parseChain (text, progs);
        bool namesOk = true;
        for (size_t i = 0; i < t.stages.size() && i < (size_t) progs.size(); ++i)
            namesOk &= progs[(int) i] == t.stages[i].program;
        check (problems.isEmpty() && n == (int) t.stages.size() && namesOk,
               "template \"" + t.name + "\": " + juce::String (n) + " plugins with their programs" + (problems.isEmpty() ? "" : ": " + problems.joinIntoString ("; ")));
        const auto w = cw.write (t.name, text);
        check (w.ok && w.file.isAChildOf (sandbox.configHome), "template \"" + t.name + "\" written: " + w.message);
    }
    // a file the user made in that folder is never replaced; one the Dojo wrote is
    // a project with the chain on an armed vocal track; in a sandbox nothing is launched, nothing replaced
    {
        const auto chainFile = sandbox.reaperChains().getChildFile ("Rap Lead.RfxChain");
        const auto proj = ChainWriter::projectText ("Vocal - Test", chainFile.loadFileAsString());
        juce::StringArray progs;
        check (proj.startsWith ("<REAPER_PROJECT") && proj.contains ("NAME \"Vocal - Test\"") && proj.contains ("    REC 1 0 0 0 0 0 0 0\n")
                   && parseChain (proj, progs) == 5, "REAPER project: named, armed track holding the five plugins");
        juce::File p1, p2;
        const auto r1 = cw.openInReaper (chainFile, "Vocal - Test", &p1);
        const auto r2 = cw.openInReaper (chainFile, "Vocal - Test", &p2);
        check (r1.ok && r2.ok && p1.existsAsFile() && p2.existsAsFile() && p1 != p2 && p1.isAChildOf (sandbox.home)
                   && r1.message.startsWith ("Sandbox"), "Open in REAPER (sandbox): projects written, never replaced, REAPER not launched");
        check (! cw.openInReaper (sandbox.reaperChains().getChildFile ("missing.RfxChain"), "x").ok, "Open in REAPER refuses a chain that was not saved");
    }
    const auto mine = sandbox.reaperChains().getChildFile ("Rap Lead.RfxChain");
    mine.replaceWithText ("my own edit", false, false, "\n");
    check (! cw.write ("Rap Lead", "x").ok && mine.loadFileAsString() == "my own edit", "an edited chain file is never replaced");
    juce::ignoreUnused (blockBodies);
}

//== Voice Profile on the artist's dry take (sandbox output) ======================================================
static void testProfile()
{
    const auto take = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Desktop/dry vocal.wav");
    if (! take.existsAsFile())
    {
        std::printf ("skip  no dry take on the Desktop\n");
        return;
    }
    const auto real = Paths::fromEnvironment();
    auto sandbox = Paths::sandbox (sandboxRoot.getChildFile ("profile"));
    sandbox.vst3 = real.vst3;
    sandbox.reaperConfig().createDirectory();
    real.reaperConfig().getChildFile ("reaper-vstplugins64.ini").copyFileTo (sandbox.reaperConfig().getChildFile ("reaper-vstplugins64.ini"));
    ProfileEngine eng (sandbox);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto o = eng.run (take, true, {});
    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    check (o.ok && o.stages.size() == 5 && o.built.size() == 5, "profile: analysed and tuned in " + juce::String (secs, 1) + " s " + o.error);
    if (! o.ok)
        return;
    for (const auto& s : o.stages)
    {
        juce::String v;
        for (auto& [k, t] : s.values)
            v << k << "=" << t << "  ";
        std::printf ("      %-22s in %6.2f LUFS out %6.2f LUFS | %s %s\n", s.product->name.toRawUTF8(), s.inLufs, s.outLufs,
                     v.toRawUTF8(), s.problem.toRawUTF8());
        check (s.problem.isEmpty(), s.product->name + ": every setting took");
        check (std::abs (s.outLufs - s.inLufs) < 0.15f, s.product->name + ": loudness kept on the whole take (" + juce::String (s.outLufs - s.inLufs, 2) + " LU)");
    }
    // Velvet's LEVEL = -18 LUFS minus its input
    const auto& vs = o.stages[4];
    for (auto& [k, t] : vs.values)
        if (k == "Level")
            check (std::abs (t.retainCharacters ("0123456789.-+").getFloatValue() - (-18.0f - vs.inLufs)) < 0.15f, "Velvet LEVEL as LEARN sets it: " + t);
    // independent re-measure of the Duo's FET: the tuned FET input gives ~6 dB at the 95th percentile
    {
        auto& duo = *o.built[3].plugin;
        juce::AudioBuffer<float> in;
        double rate;
        juce::String err;
        ProfileEngine::loadTake (take, in, rate, err);
        for (int i = 0; i < 3; ++i)   // through the first three tuned stages
            in = o.built[(size_t) i].plugin->render (in);
        const auto fetIn = duo.text ("FET Input").retainCharacters ("0123456789.-+").getFloatValue();
        for (auto [k, v] : { std::pair { "Opto On", "Off" }, { "Limiter On", "Off" }, { "Mix", "100 %" }, { "Output", "0.0 dB" }, { "FET Output", "0.0 dB" } })
            duo.set (k, v);
        const auto a = ProfileEngine::envelopeDb (in, rate), b = ProfileEngine::envelopeDb (duo.render (in), rate);
        std::vector<float> lv (a.begin(), a.end());
        std::sort (lv.begin(), lv.end());
        const float loud = lv[(size_t) (0.95 * (lv.size() - 1))] - 20.0f;
        std::vector<float> gr;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            if (a[i] > loud)
                gr.push_back (a[i] + fetIn - b[i]);
        std::sort (gr.begin(), gr.end());
        const float p95 = gr.empty() ? 0.0f : gr[(size_t) (0.95 * (gr.size() - 1))];
        check (std::abs (p95 - 6.0f) < 1.0f, "Duo FET re-measured on the whole take: 95th percentile GR " + juce::String (p95, 2) + " dB (target 6)");
    }
    // write into the sandbox: four presets, one chain, one profile
    const auto lines = eng.write (o, { "Dojo Test Voice", true, true, true });
    for (auto& l : lines)
        std::printf ("      %s\n", l.toRawUTF8());
    int presets = 0;
    for (const auto& p : Catalog::products())
        if (p.hasUserPresets() && sandbox.presetFolder (p.presetFolder).getChildFile ("Dojo Test Voice" + p.presetExt).existsAsFile())
            ++presets;
    check (presets == 4, "profile: four plugin presets written (" + juce::String (presets) + ")");
    const auto chain = sandbox.reaperChains().getChildFile ("Dojo Test Voice - Vocal Chain.RfxChain");
    juce::StringArray progs;
    check (chain.existsAsFile() && parseChain (chain.loadFileAsString(), progs) == 5 && progs.size() == 5 && progs[0] == "Dojo Test Voice",
           "profile: REAPER chain with five plugins, each named after the profile");
    check (sandbox.profiles().getChildFile ("Dojo Test Voice.json").existsAsFile(), "profile: profile file written");
    check (! real.reaperChains().getChildFile ("Dojo Test Voice - Vocal Chain.RfxChain").exists()
               && ! real.presetFolder ("AxellGate").getChildFile ("Dojo Test Voice.axgpreset").exists(),
           "nothing was written outside the sandbox");
}

//== workstation checks ==========================================================================================
static void testSystemCheck()
{
    const juce::String ini = "[audioconfig]\r\nx=1\r\n[reaper]\r\nvstpath=/a/b;/var/home/u/.vst3\r\nother=2\r\n[REAPER2]\r\nvstpath=/zzz\r\n";
    const auto vp = SystemCheck::parseVstPath (ini);
    check (vp.size() == 2 && vp[1] == "/var/home/u/.vst3", "REAPER ini: vstpath of the [reaper] section only");
    const auto added = SystemCheck::withVstPathAdded (ini, "/home/u/.vst3");
    check (added.contains ("vstpath=/a/b;/var/home/u/.vst3;/home/u/.vst3\r\n") && added.contains ("vstpath=/zzz") && added.contains ("other=2\r\n")
               && added.length() == ini.length() + 14, "REAPER ini: folder appended, everything else (line endings too) unchanged");
    check (SystemCheck::withVstPathAdded ("[reaper]\nfoo=1\n", "/x") == "[reaper]\nfoo=1\n", "REAPER ini without vstpath is left alone (defaults include ~/.vst3)");
    const auto pw = SystemCheck::parsePipewire ("Found \"settings\" metadata 31\nupdate: id:0 key:'clock.rate' value:'48000' type:''\n"
                                                "update: id:0 key:'clock.quantum' value:'512' type:''\nupdate: id:0 key:'clock.force-quantum' value:'0' type:''\n");
    check (pw.first == 48000 && pw.second == 512, "PipeWire settings parsed");
    check (SystemCheck::parsePipewire ("key:'clock.rate' value:'44100'\nkey:'clock.quantum' value:'1024'\nkey:'clock.force-quantum' value:'128'\n").second == 128,
           "PipeWire: a forced buffer size wins");
    check (SystemCheck::pipewireDropIn (256).contains ("default.clock.quantum = 256"), "PipeWire drop-in");

    // fixes in a sandbox
    const auto paths = Paths::sandbox (sandboxRoot.getChildFile ("syscheck"));
    SystemCheck sc (paths);
    paths.vst3.createDirectory();
    for (auto n : { "a.vst3", "b.vst3" })
        sandboxRoot.getChildFile ("nowhere").getChildFile (n).createSymbolicLink (paths.vst3.getChildFile (n), true);
    paths.vst3.getChildFile ("real.vst3").createDirectory();
    const auto pf = sc.pluginFolder();
    check (pf.status == Check::Status::warning && pf.fixes.size() == 1, "broken links in the plugin folder are found: " + pf.detail);
    const auto moved = sc.apply (pf.fixes[0]);
    check (moved.ok && ! paths.vst3.getChildFile ("a.vst3").existsAsFile() && paths.vst3.getChildFile ("real.vst3").isDirectory()
               && sc.pluginFolder().status == Check::Status::ok, "broken links moved aside, real plugins stay: " + moved.message);
    paths.reaperConfig().createDirectory();
    paths.reaperConfig().getChildFile ("reaper.ini").replaceWithText ("[reaper]\nvstpath=/nowhere/at/all\n", false, false, "\n");
    const auto rp = sc.reaperPath();
    check (rp.status == Check::Status::problem && rp.fixes.size() == 1, "REAPER without ~/.vst3 is a problem with a fix");
    if (! Process::isRunning ("reaper"))
    {
        const auto r = sc.apply (rp.fixes[0]);
        check (r.ok && sc.reaperPath().status != Check::Status::problem
                   && paths.reaperConfig().findChildFiles (juce::File::findFiles, false, "reaper.ini.dojo-backup-*").size() == 1,
               "REAPER path fixed in the sandbox, previous reaper.ini kept: " + r.message);
    }
    else
        std::printf ("skip  REAPER is running: the reaper.ini fix refuses to run (by design)\n");
    const auto q = sc.apply ({ Fix::Kind::pipewireQuantum, "", "", {}, 256, true, "" });
    check (paths.pipewireDropIns().getChildFile ("50-axell-signature-dojo.conf").existsAsFile(), "PipeWire buffer setting written in the sandbox");
    sc.apply ({ Fix::Kind::pipewireQuantum, "", "", {}, 0, false, "" });
    check (! paths.pipewireDropIns().getChildFile ("50-axell-signature-dojo.conf").existsAsFile(), "PipeWire setting removed again");
    const auto cmd = sc.apply ({ Fix::Kind::command, "", "", { "flatpak", "override", "--user", "--filesystem=~/.vst3:ro", "x.y.Z" }, 0, false, "" });
    check (cmd.ok && cmd.message.startsWith ("Sandbox: would run"), "a sandbox never runs a fix's command: " + cmd.message);
    juce::ignoreUnused (q);

    // read-only: the real workstation
    const auto all = SystemCheck (Paths::fromEnvironment()).runAll();
    for (const auto& c : all)
        std::printf ("      [%s] %-22s %s%s\n", c.status == Check::Status::ok ? "ok" : c.status == Check::Status::info ? "info"
                                                 : c.status == Check::Status::warning ? "WARN" : "PROBLEM",
                     c.title.toRawUTF8(), c.detail.toRawUTF8(), c.manual.isNotEmpty() ? (" | " + c.manual).toRawUTF8() : "");
    check (all.size() >= 10, "workstation: " + juce::String ((int) all.size()) + " checks ran");
}

//== v2: Reference Match, step-through audio, chain loading, Batch Render, Backup, Streaming Preview ===============
static void testV2()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto take = home.getChildFile ("Desktop/dry vocal.wav");
    if (! take.existsAsFile())
    {
        std::printf ("skip  no dry take on the Desktop\n");
        return;
    }
    const auto real = Paths::fromEnvironment();
    auto sandbox = Paths::sandbox (sandboxRoot.getChildFile ("v2"));
    sandbox.vst3 = real.vst3;
    sandbox.reaperConfig().createDirectory();
    real.reaperConfig().getChildFile ("reaper-vstplugins64.ini").copyFileTo (sandbox.reaperConfig().getChildFile ("reaper-vstplugins64.ini"));

    // a reference: the same voice through a known EQ (+4 dB at 3 kHz, -3 dB at 300 Hz)
    const auto refFile = sandboxRoot.getChildFile ("reference.wav");
    {
        wavio::Audio a;
        wavio::read (take.getFullPathName().toStdString(), a);
        for (auto& ch : a.ch)
        {
            axell::Biquad hi, lo;
            hi.setPeak (a.rate, 3000.0, 1.0, 4.0);
            lo.setPeak (a.rate, 300.0, 1.2, -3.0);
            for (auto& v : ch)
                v = (float) lo.process (hi.process (v, 0), 0);
        }
        wavio::write (refFile.getFullPathName().toStdString(), a);
    }
    ProfileEngine eng (sandbox);
    auto o = eng.run (take, true, {}, nullptr, refFile, 1.0f);
    bool problems = false;
    for (auto& st : o.stages)
        problems |= st.problem.isNotEmpty();
    check (o.ok && ! problems && o.bells.size() >= 2 && o.toneAfterDb < 0.5f * o.toneBeforeDb,
           "Reference Match through the installed Penta EQ: tone difference " + juce::String (o.toneBeforeDb, 2) + " -> "
               + juce::String (o.toneAfterDb, 2) + " dB RMS on the rendered take (" + juce::String ((int) o.bells.size()) + " bells)");
    bool near3k = false;
    for (auto& bl : o.bells)
        near3k |= std::abs (std::log2 (bl.hz / 3000.0f)) < 0.35f && bl.gainDb > 1.5f;
    check (near3k, "Reference Match: the reference's +4 dB at 3 kHz is found");
    check (o.stageAudio.size() == 5 && o.stageAudio.back().getNumSamples() == o.processed.getNumSamples(),
           "step-through: the take after each of the five plugins is kept");
    for (auto& st : o.stages)
        if (st.product != nullptr)
            check (std::abs (st.outLufs - st.inLufs) < 0.15f, st.product->name + ": loudness kept with the reference match");
    eng.write (o, { "V2 Voice", true, true, true });
    const auto chainFile = sandbox.reaperChains().getChildFile ("V2 Voice - Vocal Chain.RfxChain");

    // a saved chain loads back with the same settings
    {
        auto loaded = tools::loadChain (sandbox, chainFile, 48000.0);
        bool same = loaded.ok() && loaded.stages.size() == o.built.size();
        for (size_t i = 0; same && i < loaded.stages.size(); ++i)
            for (const char* param : { "Reduction", "Range", "HP Frequency", "Band 1 Gain", "FET Input", "Level", "Output" })
                if (o.built[i].plugin->hasParameter (param))
                    same &= loaded.stages[i].plugin->text (param) == o.built[i].plugin->text (param);
        check (same, "a saved chain loads back as live plugins with the same settings (" + juce::String ((int) loaded.stages.size()) + " plugins)");
        auto bad = sandboxRoot.getChildFile ("foreign.RfxChain");
        bad.replaceWithText ("BYPASS 0 0 0\n<VST \"VST3: Other (X)\" \"Other.vst3\" 0 \"\" 1{0123456789ABCDEF0123456789ABCDEF} \"\"\n  AAAA\n>\n");
        check (! tools::loadChain (sandbox, bad, 48000.0).ok(), "a chain with a foreign plugin is reported, not half-loaded silently");
    }
    // Batch Render
    {
        const auto out = sandboxRoot.getChildFile ("rendered");
        juce::Array<juce::File> files { take, refFile };
        auto res = tools::batchRender (sandbox, chainFile, files, out, {}, nullptr);
        bool ok = res.size() == 2;
        for (auto& r : res)
            ok &= r.ok && r.output.existsAsFile() && r.output.isAChildOf (out);
        check (ok, "Batch Render: two takes rendered through the chain into the output folder");
        if (ok)
            check (std::abs (res[0].outLufs - res[0].inLufs) < 0.3f && res[0].outTruePeakDbtp < 0.0f,
                   "Batch Render: the take it was tuned on keeps its loudness (" + juce::String (res[0].inLufs, 2) + " -> " + juce::String (res[0].outLufs, 2)
                       + " LUFS), peak " + juce::String (res[0].outTruePeakDbtp, 1) + " dBTP");
        auto again = tools::batchRender (sandbox, chainFile, juce::Array<juce::File> { take }, out, {}, nullptr);
        check (again.size() == 1 && again[0].ok && again[0].output != res[0].output, "Batch Render never replaces an earlier render");
        wavio::Audio w;
        check (ok && wavio::read (res[0].output.getFullPathName().toStdString(), w) && w.ch.size() == 2 && w.frames() > 48000 * 50,
               "Batch Render output is a readable stereo WAV of the whole take");
    }
    // Backup & Move
    {
        const auto items = tools::backupItems (sandbox);
        juce::File folder;
        const auto ex = tools::exportBackup (sandbox, sandboxRoot.getChildFile ("usb"), &folder);
        check (ex.ok && items.size() >= 6 && folder.getChildFile ("manifest.txt").existsAsFile(), "Backup: " + ex.message);
        // another computer: a fresh sandbox; one file damaged in transit
        auto other = Paths::sandbox (sandboxRoot.getChildFile ("other-computer"));
        const auto victim = folder.getChildFile (items[0].relative);
        victim.appendText ("x");
        const auto im = tools::importBackup (other, folder);
        check (! im.ok && im.message.contains ("1 damaged") && (int) tools::backupItems (other).size() == (int) items.size() - 1,
               "Restore on another computer: every intact file copied, the damaged one skipped: " + im.message);
        const auto im2 = tools::importBackup (other, folder);
        check (im2.message.startsWith ("0 files restored"), "Restore again: nothing is replaced: " + im2.message);
        auto fake = sandboxRoot.getChildFile ("not-a-backup");
        fake.createDirectory();
        fake.getChildFile ("manifest.txt").replaceWithText ("abc  ../../etc/passwd\n");
        const auto im3 = tools::importBackup (other, fake);
        check (! juce::File ("/etc/passwd.dojo").exists() && im3.message.contains ("not recognised"), "Restore ignores paths outside its own folders: " + im3.message);
    }
    // Streaming Preview on the artist's mix
    {
        const auto mix = home.getChildFile ("Desktop/axell suite on.wav");
        if (mix.existsAsFile())
        {
            const auto m = tools::previewMaster (mix);
            check (m.ok && m.services.size() == 5 && m.measurement.integratedLufs < 0.0f && m.measurement.integratedLufs > -40.0f,
                   "Streaming Preview of the artist's mix: " + juce::String (m.measurement.integratedLufs, 1) + " LUFS, true peak "
                       + juce::String (m.measurement.truePeakDbtp, 1) + " dBTP; Spotify " + juce::String (m.services[0].gainDb, 1) + " dB, Apple "
                       + juce::String (m.services[1].gainDb, 1) + " dB");
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    sandboxRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dojo-tests-" + juce::String::toHexString (juce::Random().nextInt64()));
    if (argc > 1 && juce::String (argv[1]) == "--keep")
        std::printf ("sandbox: %s\n", sandboxRoot.getFullPathName().toRawUTF8());
    // writes chains + what REAPER should show for each, for tools/reaper-verify.sh
    if (argc > 2 && juce::String (argv[1]) == "--reaper-prep")
    {
        const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        dir.createDirectory();
        const auto real = Paths::fromEnvironment();
        auto withUid = Paths::sandbox (dir.getChildFile ("sandbox"));
        withUid.vst3 = real.vst3;
        withUid.reaperConfig().createDirectory();
        real.reaperConfig().getChildFile ("reaper-vstplugins64.ini").copyFileTo (withUid.reaperConfig().getChildFile ("reaper-vstplugins64.ini"));
        auto noUid = Paths::sandbox (dir.getChildFile ("sandbox-nouid"));
        noUid.vst3 = real.vst3;
        auto expect = [] (const std::vector<BuiltStage>& st, const std::vector<StageOutcome>* stages)
        {
            juce::String e;
            for (size_t i = 0; i < st.size(); ++i)
            {
                e << "FX\t" << (int) i << "\t" << st[i].product->name << "\t" << st[i].presetName << "\n";
                if (stages != nullptr)
                    for (auto& [k, v] : (*stages)[i].values)
                        e << "P\t" << (int) i << "\t" << k << "\t" << v << "\n";
            }
            return e;
        };
        for (const auto& t : ChainTemplate::all())
        {
            juce::StringArray problems;
            ChainWriter cw (withUid);
            auto st = cw.build (t, problems);
            dir.getChildFile (t.id + ".RfxChain").replaceWithText (cw.chainText (st), false, false, "\n");
            dir.getChildFile (t.id + ".expect").replaceWithText (expect (st, nullptr), false, false, "\n");
        }
        ProfileEngine eng (withUid);
        auto o = eng.run (real.home.getChildFile ("Desktop/dry vocal.wav"), true, {});
        if (! o.ok)
        {
            std::printf ("profile failed: %s\n", o.error.toRawUTF8());
            return 1;
        }
        for (auto& b : o.built)
            ChainWriter::capture (b, "Dojo Verify");
        dir.getChildFile ("profile.RfxChain").replaceWithText (ChainWriter (withUid).chainText (o.built), false, false, "\n");
        dir.getChildFile ("profile.expect").replaceWithText (expect (o.built, &o.stages), false, false, "\n");
        dir.getChildFile ("profile-uid0.RfxChain").replaceWithText (ChainWriter (noUid).chainText (o.built), false, false, "\n");
        dir.getChildFile ("profile-project.RPP").replaceWithText (ChainWriter::projectText ("Vocal - Dojo Verify", ChainWriter (withUid).chainText (o.built)), false, false, "\n");
        dir.getChildFile ("profile-project.expect").replaceWithText (expect (o.built, &o.stages) + "ARM\t1\n", false, false, "\n");
        dir.getChildFile ("profile-uid0.expect").replaceWithText (expect (o.built, &o.stages), false, false, "\n");
        std::printf ("prepared %d chains in %s\n", (int) ChainTemplate::all().size() + 2, dir.getFullPathName().toRawUTF8());
        return 0;
    }
    // De-Reverb presets as single-plugin REAPER chains, each loudness-matched on a take. The list is personal (takes,
    // names), so it comes from a local file: one line per preset, "<take path>|<chain name>|<reduction %>"
    if (argc > 2 && juce::String (argv[1]) == "--dereverb-presets")
    {
        const auto real = Paths::fromEnvironment();
        const auto& prod = *Catalog::find ("dereverb");
        struct P { juce::String take, name; float red; };
        std::vector<P> list;
        juce::StringArray lines;
        juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).readLines (lines);
        for (auto& l : lines)
        {
            const auto parts = juce::StringArray::fromTokens (l, "|", "");
            if (parts.size() == 3)
                list.push_back ({ parts[0].trim(), parts[1].trim(), (float) parts[2].getDoubleValue() });
        }
        ChainWriter cw (real);
        juce::String lastTake;
        juce::AudioBuffer<float> audio;
        double rate = 48000.0;
        float inL = 0;
        for (const auto& e : list)
        {
            if (lastTake != e.take)
            {
                juce::String err;
                if (! ProfileEngine::loadTake (juce::File::getCurrentWorkingDirectory().getChildFile (e.take), audio, rate, err)) { std::printf ("%s\n", err.toRawUTF8()); return 1; }
                inL = ProfileEngine::lufs (audio, rate);
                lastTake = e.take;
            }
            BuiltStage b;
            b.product = &prod;
            juce::String err;
            b.plugin = HostedPlugin::load (real.vst3.getChildFile (prod.bundle), rate, 512, err);
            if (b.plugin == nullptr) { std::printf ("%s\n", err.toRawUTF8()); return 1; }
            b.plugin->set ("Reduction", juce::String (juce::roundToInt (e.red)) + " %");
            b.plugin->set ("Output", "0.0 dB");
            float out = ProfileEngine::lufs (b.plugin->render (audio), rate);
            b.plugin->set ("Output", juce::String (inL - out, 1) + " dB");
            out = ProfileEngine::lufs (b.plugin->render (audio), rate);
            ChainWriter::capture (b, e.name.fromFirstOccurrenceOf ("De-Reverb - ", false, false));
            std::vector<BuiltStage> one;
            one.push_back (std::move (b));
            const auto w = cw.write (e.name, cw.chainText (one));
            std::printf ("%-48s Reduction %3.0f %%  Output %-8s  loudness %.2f -> %.2f LUFS  %s\n", e.name.toRawUTF8(), e.red,
                         one[0].plugin->text ("Output").toRawUTF8(), inL, out, w.ok ? "saved" : w.message.toRawUTF8());
        }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--programs")
    {
        for (const auto& p : Catalog::products())
        {
            juce::String err;
            if (auto h = HostedPlugin::load (Paths::fromEnvironment().vst3.getChildFile (p.bundle), 48000.0, 512, err))
                std::printf ("%s: %s\n", p.name.toRawUTF8(), h->programNames().joinIntoString (" | ").toRawUTF8());
        }
        return 0;
    }
    sandboxRoot.createDirectory();
    testProcess();
    testSuite();
    testHosting();
    testSystemCheck();
    testTemplates();
    testProfile();
    testV2();
    if (! (argc > 1 && juce::String (argv[1]) == "--keep"))
        sandboxRoot.deleteRecursively();
    std::printf ("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
