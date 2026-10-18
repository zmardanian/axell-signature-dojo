#include "Process.h"

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
} // namespace dojo
