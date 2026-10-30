#pragma once
// Axell Signature Dojo - is this computer ready for the Axell Signature suite? Every check reads only; a fix is
// offered when it can be done in the user's own account, and it runs only after the user confirms it (D5).
// Anything that would need administrator rights (system files, groups, kernel settings) is explained, never run:
// on an immutable system like Bazzite, the system image is not the Dojo's to change.
#include "Paths.h"
#include "Suite.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace dojo
{
struct Fix
{
    enum class Kind { none, command, pipewireQuantum, reaperVstPath, parkBrokenLinks, launch };
    Kind kind = Kind::none;
    juce::String label;              // the button: "Grant access", "Add ~/.vst3"
    juce::String confirm;            // what will happen, in full, shown before it runs
    juce::StringArray command;       // Kind::command: the program and its arguments
    int value = 0;                   // Kind::pipewireQuantum: the buffer size
    bool persistent = false;         // Kind::pipewireQuantum: also after a restart
    juce::String undo;               // how to take it back
    int timeoutMs = 30000;           // Kind::command: how long it may take (a full plugin test takes minutes)
    bool showOutput = false;         // Kind::command: report the program's last line instead of "Done."
};

struct Check
{
    enum class Status { ok, info, warning, problem };
    juce::String id, group, title, detail;
    Status status = Status::info;
    std::vector<Fix> fixes;
    juce::String manual;             // a step only the user (or an administrator) can do
};

class SystemCheck
{
public:
    explicit SystemCheck (Paths p) : paths (std::move (p)) {}
    std::vector<Check> runAll() const;

    // applies a fix the user confirmed; returns what happened
    StepResult apply (const Fix&) const;

    // parts, public for the tests
    static juce::StringArray parseVstPath (const juce::String& reaperIni);
    static juce::String withVstPathAdded (const juce::String& reaperIni, const juce::String& folder);
    static std::pair<int, int> parsePipewire (const juce::String& pwMetadataOutput);   // rate, quantum (0 = unknown)
    static juce::String pipewireDropIn (int quantum);
    // REAPER's program: ~/opt/REAPER, /opt/REAPER, ~/REAPER, or on PATH; {} if absent
    static juce::File findReaper (const Paths&);

    // the sub-checks
    Check system() const;
    Check pluginFolder() const;
    std::vector<Check> plugins() const;
    std::vector<Check> daws() const;
    Check reaperPath() const;
    Check reaperScan() const;
    Check audioServer() const;
    Check realtime() const;
    Check cpu() const;
    Check effects() const;
    Check disk() const;

private:
    juce::File reaperIni() const { return paths.reaperConfig().getChildFile ("reaper.ini"); }
    Paths paths;
};
} // namespace dojo
