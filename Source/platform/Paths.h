#pragma once
// Axell Signature Dojo - every place the Dojo reads or writes, in one value. The app uses the user's real folders
// (XDG base directories, ~/.vst3, REAPER's config); the tests and AXELL_DOJO_SANDBOX=<dir> root all of them in one
// folder, so nothing a test does can touch the user's plugins, presets or REAPER.
#include <juce_core/juce_core.h>

namespace dojo
{
struct Paths
{
    juce::File home, vst3, dataHome, configHome, cacheHome;

    juce::File dojoData() const { return dataHome.getChildFile ("AxellSignature").getChildFile ("Dojo"); }
    juce::File backups() const { return dojoData().getChildFile ("backups"); }
    juce::File staging() const { return dojoData().getChildFile ("staging"); }
    juce::File profiles() const { return dojoData().getChildFile ("profiles"); }
    juce::File takes() const { return dojoData().getChildFile ("takes"); }
    juce::File downloads() const { return cacheHome.getChildFile ("AxellSignature").getChildFile ("downloads"); }
    juce::File settingsFile() const { return configHome.getChildFile ("AxellSignature").getChildFile ("dojo.settings"); }
    juce::File presetFolder (const juce::String& pluginFolder) const { return dataHome.getChildFile (pluginFolder).getChildFile ("Presets"); }
    juce::File reaperConfig() const { return configHome.getChildFile ("REAPER"); }
    juce::File reaperChains() const { return reaperConfig().getChildFile ("FXChains").getChildFile ("Axell Signature"); }
    juce::File pipewireDropIns() const { return configHome.getChildFile ("pipewire").getChildFile ("pipewire.conf.d"); }

    // the user's folders (XDG_DATA_HOME, XDG_CONFIG_HOME, XDG_CACHE_HOME honoured), or a sandbox when
    // AXELL_DOJO_SANDBOX is set
    static Paths fromEnvironment();
    // everything below `root` (root acts as the home folder)
    static Paths sandbox (const juce::File& root);

    bool isSandbox = false;
};
} // namespace dojo
