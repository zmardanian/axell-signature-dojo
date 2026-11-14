#include "Process.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shellapi.h>
 #include <tlhelp32.h>
 #include <string>

namespace dojo
{
// Windows: only real executables (.exe) are found, never .bat/.cmd (those would run through cmd.exe, a shell)
juce::File Process::which (const juce::String& program)
{
    auto exe = [] (juce::File f)
    {
        if (! f.hasFileExtension ("exe"))
            f = f.withFileExtension ("exe");
        return f.existsAsFile() ? f : juce::File();
    };
    if (program.containsChar ('\\') || program.containsChar ('/'))
        return juce::File::isAbsolutePath (program) ? exe (juce::File (program)) : juce::File();
    auto dirs = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ";", "\"");
    dirs.insert (0, juce::File::getSpecialLocation (juce::File::windowsSystemDirectory).getFullPathName());
    for (auto& d : dirs)
    {
        d = d.unquoted().trim();
        if (d.isEmpty() || ! juce::File::isAbsolutePath (d))
            continue;   // a relative PATH entry would make the result depend on the current folder
        if (const auto f = exe (juce::File (d).getChildFile (program)); f != juce::File())
            return f;
    }
    return {};
}

// one argument, quoted the way CommandLineToArgvW reads it back (backslashes doubled only before a quote)
static std::wstring quoteArg (const std::wstring& a)
{
    if (! a.empty() && a.find_first_of (L" \t\n\v\"") == std::wstring::npos)
        return a;
    std::wstring r = L"\"";
    for (auto it = a.begin();; ++it)
    {
        size_t n = 0;
        while (it != a.end() && *it == L'\\') { ++it; ++n; }
        if (it == a.end()) { r.append (n * 2, L'\\'); break; }
        if (*it == L'"') { r.append (n * 2 + 1, L'\\'); r.push_back (*it); }
        else { r.append (n, L'\\'); r.push_back (*it); }
    }
    r.push_back (L'"');
    return r;
}

// CreateProcessW with one pipe for stdout + stderr (no console window, no shell), a real timeout, TerminateProcess
// on timeout/cancel
ProcessResult Process::run (const juce::StringArray& args, int timeoutMs, const std::atomic<bool>* cancel,
                            const std::function<void()>& tick)
{
    ProcessResult r;
    if (args.isEmpty())
        return r;
    const auto exe = which (args[0]);
    if (exe == juce::File())
        return r;

    SECURITY_ATTRIBUTES sa { sizeof (sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (! CreatePipe (&rd, &wr, &sa, 0))
        return r;
    SetHandleInformation (rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW (L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    std::wstring cmd = quoteArg (exe.getFullPathName().toWideCharPointer());
    for (int i = 1; i < args.size(); ++i)
        cmd += L" " + quoteArg (args[i].toWideCharPointer());

    STARTUPINFOW si {};
    si.cb = sizeof (si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi {};
    const auto exePath = std::wstring (exe.getFullPathName().toWideCharPointer());
    const bool ok = CreateProcessW (exePath.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle (wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle (nul);
    if (! ok)
    {
        CloseHandle (rd);
        return r;
    }
    r.started = true;

    juce::MemoryOutputStream out;
    const double deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    char buf[8192];
    for (;;)
    {
        if ((cancel != nullptr && cancel->load()) || juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            r.timedOut = true;
            break;
        }
        if (tick)
            tick();
        DWORD avail = 0;
        if (! PeekNamedPipe (rd, nullptr, 0, nullptr, &avail, nullptr))
            break;   // the write end closed: the program (and anything it started with our handles) is done
        if (avail == 0)
        {
            if (WaitForSingleObject (pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                // exited: drain what is left (a child it started may still hold the pipe; do not wait for it)
                while (PeekNamedPipe (rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
                {
                    DWORD n = 0;
                    if (! ReadFile (rd, buf, (DWORD) std::min<size_t> (sizeof (buf), avail), &n, nullptr) || n == 0)
                        break;
                    out.write (buf, n);
                }
                break;
            }
            Sleep (25);
            continue;
        }
        DWORD n = 0;
        if (! ReadFile (rd, buf, (DWORD) std::min<size_t> (sizeof (buf), avail), &n, nullptr) || n == 0)
            break;
        out.write (buf, n);
    }
    CloseHandle (rd);

    if (r.timedOut)
    {
        TerminateProcess (pi.hProcess, 1);
        WaitForSingleObject (pi.hProcess, 5000);
        r.exitCode = -1;
    }
    else
    {
        WaitForSingleObject (pi.hProcess, INFINITE);
        DWORD code = 0;
        r.exitCode = GetExitCodeProcess (pi.hProcess, &code) ? (int) code : -1;
    }
    CloseHandle (pi.hThread);
    CloseHandle (pi.hProcess);
    r.output = out.toUTF8();
    return r;
}

bool Process::isRunning (const juce::String& exeName)
{
    const auto want = exeName.endsWithIgnoreCase (".exe") ? exeName : exeName + ".exe";
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W e {};
    e.dwSize = sizeof (e);
    bool found = false;
    for (BOOL more = Process32FirstW (snap, &e); more && ! found; more = Process32NextW (snap, &e))
        found = want.equalsIgnoreCase (juce::String (e.szExeFile));
    CloseHandle (snap);
    return found;
}

bool Process::launchDetached (const juce::File& program, const juce::StringArray& args)
{
    juce::StringArray quoted;
    for (const auto& a : args)
        quoted.add (juce::String (quoteArg (a.toWideCharPointer()).c_str()));
    return program.startAsProcess (quoted.joinIntoString (" "));
}

const char* Process::reaperPlatform() { return "7.0/win64"; }

bool Process::relaunchElevated (const juce::String& args)
{
    wchar_t self[MAX_PATH * 4];
    const auto n = GetModuleFileNameW (nullptr, self, (DWORD) std::size (self));
    if (n == 0 || n >= std::size (self))
        return false;
    const std::wstring params (args.toWideCharPointer());
    SHELLEXECUTEINFOW sei {};
    sei.cbSize = sizeof (sei);
    sei.lpVerb = L"runas";
    sei.lpFile = self;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW (&sei) != FALSE;   // false when the user says no at the prompt
}

juce::File Process::removableMediaRoot() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory); }
} // namespace dojo

#else

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace dojo
{
juce::File Process::which (const juce::String& program)
{
    auto isExe = [] (const juce::File& f) { return f.existsAsFile() && ::access (f.getFullPathName().toRawUTF8(), X_OK) == 0; };
    if (program.containsChar ('/'))
    {
        juce::File f (program);
        return juce::File::isAbsolutePath (program) && isExe (f) ? f : juce::File();
    }
    auto dirs = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ":", "");
    dirs.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/bin").getFullPathName());
    for (auto extra : { "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin" })
        dirs.add (extra);
    for (auto& d : dirs)
    {
        if (d.isEmpty() || ! juce::File::isAbsolutePath (d))
            continue;   // a relative PATH entry would make the result depend on the current folder
        const auto f = juce::File (d).getChildFile (program);
        if (isExe (f))
            return f;
    }
    return {};
}

// posix_spawn with one pipe for stdout + stderr, poll() for a real timeout, SIGTERM then SIGKILL on timeout/cancel
ProcessResult Process::run (const juce::StringArray& args, int timeoutMs, const std::atomic<bool>* cancel,
                            const std::function<void()>& tick)
{
    ProcessResult r;
    if (args.isEmpty())
        return r;
    const auto exe = which (args[0]);
    if (exe == juce::File())
        return r;

    int fds[2];
    if (::pipe2 (fds, O_CLOEXEC) != 0)
        return r;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init (&fa);
    posix_spawn_file_actions_adddup2 (&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2 (&fa, fds[1], 2);
    posix_spawn_file_actions_addopen (&fa, 0, "/dev/null", O_RDONLY, 0);

    std::vector<std::string> store;
    store.push_back (exe.getFullPathName().toStdString());
    for (int i = 1; i < args.size(); ++i)
        store.push_back (args[i].toStdString());
    std::vector<char*> argv;
    for (auto& s : store)
        argv.push_back (s.data());
    argv.push_back (nullptr);

    pid_t pid = 0;
    const int err = posix_spawn (&pid, store[0].c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy (&fa);
    ::close (fds[1]);
    if (err != 0)
    {
        ::close (fds[0]);
        return r;
    }
    r.started = true;

    juce::MemoryOutputStream out;
    const double deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    bool eof = false;
    char buf[8192];
    while (! eof)
    {
        if ((cancel != nullptr && cancel->load()) || juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            r.timedOut = true;
            break;
        }
        if (tick)
            tick();
        pollfd p { fds[0], POLLIN, 0 };
        const int pr = ::poll (&p, 1, 50);
        if (pr < 0 && errno != EINTR)
            break;
        if (pr > 0)
        {
            const auto n = ::read (fds[0], buf, sizeof (buf));
            if (n > 0)
                out.write (buf, (size_t) n);
            else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN))
                eof = true;
        }
    }
    ::close (fds[0]);

    int status = 0;
    if (r.timedOut)
    {
        ::kill (pid, SIGTERM);
        for (int i = 0; i < 40 && ::waitpid (pid, &status, WNOHANG) == 0; ++i)
            juce::Thread::sleep (25);
        if (::waitpid (pid, &status, WNOHANG) == 0)
        {
            ::kill (pid, SIGKILL);
            ::waitpid (pid, &status, 0);
        }
        r.exitCode = -1;
    }
    else
    {
        while (::waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
        r.exitCode = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    }
    r.output = out.toUTF8();
    return r;
}

bool Process::isRunning (const juce::String& exeName)
{
    const auto want = exeName.substring (0, 15);   // /proc/<pid>/comm holds at most 15 characters
    for (const auto& d : juce::File ("/proc").findChildFiles (juce::File::findDirectories, false))
        if (d.getFileName().containsOnly ("0123456789") && d.getChildFile ("comm").loadFileAsString().trim() == want)
            return true;
    return false;
}

bool Process::launchDetached (const juce::File& program, const juce::StringArray& args)
{
    // started in its own session (setsid -f returns at once), so it keeps running after the Dojo closes
    juce::StringArray a { "setsid", "-f", program.getFullPathName() };
    a.addArray (args);
    return run (a, 10000).ok();
}

const char* Process::reaperPlatform() { return "7.0/linux-x86_64"; }

bool Process::relaunchElevated (const juce::String&) { return false; }   // Linux: the plugin folder is the user's own

juce::File Process::removableMediaRoot() { return juce::File ("/run/media"); }
} // namespace dojo
#endif
