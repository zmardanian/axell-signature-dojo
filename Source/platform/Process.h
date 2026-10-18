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
    // is a process with this exact executable name running (from /proc, no pgrep)
    static bool isRunning (const juce::String& exeName);
};
} // namespace dojo
