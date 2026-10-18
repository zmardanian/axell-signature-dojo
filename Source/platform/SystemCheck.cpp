#include "SystemCheck.h"
#include "Process.h"

#include <climits>
#include <dirent.h>
#include <gnu/libc-version.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace dojo
{
namespace
{
// KEY=value files (os-release)
juce::String keyValue (const juce::File& f, const juce::String& key)
{
    juce::StringArray lines;
    f.readLines (lines);
    for (auto& l : lines)
        if (l.startsWith (key + "="))
            return l.fromFirstOccurrenceOf ("=", false, false).trim().unquoted();
    return {};
}

// REAPER's plugin cache names a bundle with every character but letters, digits and '.' turned into '_'
juce::String reaperCacheKey (const juce::String& bundle)
{
    juce::String k;
    for (auto ch : bundle)
        k += (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == '.') ? juce::String::charToString (ch) : juce::String ("_");
    return k;
}

bool isBrokenLink (const juce::File& f)
{
    struct stat st {};
    return ::lstat (f.getFullPathName().toRawUTF8(), &st) == 0 && S_ISLNK (st.st_mode) && ! f.exists();
}

// the same folder, written differently (~, /home vs /var/home on Silverblue-style systems, trailing slash)
bool sameFolder (juce::String a, const juce::File& b, const juce::File& home)
{
    a = a.trim();
    if (a.startsWith ("~"))
        a = home.getFullPathName() + a.substring (1);
    if (! juce::File::isAbsolutePath (a))
        return false;
    const juce::File fa (a);
    if (fa == b)
        return true;
    char ra[PATH_MAX], rb[PATH_MAX];
    return ::realpath (fa.getFullPathName().toRawUTF8(), ra) != nullptr && ::realpath (b.getFullPathName().toRawUTF8(), rb) != nullptr
           && juce::String (ra) == juce::String (rb);
}

const char* const kFlatpakDaws[][2] = { { "fm.reaper.Reaper", "REAPER" }, { "com.bitwig.BitwigStudio", "Bitwig Studio" },
                                        { "org.ardour.Ardour", "Ardour" }, { "io.lmms.LMMS", "LMMS" },
                                        { "org.zrythm.Zrythm", "Zrythm" }, { "org.tenacityaudio.Tenacity", "Tenacity" } };
} // namespace

//==============================================================================
juce::StringArray SystemCheck::parseVstPath (const juce::String& ini)
{
    bool inReaper = false;
    for (auto line : juce::StringArray::fromLines (ini))
    {
        line = line.trim();
        if (line.startsWithChar ('['))
            inReaper = line.equalsIgnoreCase ("[reaper]");
        else if (inReaper && line.startsWith ("vstpath="))
            return juce::StringArray::fromTokens (line.fromFirstOccurrenceOf ("=", false, false), ";", "");
    }
    return {};
}

juce::String SystemCheck::withVstPathAdded (const juce::String& ini, const juce::String& folder)
{
    const auto eol = ini.contains ("\r\n") ? juce::String ("\r\n") : juce::String ("\n");
    auto lines = juce::StringArray::fromLines (ini);
    if (ini.endsWithChar ('\n') && ! lines.isEmpty() && lines[lines.size() - 1].isEmpty())
        lines.remove (lines.size() - 1);   // fromLines gives the final line ending an empty line of its own
    bool inReaper = false;
    for (auto& line : lines)
    {
        const auto t = line.trim();
        if (t.startsWithChar ('['))
            inReaper = t.equalsIgnoreCase ("[reaper]");
        else if (inReaper && t.startsWith ("vstpath="))
        {
            auto paths = juce::StringArray::fromTokens (t.fromFirstOccurrenceOf ("=", false, false), ";", "");
            paths.removeEmptyStrings();
            paths.add (folder);
            line = "vstpath=" + paths.joinIntoString (";");
            return lines.joinIntoString (eol) + (ini.endsWith ("\n") ? eol : juce::String());
        }
    }
    return ini;   // no vstpath: REAPER uses its defaults, which include ~/.vst3
}

std::pair<int, int> SystemCheck::parsePipewire (const juce::String& out)
{
    int rate = 0, quantum = 0, force = 0;
    for (auto line : juce::StringArray::fromLines (out))
    {
        const auto key = line.fromFirstOccurrenceOf ("key:'", false, false).upToFirstOccurrenceOf ("'", false, false);
        const auto value = line.fromFirstOccurrenceOf ("value:'", false, false).upToFirstOccurrenceOf ("'", false, false).getIntValue();
        if (key == "clock.rate") rate = value;
        else if (key == "clock.quantum") quantum = value;
        else if (key == "clock.force-quantum") force = value;
    }
    return { rate, force > 0 ? force : quantum };
}

juce::String SystemCheck::pipewireDropIn (int quantum)
{
    return "# Written by Axell Signature Dojo: the audio buffer size. Delete this file (or use the Dojo) to undo.\n"
           "context.properties = {\n"
           "    default.clock.quantum = " + juce::String (quantum) + "\n"
           "    default.clock.min-quantum = 32\n"
           "    default.clock.max-quantum = 2048\n"
           "}\n";
}

juce::File SystemCheck::findReaper (const Paths& paths)
{
    for (auto f : { paths.home.getChildFile ("opt/REAPER/reaper"), juce::File ("/opt/REAPER/reaper"),
                    paths.home.getChildFile ("REAPER/reaper"), juce::File ("/usr/local/bin/reaper") })
        if (f.existsAsFile())
            return f;
    return Process::which ("reaper");
}

//==============================================================================
Check SystemCheck::system() const
{
    Check c { "system", "Computer", "System", {}, Check::Status::ok, {}, {} };
    auto os = keyValue (juce::File ("/etc/os-release"), "PRETTY_NAME");
    const auto ver = keyValue (juce::File ("/etc/os-release"), "VERSION_ID");
    if (ver.isNotEmpty() && ! os.contains (ver))
        os << " " << ver;
    const bool immutable = juce::File ("/run/ostree-booted").exists();
    const auto glibc = juce::String (gnu_get_libc_version());
    const auto gv = Version::parse (glibc.toStdString());
    c.detail << (os.isNotEmpty() ? os : juce::String ("Linux")) << (immutable ? " (immutable system image)" : "") << ", "
             << juce::SystemStats::getNumCpus() << " CPU threads, " << juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0, 1)
             << " GB memory, glibc " << glibc << ".";
    if (gv.valid && gv < Version::parse ("2.38"))
    {
        c.status = Check::Status::problem;
        c.detail << " The Axell plugins need glibc 2.38 or newer (Fedora 39+, Ubuntu 24.04+).";
    }
    return c;
}

Check SystemCheck::pluginFolder() const
{
    Check c { "vst3", "Plugins", "Plugin folder", {}, Check::Status::ok, {}, {} };
    if (! paths.vst3.isDirectory())
    {
        c.status = Check::Status::info;
        c.detail = paths.vst3.getFullPathName() + " does not exist yet; installing a plugin creates it.";
        return c;
    }
    if (! paths.vst3.hasWriteAccess())
    {
        c.status = Check::Status::problem;
        c.detail = "The Dojo cannot write to " + paths.vst3.getFullPathName() + ".";
        return c;
    }
    juce::StringArray broken;
    for (const auto& f : paths.vst3.findChildFiles (juce::File::findFilesAndDirectories, false))
        if (isBrokenLink (f))
            broken.add (f.getFileName());
    // findChildFiles skips links to nothing on some systems: look at the directory entries too
    if (auto* d = opendir (paths.vst3.getFullPathName().toRawUTF8()))
    {
        while (auto* e = readdir (d))
        {
            const juce::String name (e->d_name);
            if (name != "." && name != ".." && isBrokenLink (paths.vst3.getChildFile (name)))
                broken.addIfNotAlreadyThere (name);
        }
        closedir (d);
    }
    const int bundles = paths.vst3.findChildFiles (juce::File::findDirectories, false, "*.vst3").size();
    c.detail = paths.vst3.getFullPathName() + " holds " + juce::String (bundles) + " VST3 plugins.";
    if (! broken.isEmpty())
    {
        c.status = Check::Status::warning;
        c.detail << " " << broken.size() << (broken.size() == 1 ? " entry points" : " entries point")
                 << " to plugins that no longer exist (" << broken.joinIntoString (", ").substring (0, 160)
                 << "); DAWs try to load them on every scan.";
        Fix f;
        f.kind = Fix::Kind::parkBrokenLinks;
        f.label = "Move them aside";
        f.confirm = "Move " + juce::String (broken.size()) + " broken links from " + paths.vst3.getFullPathName() + " to "
                    + paths.home.getChildFile (".vst3-disabled").getFullPathName() + ". Nothing is deleted.";
        f.undo = "Move them back from ~/.vst3-disabled.";
        c.fixes.push_back (f);
    }
    for (const auto& conflict : Suite (paths).identityConflicts())
    {
        c.status = Check::Status::warning;
        c.detail << " " << conflict;
    }
    return c;
}

std::vector<Check> SystemCheck::plugins() const
{
    Check c { "libs", "Plugins", "Axell plugins", {}, Check::Status::ok, {}, {} };
    Suite suite (paths);
    int installed = 0;
    juce::StringArray problems;
    bool lddMissing = false;
    for (const auto& p : Catalog::products())
    {
        const auto in = suite.installed (p);
        if (! in.present)
            continue;
        ++installed;
        bool ran = false;
        const auto missing = Suite::missingLibraries (in.bundle, ran);
        lddMissing |= ! ran;
        if (! missing.isEmpty())
            problems.add (p.name + " needs " + missing.joinIntoString (", "));
    }
    c.detail = juce::String (installed) + " of " + juce::String ((int) Catalog::products().size()) + " Axell plugins installed";
    if (! problems.isEmpty())
    {
        c.status = Check::Status::problem;
        c.detail << ". Missing libraries: " << problems.joinIntoString ("; ") << ".";
    }
    else if (installed > 0 && lddMissing)
    {
        c.status = Check::Status::warning;
        c.detail << ", but their libraries could not be checked (ldd is missing or failed).";
    }
    else
        c.detail << ", every library they need is present.";
    if (installed == 0)
        c.status = Check::Status::info;
    return { c };
}

std::vector<Check> SystemCheck::daws() const
{
    std::vector<Check> out;
    const auto reaper = findReaper (paths);
    if (reaper.existsAsFile())
    {
        Check c { "daw-reaper", "DAW", "REAPER", {}, Check::Status::ok, {}, {} };
        const auto rev = paths.reaperConfig().getChildFile ("reaper-install-rev.txt").loadFileAsString().trim();
        c.detail = "REAPER " + (rev.isNotEmpty() ? rev + " " : juce::String()) + "at " + reaper.getFullPathName()
                   + " (native: it sees ~/.vst3 directly).";
        out.push_back (c);
    }
    if (Process::which ("flatpak") != juce::File())
    {
        const auto list = Process::run ({ "flatpak", "list", "--app", "--columns=application" }, 15000);
        for (const auto& daw : kFlatpakDaws)
        {
            if (! list.output.contains (daw[0]))
                continue;
            Check c { juce::String ("daw-") + daw[0], "DAW", juce::String (daw[1]) + " (Flatpak)", {}, Check::Status::ok, {}, {} };
            const auto perms = Process::run ({ "flatpak", "info", "--show-permissions", daw[0] }, 15000).output
                               + Process::run ({ "flatpak", "override", "--user", "--show", daw[0] }, 15000).output;
            juce::StringArray fsys;
            for (auto line : juce::StringArray::fromLines (perms))
                if (line.trim().startsWith ("filesystems="))
                    fsys.addTokens (line.fromFirstOccurrenceOf ("=", false, false), ";", "");
            const bool sees = fsys.contains ("home") || fsys.contains ("host") || fsys.contains ("~/.vst3")
                              || fsys.contains ("~/.vst3:ro") || fsys.contains ("home:ro") || fsys.contains ("host:ro");
            if (sees)
                c.detail = "Installed as a Flatpak and allowed to read ~/.vst3.";
            else
            {
                c.status = Check::Status::warning;
                c.detail = "Installed as a Flatpak: its sandbox cannot see ~/.vst3, so it will not find the Axell plugins.";
                Fix f;
                f.kind = Fix::Kind::command;
                f.label = "Grant access";
                f.command = { "flatpak", "override", "--user", "--filesystem=~/.vst3:ro", daw[0] };
                f.confirm = "Run: flatpak override --user --filesystem=~/.vst3:ro " + juce::String (daw[0])
                            + "\nThis lets " + daw[1] + " read (not change) your plugin folder. It changes nothing outside your account.";
                f.undo = "flatpak override --user --nofilesystem=~/.vst3 " + juce::String (daw[0]);
                c.fixes.push_back (f);
            }
            out.push_back (c);
        }
    }
    if (out.empty())
        out.push_back ({ "daw-none", "DAW", "DAW", "No DAW found (REAPER native or a Flatpak DAW). Any VST3 host that scans ~/.vst3 works.",
                         Check::Status::info, {}, {} });
    return out;
}

Check SystemCheck::reaperPath() const
{
    Check c { "reaper-path", "DAW", "REAPER plugin folders", {}, Check::Status::ok, {}, {} };
    if (! reaperIni().existsAsFile())
    {
        c.status = Check::Status::info;
        c.detail = "REAPER has not been set up in this account yet; its default folders include ~/.vst3.";
        return c;
    }
    const auto list = parseVstPath (reaperIni().loadFileAsString());
    if (list.isEmpty())
    {
        c.detail = "REAPER uses its default plugin folders, which include ~/.vst3.";
        return c;
    }
    bool has = false;
    juce::StringArray stale;
    for (const auto& e : list)
    {
        has |= sameFolder (e, paths.vst3, paths.home);
        auto t = e.trim();
        if (t.startsWith ("~"))
            t = paths.home.getFullPathName() + t.substring (1);
        if (t.isNotEmpty() && juce::File::isAbsolutePath (t) && ! juce::File (t).isDirectory())
            stale.add (e.trim());
        else if (t.isNotEmpty() && juce::File::isAbsolutePath (t) && juce::File (t).isDirectory() && ! sameFolder (t, paths.vst3, paths.home)
                 && juce::File (t).findChildFiles (juce::File::findFilesAndDirectories, true, "*.vst3").isEmpty()
                 && juce::File (t).findChildFiles (juce::File::findFilesAndDirectories, true, "*.so").isEmpty())
            stale.add (e.trim());
    }
    if (has)
        c.detail = "REAPER scans ~/.vst3, where the Axell plugins live.";
    else
    {
        c.status = Check::Status::problem;
        c.detail = "REAPER does not scan ~/.vst3, so it cannot see the Axell plugins.";
        Fix f;
        f.kind = Fix::Kind::reaperVstPath;
        f.label = "Add ~/.vst3";
        f.confirm = "Close REAPER first. The Dojo then adds " + paths.vst3.getFullPathName()
                    + " to REAPER's plugin folders in reaper.ini, after saving a copy of the file next to it.";
        f.undo = "The previous reaper.ini is kept as reaper.ini.dojo-backup-<date>.";
        c.fixes.push_back (f);
        c.manual = "Or in REAPER: Options > Preferences > Plug-ins > VST, add " + paths.vst3.getFullPathName() + ", then Re-scan.";
    }
    if (! stale.isEmpty())
    {
        if (c.status == Check::Status::ok)
            c.status = Check::Status::info;
        c.detail << " REAPER also scans " << stale.size() << (stale.size() == 1 ? " folder that is" : " folders that are")
                 << " empty or gone (" << stale.joinIntoString ("; ").substring (0, 140)
                 << "); removing it in Options > Preferences > Plug-ins > VST makes scans faster.";
    }
    return c;
}

Check SystemCheck::reaperScan() const
{
    Check c { "reaper-scan", "DAW", "REAPER knows the plugins", {}, Check::Status::ok, {}, {} };
    const auto cache = paths.reaperConfig().getChildFile ("reaper-vstplugins64.ini");
    if (! cache.existsAsFile())
    {
        c.status = Check::Status::info;
        c.detail = "REAPER has not scanned plugins in this account yet.";
        return c;
    }
    const auto text = cache.loadFileAsString();
    Suite suite (paths);
    juce::StringArray missing, outdated;
    for (const auto& p : Catalog::products())
    {
        const auto in = suite.installed (p);
        if (! in.present)
            continue;
        if (! text.contains (reaperCacheKey (p.bundle) + "="))
            missing.add (p.name);
    }
    if (missing.isEmpty())
        c.detail = "REAPER has scanned every installed Axell plugin.";
    else
    {
        c.status = Check::Status::warning;
        c.detail = "REAPER has not scanned " + missing.joinIntoString (", ") + " yet.";
        c.manual = "Restart REAPER (it scans new plugins when it starts), or Options > Preferences > Plug-ins > VST > Re-scan.";
    }
    return c;
}

Check SystemCheck::audioServer() const
{
    Check c { "audio", "Audio", "Audio buffer", {}, Check::Status::ok, {}, {} };
    const auto r = Process::run ({ "pw-metadata", "-n", "settings" }, 5000);
    const auto [rate, quantum] = parsePipewire (r.output);
    if (! r.ok() || rate <= 0)
    {
        c.status = Check::Status::info;
        c.detail = "PipeWire's settings could not be read; the DAW's own audio settings decide the buffer.";
        return c;
    }
    const double ms = 1000.0 * quantum / rate;
    const bool dropIn = paths.pipewireDropIns().getChildFile ("50-axell-signature-dojo.conf").existsAsFile();
    c.detail = "PipeWire runs at " + juce::String (rate) + " Hz with a " + juce::String (quantum) + "-sample buffer ("
               + juce::String (ms, 1) + " ms). Mixing is comfortable at 512-1024; recording with live monitoring feels best at 128-256 (3-5 ms)."
               + (dropIn ? " The Dojo's buffer setting is active." : "");
    if (quantum > 512)
        c.status = Check::Status::info;
    for (int q : { 128, 256, 512 })
    {
        if (q == quantum)
            continue;
        Fix now;
        now.kind = Fix::Kind::command;
        now.label = juce::String (q) + " now";
        now.command = { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", juce::String (q) };
        now.confirm = "Set the audio buffer to " + juce::String (q) + " samples (" + juce::String (1000.0 * q / rate, 1)
                      + " ms) until you log out. Smaller buffers need more CPU: if you hear crackles, go back to 512.";
        now.undo = "Choose \"Automatic\" or log out.";
        c.fixes.push_back (now);
    }
    Fix keep;
    keep.kind = Fix::Kind::pipewireQuantum;
    keep.value = 256;
    keep.persistent = true;
    keep.label = "256 always";
    keep.confirm = "Write " + paths.pipewireDropIns().getChildFile ("50-axell-signature-dojo.conf").getFullPathName()
                   + " so PipeWire uses a 256-sample buffer after every restart, and apply it now. Only your account is affected.";
    keep.undo = "Choose \"Automatic\": the Dojo removes the file.";
    c.fixes.push_back (keep);
    Fix autoQ;
    autoQ.kind = Fix::Kind::pipewireQuantum;
    autoQ.value = 0;
    autoQ.label = "Automatic";
    autoQ.confirm = "Let PipeWire choose the buffer again (and remove the Dojo's setting file, if any).";
    c.fixes.push_back (autoQ);
    return c;
}

Check SystemCheck::realtime() const
{
    Check c { "realtime", "Audio", "Realtime priority", {}, Check::Status::ok, {}, {} };
    rlimit rl {};
    ::getrlimit (RLIMIT_RTPRIO, &rl);
    const bool rtkit = Process::isRunning ("rtkit-daemon");
    if (rtkit)
        c.detail = "PipeWire gets realtime priority through RealtimeKit, so audio keeps running when the computer is busy.";
    else if (rl.rlim_cur > 0)
        c.detail = "Your account may use realtime priority (up to " + juce::String ((int) rl.rlim_cur) + ").";
    else
    {
        c.status = Check::Status::warning;
        c.detail = "Neither RealtimeKit nor a realtime limit is available: audio may drop out under load.";
        c.manual = "This needs an administrator: enable the rtkit service, or give your account realtime priority (a 'realtime' or "
                   "'audio' group with an rtprio limit). The Dojo does not change system settings.";
    }
    return c;
}

Check SystemCheck::cpu() const
{
    Check c { "cpu", "Computer", "CPU speed", {}, Check::Status::ok, {}, {} };
    const auto gov = juce::File ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").loadFileAsString().trim();
    if (gov.isEmpty())
    {
        c.status = Check::Status::info;
        c.detail = "The CPU frequency policy could not be read.";
    }
    else if (gov == "powersave")
    {
        c.status = Check::Status::warning;
        c.detail = "The CPU runs in power-saving mode: heavy sessions may crackle at small buffers.";
        c.manual = "Switch the power profile to Performance (system settings or the battery menu) while recording.";
    }
    else
        c.detail = "The CPU frequency policy is \"" + gov + "\".";
    return c;
}

Check SystemCheck::effects() const
{
    Check c { "effects", "Audio", "System audio effects", {}, Check::Status::ok, {}, {} };
    if (Process::isRunning ("easyeffects"))
    {
        c.status = Check::Status::warning;
        c.detail = "EasyEffects is running: it processes what you hear (and may add latency), so your mix is not what listeners get.";
        c.manual = "Turn EasyEffects off (or bypass it) while recording and mixing.";
    }
    else
        c.detail = "No system-wide audio effects are running.";
    return c;
}

Check SystemCheck::disk() const
{
    Check c { "disk", "Computer", "Free space", {}, Check::Status::ok, {}, {} };
    struct statvfs s {};
    if (::statvfs (paths.home.getFullPathName().toRawUTF8(), &s) == 0)
    {
        const double gb = (double) s.f_bavail * (double) s.f_frsize / 1.0e9;
        c.detail = juce::String (gb, 1) + " GB free in your home folder.";
        if (gb < 1.0)
        {
            c.status = Check::Status::warning;
            c.detail << " Recording and installing need some room.";
        }
    }
    return c;
}

std::vector<Check> SystemCheck::runAll() const
{
    std::vector<Check> out { system(), cpu(), disk(), pluginFolder() };
    for (auto& c : plugins())
        out.push_back (c);
    for (auto& c : daws())
        out.push_back (c);
    out.push_back (reaperPath());
    out.push_back (reaperScan());
    out.push_back (audioServer());
    out.push_back (realtime());
    out.push_back (effects());
    return out;
}

//==============================================================================
StepResult SystemCheck::apply (const Fix& f) const
{
    switch (f.kind)
    {
        case Fix::Kind::none:
            return { false, "Nothing to do." };
        case Fix::Kind::command:
        {
            // a sandbox (tests, AXELL_DOJO_SANDBOX) never changes the live session or system: it only says what it would do
            if (paths.isSandbox)
                return { true, "Sandbox: would run " + f.command.joinIntoString (" ") };
            const auto r = Process::run (f.command, 30000);
            if (! r.started)
                return { false, f.command[0] + " is not available." };
            return { r.ok(), r.ok() ? juce::String ("Done.") : "It did not work: " + r.output.trim().substring (0, 200) };
        }
        case Fix::Kind::pipewireQuantum:
        {
            const auto file = paths.pipewireDropIns().getChildFile ("50-axell-signature-dojo.conf");
            auto setLive = [this] (int q)
            {
                if (! paths.isSandbox)
                    Process::run ({ "pw-metadata", "-n", "settings", "0", "clock.force-quantum", juce::String (q) }, 5000);
            };
            if (f.value <= 0)
            {
                file.deleteFile();
                setLive (0);
                return { true, "PipeWire chooses the buffer again." };
            }
            if (f.persistent && (! paths.pipewireDropIns().createDirectory() || ! file.replaceWithText (pipewireDropIn (f.value), false, false, "\n")))
                return { false, "Cannot write " + file.getFullPathName() };
            setLive (f.value);
            return { true, "The buffer is " + juce::String (f.value) + " samples" + (f.persistent ? ", also after a restart." : ".") };
        }
        case Fix::Kind::reaperVstPath:
        {
            if (Process::isRunning ("reaper"))
                return { false, "REAPER is running. Close it first: it rewrites its settings when it quits." };
            const auto ini = reaperIni();
            const auto before = ini.loadFileAsString();
            const auto after = withVstPathAdded (before, paths.vst3.getFullPathName());
            if (after == before)
                return { true, "REAPER already uses its default folders, which include ~/.vst3." };
            const auto backup = ini.getSiblingFile ("reaper.ini.dojo-backup-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"));
            if (! ini.copyFileTo (backup))
                return { false, "Could not save a copy of reaper.ini first; nothing changed." };
            const auto tmp = ini.getSiblingFile ("reaper.ini.dojo-tmp");
            if (! tmp.replaceWithText (after, false, false, nullptr) || ! tmp.moveFileTo (ini))
            {
                tmp.deleteFile();
                return { false, "Could not write reaper.ini; nothing changed." };
            }
            return { true, "REAPER will scan ~/.vst3 next time it starts. The previous settings are in " + backup.getFileName() + "." };
        }
        case Fix::Kind::parkBrokenLinks:
        {
            const auto dest = paths.home.getChildFile (".vst3-disabled").getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + "-broken-links");
            if (! dest.createDirectory())
                return { false, "Cannot create " + dest.getFullPathName() };
            int moved = 0;
            if (auto* d = opendir (paths.vst3.getFullPathName().toRawUTF8()))
            {
                juce::StringArray names;
                while (auto* e = readdir (d))
                    names.add (e->d_name);
                closedir (d);
                for (auto& n : names)
                {
                    const auto src = paths.vst3.getChildFile (n);
                    if (n != "." && n != ".." && isBrokenLink (src) && ! dest.getChildFile (n).exists()
                        && ::rename (src.getFullPathName().toRawUTF8(), dest.getChildFile (n).getFullPathName().toRawUTF8()) == 0)
                        ++moved;
                }
            }
            return { moved > 0, juce::String (moved) + " broken links moved to " + dest.getFullPathName() + "." };
        }
    }
    return { false, {} };
}
} // namespace dojo
