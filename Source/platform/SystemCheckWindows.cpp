// Axell Signature Dojo - the Workstation checks on Windows. Every check reads only; a fix runs after the user
// confirms it (D5). The Linux-only checks (PipeWire, realtime limits, flatpak DAWs, the UAD bridge) have no Windows
// counterpart here: on Windows the DAW's ASIO driver sets the buffer, and Universal Audio's own software runs UADx.
#include "SystemCheck.h"
#include "Process.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

namespace dojo
{
namespace
{
// REAPER's plugin cache names a bundle with every character but letters, digits and '.' turned into '_'
juce::String reaperCacheKey (const juce::String& bundle)
{
    juce::String k;
    for (auto ch : bundle)
        k += (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == '.') ? juce::String::charToString (ch) : juce::String ("_");
    return k;
}

// %VAR% references in a REAPER path entry, expanded as Windows does
juce::String expand (const juce::String& s)
{
    wchar_t out[4096];
    const auto n = ExpandEnvironmentStringsW (s.toWideCharPointer(), out, (DWORD) std::size (out));
    return n > 0 && n <= std::size (out) ? juce::String (out) : s;
}

bool sameFolder (const juce::String& a, const juce::File& b)
{
    const auto t = expand (a.trim()).trimCharactersAtEnd ("\\/");
    return juce::File::isAbsolutePath (t) && juce::File (t).getFullPathName().equalsIgnoreCase (b.getFullPathName());
}

// the names of the subkeys of HKLM\<path> (ASIO drivers register there)
juce::StringArray subkeys (const wchar_t* path)
{
    juce::StringArray out;
    HKEY k = nullptr;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return out;
    wchar_t name[256];
    for (DWORD i = 0;; ++i)
    {
        DWORD len = (DWORD) std::size (name);
        if (RegEnumKeyExW (k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        out.add (juce::String (name, (size_t) len));
    }
    RegCloseKey (k);
    return out;
}

const char* kHighPerformance = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";   // Windows' built-in power plans
const char* kPowerSaver = "a1841308-3541-4fab-bc81-f71556f20b4a";
} // namespace

juce::File SystemCheck::findReaper (const Paths& paths)
{
    juce::ignoreUnused (paths);
    juce::Array<juce::File> places;
    for (auto var : { "ProgramW6432", "ProgramFiles" })
    {
        const auto pf = juce::SystemStats::getEnvironmentVariable (var, {});
        if (pf.isNotEmpty())
        {
            places.add (juce::File (pf).getChildFile ("REAPER (x64)\\reaper.exe"));
            places.add (juce::File (pf).getChildFile ("REAPER\\reaper.exe"));
        }
    }
    for (const auto& f : places)
        if (f.existsAsFile())
            return f;
    return Process::which ("reaper");
}

//==============================================================================
Check SystemCheck::system() const
{
    Check c { "system", "Computer", "System", {}, Check::Status::ok, {}, {} };
    c.detail << juce::SystemStats::getOperatingSystemName() << ", " << juce::SystemStats::getNumCpus() << " CPU threads, "
             << juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0, 1) << " GB memory.";
    if (! juce::SystemStats::isOperatingSystem64Bit())
    {
        c.status = Check::Status::problem;
        c.detail << " The Axell plugins are 64-bit; this Windows is 32-bit.";
    }
    else if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) != 0
             && juce::SystemStats::getOperatingSystemType() < juce::SystemStats::Windows10)
    {
        c.status = Check::Status::warning;
        c.detail << " The Axell plugins are built and tested for Windows 10 and 11.";
    }
    return c;
}

Check SystemCheck::cpu() const
{
    Check c { "cpu", "Computer", "Processor", {}, Check::Status::ok, {}, {} };
    c.detail = juce::SystemStats::getCpuModel().trim();
    if (! juce::SystemStats::hasSSE41())
    {
        c.status = Check::Status::warning;
        c.detail << " - an older processor: heavy chains may need a larger audio buffer.";
    }
    return c;
}

Check SystemCheck::disk() const
{
    Check c { "disk", "Computer", "Free space", {}, Check::Status::ok, {}, {} };
    const double gb = (double) paths.home.getBytesFreeOnVolume() / 1.0e9;
    c.detail = juce::String (gb, 1) + " GB free on the drive with your user folder.";
    if (gb < 1.0)
    {
        c.status = Check::Status::warning;
        c.detail << " Recording and installing need some room.";
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
    const int bundles = paths.vst3.findChildFiles (juce::File::findDirectories, false, "*.vst3").size();
    c.detail = paths.vst3.getFullPathName() + " holds " + juce::String (bundles) + " VST3 plugins.";
    if (! Suite (paths).canWritePluginFolder())
        c.detail << " Changing plugins there needs administrator permission: Windows asks for it when you install or update.";
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
    juce::StringArray broken;
    for (const auto& p : Catalog::products())
    {
        const auto in = suite.installed (p);
        if (! in.present)
            continue;
        ++installed;
        bool ran = false;
        Suite::missingLibraries (in.bundle, ran);   // on Windows: is the plugin binary there
        if (! ran)
            broken.add (p.name);
    }
    c.detail = juce::String (installed) + " of " + juce::String ((int) Catalog::products().size()) + " Axell plugins installed";
    if (! broken.isEmpty())
    {
        c.status = Check::Status::problem;
        c.detail << ". " << broken.joinIntoString (", ") << (broken.size() == 1 ? " has" : " have")
                 << " no Windows plugin binary in its bundle: reinstall from the Plugins page.";
    }
    else
        c.detail << ".";
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
        c.detail = "REAPER at " + reaper.getFullPathName() + ".";
        Fix f;
        f.kind = Fix::Kind::launch;
        f.label = "Open REAPER";
        f.confirm = "Open REAPER.";
        f.command = { reaper.getFullPathName() };
        c.fixes.push_back (f);
        out.push_back (c);
    }
    else
        out.push_back ({ "daw-none", "DAW", "DAW", "REAPER was not found in Program Files. The Axell plugins work in any VST3 "
                         "host; the Dojo's chains and project templates are for REAPER (reaper.fm).", Check::Status::info, {}, {} });
    return out;
}

Check SystemCheck::reaperPath() const
{
    Check c { "reaper-path", "DAW", "REAPER plugin folders", {}, Check::Status::ok, {}, {} };
    const auto common = paths.vst3.getFullPathName();
    if (! reaperIni().existsAsFile())
    {
        c.status = Check::Status::info;
        c.detail = "REAPER has not been set up in this account yet; its default folders include " + common + ".";
        return c;
    }
    const auto list = parseVstPath (reaperIni().loadFileAsString(), "vstpath64");
    if (list.isEmpty())
    {
        c.detail = "REAPER uses its default plugin folders, which include " + common + ".";
        return c;
    }
    bool has = false;
    for (const auto& e : list)
        has |= sameFolder (e, paths.vst3);
    if (has)
        c.detail = "REAPER scans " + common + ", where the Axell plugins live.";
    else
    {
        c.status = Check::Status::problem;
        c.detail = "REAPER does not scan " + common + ", so it cannot see the Axell plugins.";
        Fix f;
        f.kind = Fix::Kind::reaperVstPath;
        f.label = "Add the VST3 folder";
        f.confirm = "Close REAPER first. The Dojo then adds " + common
                    + " to REAPER's plugin folders in reaper.ini, after saving a copy of the file next to it.";
        f.undo = "The previous reaper.ini is kept as reaper.ini.dojo-backup-<date>.";
        c.fixes.push_back (f);
        c.manual = "Or in REAPER: Options > Preferences > Plug-ins > VST, add " + common + ", then Re-scan.";
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
    juce::StringArray missing;
    for (const auto& p : Catalog::products())
        if (suite.installed (p).present && ! text.contains (reaperCacheKey (p.bundle) + "="))
            missing.add (p.name);
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
    Check c { "audio", "Audio", "ASIO driver", {}, Check::Status::ok, {}, {} };
    const auto drivers = subkeys (L"SOFTWARE\\ASIO");
    if (drivers.isEmpty())
    {
        c.status = Check::Status::info;
        c.detail = "No ASIO driver is installed. REAPER works with WASAPI; for the lowest latency while recording, install "
                   "your audio interface's ASIO driver.";
    }
    else
        c.detail = "ASIO " + juce::String (drivers.size() == 1 ? "driver" : "drivers") + ": " + drivers.joinIntoString (", ")
                   + ". Choose it in REAPER: Options > Preferences > Audio > Device.";
    return c;
}

Check SystemCheck::realtime() const
{
    Check c { "realtime", "Computer", "Power plan", {}, Check::Status::ok, {}, {} };
    const auto r = Process::run ({ "powercfg", "/getactivescheme" }, 5000);
    if (! r.ok())
    {
        c.status = Check::Status::info;
        c.detail = "The power plan could not be read.";
        return c;
    }
    const auto line = r.output.trim();
    const auto name = line.fromFirstOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false);
    c.detail = "Power plan: " + (name.isNotEmpty() ? name : line.substring (0, 80)) + ".";
    if (line.containsIgnoreCase (kPowerSaver))
    {
        c.status = Check::Status::warning;
        c.detail << " Power saver slows the processor down and causes audio dropouts at small buffers.";
        Fix f;
        f.kind = Fix::Kind::command;
        f.label = "Use High performance";
        f.confirm = "Switch Windows to its High performance power plan (powercfg /setactive).";
        f.command = { "powercfg", "/setactive", kHighPerformance };
        f.undo = "Settings > System > Power, or Control Panel > Power Options.";
        c.fixes.push_back (f);
    }
    return c;
}

Check SystemCheck::effects() const
{
    return {};   // Linux only (system-wide effects on PipeWire)
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
            if (paths.isSandbox)
                return { true, "Sandbox: would run " + f.command.joinIntoString (" ") };
            const auto r = Process::run (f.command, f.timeoutMs);
            if (! r.started)
                return { false, f.command[0] + " is not available." };
            return { r.ok(), r.ok() ? juce::String ("Done.") : "It did not work: " + r.output.trim().substring (0, 200) };
        }
        case Fix::Kind::launch:
        {
            if (paths.isSandbox)
                return { true, "Sandbox: would open " + f.command.joinIntoString (" ") };
            // started on its own through the shell (detached, nothing captured): the Dojo does not wait
            juce::StringArray rest (f.command);
            rest.remove (0);
            if (! juce::File (f.command[0]).startAsProcess (rest.joinIntoString (" ")))
                return { false, f.command[0] + " could not be started." };
            return { true, "Opening " + juce::File (f.command[0]).getFileName() + "..." };
        }
        case Fix::Kind::reaperVstPath:
        {
            if (Process::isRunning ("reaper"))
                return { false, "REAPER is running. Close it first: it rewrites its settings when it quits." };
            const auto ini = reaperIni();
            const auto before = ini.loadFileAsString();
            const auto after = withVstPathAdded (before, paths.vst3.getFullPathName(), "vstpath64");
            if (after == before)
                return { true, "REAPER already uses its default folders, which include " + paths.vst3.getFullPathName() + "." };
            const auto backup = ini.getSiblingFile ("reaper.ini.dojo-backup-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"));
            if (! ini.copyFileTo (backup))
                return { false, "Could not save a copy of reaper.ini first; nothing changed." };
            const auto tmp = ini.getSiblingFile ("reaper.ini.dojo-tmp");
            if (! tmp.replaceWithText (after, false, false, nullptr) || ! tmp.moveFileTo (ini))
            {
                tmp.deleteFile();
                return { false, "Could not write reaper.ini; nothing changed." };
            }
            return { true, "REAPER will scan " + paths.vst3.getFullPathName() + " next time it starts. The previous settings are in "
                           + backup.getFileName() + "." };
        }
        case Fix::Kind::pipewireQuantum:
        case Fix::Kind::parkBrokenLinks:
            return { false, "Not available on Windows." };
    }
    return { false, {} };
}
} // namespace dojo
#endif
