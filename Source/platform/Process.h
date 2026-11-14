#pragma once
// Axell Signature Dojo - running system tools (curl, ldd, flatpak, pw-metadata, ...). Arguments are passed as a list
// straight to the program (never through a shell), with a timeout; nothing the Dojo runs can be re-interpreted by a
// shell, whatever a file or a release name contains.
#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace dojo
{
struct ProcessResult
{
    bool started = false, timedOut = false;
    int exitCode = -1;
    juce::String output;           // stdout and stderr together
    bool ok() const noexcept { return started && ! timedOut && exitCode == 0; }
};

struct Process
{
    // the absolute path of `program` on PATH (and the usual system folders), or {} if absent
    static juce::File which (const juce::String& program);
    // runs to completion or until `timeoutMs`; `cancel`, when given, stops it early; `tick` is called about every
    // 50 ms while it runs (progress of a download, ...)
    static ProcessResult run (const juce::StringArray& args, int timeoutMs = 10000, const std::atomic<bool>* cancel = nullptr,
                              const std::function<void()>& tick = {});
    // is a process with this exact executable name running (from /proc, no pgrep; Windows: the process list)
    static bool isRunning (const juce::String& exeName);
    // starts a program that keeps running after the Dojo closes (REAPER with a project); nothing captured, no wait
    static bool launchDetached (const juce::File& program, const juce::StringArray& args);
    // the REAPER project header's platform ("7.0/linux-x86_64", "7.0/win64")
    static const char* reaperPlatform();
    // Windows: starts this program again as administrator (the UAC prompt) with `args`; false if refused or not Windows
    static bool relaunchElevated (const juce::String& args);
    // where file choosers start when looking for a USB stick or another drive
    static juce::File removableMediaRoot();
};
} // namespace dojo
