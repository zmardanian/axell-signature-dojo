#include "UadBridge.h"
#include "Process.h"

#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dojo
{
namespace
{
constexpr const char* kGroup = "UAD bridge";

// the real path (symlinks such as /home -> /var/home resolved), or {} when it does not exist
juce::String canonical (const juce::String& path)
{
    char buf[PATH_MAX];
    return ::realpath (path.toRawUTF8(), buf) != nullptr ? juce::String (buf) : juce::String();
}

Fix commandFix (const juce::String& label, const juce::String& confirm, juce::StringArray command, int timeoutMs = 30000,
                bool showOutput = true)
{
    Fix f;
    f.kind = Fix::Kind::command;
    f.label = label;
    f.confirm = confirm;
    f.command = std::move (command);
    f.timeoutMs = timeoutMs;
    f.showOutput = showOutput;
    return f;
}
} // namespace

juce::String UadBridge::serverDir (unsigned long long dev, unsigned long long ino, unsigned uid)
{
    return "/tmp/.wine-" + juce::String (uid) + "/server-" + juce::String::toHexString ((juce::int64) dev) + "-"
           + juce::String::toHexString ((juce::int64) ino);
}

juce::String UadBridge::niceName (const juce::String& plugin)
{
    auto words = juce::StringArray::fromTokens (plugin.fromFirstOccurrenceOf ("uaudio_", false, false).replaceCharacter ('_', ' '), " ", {});
    for (auto& w : words)
        w = w.substring (0, 1).toUpperCase() + w.substring (1);
    return words.joinIntoString (" ");
}

UadAcceptance UadBridge::parseAcceptance (const juce::String& latestJson)
{
    UadAcceptance a;
    const auto v = juce::JSON::parse (latestJson);
    a.date = v["date"].toString();
    if (const auto* rows = v["results"].getArray())
        for (const auto& r : *rows)
        {
            ++a.total;
            const auto st = r["status"].toString();
            const auto name = niceName (r["plugin"].toString());
            if (st == "PASS")
                ++a.pass;
            else if (st == "NOT LICENSED")
            {
                ++a.unlicensed;
                a.notLicensed.add (name);
            }
            else
                a.failing.add (name + " (" + st.toLowerCase() + ")");
        }
    return a;
}

bool UadBridge::installed() const
{
    return bin ("uadx-doctor").existsAsFile() && prefix().isDirectory();
}

juce::String UadBridge::foreignServer() const
{
    struct stat st {};
    if (::stat (prefix().getFullPathName().toRawUTF8(), &st) != 0)
        return {};
    const auto dir = serverDir ((unsigned long long) st.st_dev, (unsigned long long) st.st_ino, (unsigned) ::getuid());
    const auto expected = canonical (pinnedWine().getChildFile ("bin/wineserver").getFullPathName());
    DIR* d = ::opendir ("/proc");
    if (d == nullptr)
        return {};
    juce::String foreign;
    while (auto* e = ::readdir (d))
    {
        const juce::String pid (e->d_name);
        if (! pid.containsOnly ("0123456789"))
            continue;
        const auto comm = juce::File ("/proc/" + pid + "/comm").loadFileAsString().trim();
        if (comm != "wineserver" && comm != "wineserver64")
            continue;
        if (canonical ("/proc/" + pid + "/cwd") != dir)
            continue;
        const auto exe = canonical ("/proc/" + pid + "/exe");
        if (exe != expected)
            foreign = exe + " (pid " + pid + ")";
    }
    ::closedir (d);
    return foreign;
}

Check UadBridge::wineOwner() const
{
    Check c { "uad-wine", kGroup, "One Wine for the UAD prefix", {}, Check::Status::ok, {}, {} };
    const auto foreign = foreignServer();
    if (foreign.isEmpty())
    {
        c.detail = "The UAD prefix is run only by its own Wine build (or is idle). Hosts started any way are pinned to it.";
        return c;
    }
    c.status = Check::Status::problem;
    c.detail = "A different Wine is running the UAD prefix: " + foreign + ". Two Wine versions in one prefix break it "
               "(the plugins fail to open; the prefix's files are overwritten back and forth).";
    c.fixes.push_back (commandFix ("Give it back", "Stops only the other Wine on the UAD prefix (it saves its state first), then the UAD "
                                   "Wine takes the prefix back and the result is checked. REAPER must be closed; save your session first.",
                                   { bin ("uadx-repair").getFullPathName(), "--wine" }, 180000));
    return c;
}

Check UadBridge::setup() const
{
    Check c { "uad-setup", kGroup, "Bridge set-up", {}, Check::Status::ok, {}, {} };
    juce::StringArray missing;
    if (! runtime().getChildFile ("yabridge-host.exe").loadFileAsString().contains ("UADX pin"))
        missing.add ("the bridge is not pinned to the UAD Wine");
    if (canonical (paths.home.getChildFile (".local/share/yabridge").getFullPathName()) != canonical (runtime().getFullPathName()))
        missing.add ("the default yabridge location is not the UAD runtime");
    if (! bin ("wine").loadFileAsString().contains ("UADX shim"))
        missing.add ("the wine shim is missing");
    int installedN = 0, bridged = 0;
    for (const auto& d : prefix().getChildFile ("drive_c/Program Files/Common Files/VST3").findChildFiles (juce::File::findDirectories, false, "uaudio_*.vst3"))
    {
        ++installedN;
        if (paths.home.getChildFile (".vst3/yabridge").getChildFile (d.getFileName()).isDirectory())
            ++bridged;
    }
    const bool group = paths.home.getChildFile (".vst3/yabridge/yabridge.toml").loadFileAsString().contains ("group = \"uad\"");
    c.detail = juce::String (bridged) + " of " + juce::String (installedN) + " UADx plugins bridged"
               + (group ? ", sharing one Wine process (about a third of the memory)." : ", one Wine process per instance.");
    if (! missing.isEmpty() || bridged < installedN)
    {
        c.status = Check::Status::warning;
        if (! missing.isEmpty())
            c.detail << " Not as set up: " << missing.joinIntoString ("; ") << ".";
        c.fixes.push_back (commandFix ("Re-link", "Adds the UAD prefix's VST3 folder to yabridge again and syncs the plugin links "
                                       "(uadx-repair). Restart REAPER afterwards so it scans them.", { bin ("uadx-repair").getFullPathName() }, 120000));
    }
    c.fixes.push_back (commandFix ("Full diagnosis", "Runs the toolkit's doctor (it starts the UAD Wine briefly to test it) and shows its verdict.",
                                   { bin ("uadx-doctor").getFullPathName(), "--compare" }, 120000));
    return c;
}

Check UadBridge::plugins() const
{
    Check c { "uad-plugins", kGroup, "UADx plugins tested", {}, Check::Status::info, {}, {} };
    const auto latest = toolkit().getChildFile ("acceptance/latest.json");
    const auto test = commandFix ("Test all again", "Loads every installed UADx plugin through the bridge, one after the other, and checks it "
                                  "processes audio, restores its state and its CPU (a few minutes; no GUI, nothing changed).",
                                  { bin ("uadx-test").getFullPathName() }, 1200000);
    if (! latest.existsAsFile())
    {
        c.detail = "Not tested yet on this machine.";
        c.fixes.push_back (test);
        return c;
    }
    const auto a = parseAcceptance (latest.loadFileAsString());
    c.detail = juce::String (a.pass) + " of " + juce::String (a.total) + " work through the bridge (load, process, state) - tested " + a.date + ".";
    if (! a.notLicensed.isEmpty())
        c.detail << " Not licensed on this account (they pass the audio through untouched): " << a.notLicensed.joinIntoString (", ") << ".";
    if (! a.failing.isEmpty())
    {
        c.detail << " Not passing the offline test: " << a.failing.joinIntoString (", ") << ".";
        c.manual = "Galaxy Tape Echo stops in the offline test host only (at its first parameter, licence valid); it loads and runs in "
                   "REAPER. Re-test after a UA update.";
    }
    c.status = a.failing.isEmpty() ? Check::Status::ok : Check::Status::info;
    c.fixes.push_back (test);
    return c;
}

Check UadBridge::snapshot() const
{
    Check c { "uad-snapshot", kGroup, "Rollback point", {}, Check::Status::ok, {}, {} };
    juce::Time newest;
    juce::String name;
    for (const auto& d : paths.home.getChildFile ("uadx-prefix-backups").findChildFiles (juce::File::findDirectories, false, "wine-uaconnect-*"))
        if (! d.getFileName().contains ("replaced") && d.getLastModificationTime() > newest)
        {
            newest = d.getLastModificationTime();
            name = d.getFileName();
        }
    const auto snap = commandFix ("Snapshot now", "Takes a snapshot of the UAD prefix on this computer (instant, it shares the disk with the "
                                  "prefix). REAPER and UA Connect must be closed. A snapshot is for rolling THIS machine back after a bad "
                                  "update - not for moving an activation elsewhere.",
                                  { bin ("uadx-backup").getFullPathName(), "--note", "from the Dojo" }, 120000);
    if (name.isEmpty())
    {
        c.status = Check::Status::warning;
        c.detail = "No snapshot of the UAD prefix yet: a bad UA update could not be rolled back.";
        c.fixes.push_back (snap);
        return c;
    }
    const int days = (int) ((juce::Time::getCurrentTime() - newest).inDays());
    c.detail = "Newest snapshot: " + name + " (" + (days == 0 ? juce::String ("today") : juce::String (days) + " days ago")
               + "). UA Connect takes one by itself before it starts when the installed UA software changed. Restore with uadx-restore.";
    if (days > 30)
        c.status = Check::Status::warning;
    c.fixes.push_back (snap);
    return c;
}

Check UadBridge::reaper() const
{
    Check c { "uad-reaper", kGroup, "REAPER opens the UAD way", {}, Check::Status::ok, {}, {} };
    const auto entry = paths.dataHome.getChildFile ("applications/cockos-reaper.desktop").loadFileAsString();
    if (entry.contains ("uadx-reaper"))
        c.detail = "The REAPER menu entry starts REAPER through uadx-reaper (the UAD Wine, the guard, the meter pacing).";
    else
    {
        c.status = Check::Status::info;
        c.detail = "The REAPER menu entry does not use uadx-reaper; the bridge's own pin still keeps the UAD Wine right.";
    }
    if (! Process::isRunning ("reaper"))
    {
        Fix f;
        f.kind = Fix::Kind::launch;
        f.label = "Open REAPER";
        f.confirm = "Opens REAPER through uadx-reaper, with the UAD Wine and the guard.";
        f.command = { bin ("uadx-reaper").getFullPathName() };
        c.fixes.push_back (f);
    }
    return c;
}

std::vector<Check> UadBridge::checks() const
{
    if (! installed())
        return {};
    return { wineOwner(), setup(), plugins(), snapshot(), reaper() };
}
} // namespace dojo
