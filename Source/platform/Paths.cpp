#include "Paths.h"

namespace dojo
{
Paths Paths::sandbox (const juce::File& root)
{
    Paths p;
    p.home = root;
    p.vst3 = root.getChildFile (".vst3");
    p.dataHome = root.getChildFile (".local/share");
    p.configHome = root.getChildFile (".config");
    p.cacheHome = root.getChildFile (".cache");
    p.isSandbox = true;
    return p;
}

Paths Paths::fromEnvironment()
{
    const auto sandboxDir = juce::SystemStats::getEnvironmentVariable ("AXELL_DOJO_SANDBOX", {});
    if (sandboxDir.isNotEmpty() && juce::File::isAbsolutePath (sandboxDir))
        return sandbox (juce::File (sandboxDir));

    Paths p;
    p.home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto xdg = [&p] (const char* var, const char* fallback)
    {
        const auto v = juce::SystemStats::getEnvironmentVariable (var, {});
        // the XDG spec: a relative path is invalid and must be ignored
        return v.isNotEmpty() && juce::File::isAbsolutePath (v) ? juce::File (v) : p.home.getChildFile (fallback);
    };
    p.vst3 = p.home.getChildFile (".vst3");
    p.dataHome = xdg ("XDG_DATA_HOME", ".local/share");
    p.configHome = xdg ("XDG_CONFIG_HOME", ".config");
    p.cacheHome = xdg ("XDG_CACHE_HOME", ".cache");
    return p;
}
} // namespace dojo
