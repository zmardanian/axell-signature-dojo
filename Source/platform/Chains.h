#pragma once
// Axell Signature Dojo - REAPER FX chains for the suite: ready-made templates built from the plugins' factory
// programs, and personal chains from a Voice Profile. Every entry is the plugin's own state, produced by the
// installed plugin itself, so REAPER opens exactly what the Dojo set up.
#include "Catalog.h"
#include "Paths.h"
#include "PluginHost.h"
#include "Suite.h"

#include <atomic>
#include <functional>
#include <vector>

namespace dojo
{
struct ChainStage
{
    juce::String product;                 // catalog key
    juce::String program;                 // factory program
    std::vector<std::pair<juce::String, juce::String>> settings;   // parameter display name -> text
};

struct ChainTemplate
{
    juce::String id, name, group, description;   // group: "Vocals", "Beats", "Master"
    std::vector<ChainStage> stages;
    static const std::vector<ChainTemplate>& all();
};

// a stage's plugin, configured: ready to render and to be written into a chain
struct BuiltStage
{
    const Product* product = nullptr;
    std::unique_ptr<HostedPlugin> plugin;
    juce::String presetName;              // the name REAPER and the plugin show (the program, or a profile name)
    std::vector<uint8_t> component;       // state with that preset name
    std::vector<uint8_t> controller;
};

class ChainWriter
{
public:
    explicit ChainWriter (Paths p) : paths (std::move (p)) {}

    // loads and configures every stage from the installed plugins; a missing plugin is reported, not skipped
    // silently (the chain is incomplete and is not written)
    std::vector<BuiltStage> build (const ChainTemplate&, juce::StringArray& problems) const;
    // takes each plugin's current state under `presetName` (the plugin will show that name)
    static void capture (BuiltStage&, const juce::String& presetName);

    // the .RfxChain text; and writing it to REAPER's FX chain folder ("Axell Signature/<name>.RfxChain") without
    // ever replacing a file the Dojo did not write
    juce::String chainText (const std::vector<BuiltStage>&) const;
    struct Written
    {
        bool ok = false;
        juce::File file;
        juce::String message;
    };
    Written write (const juce::String& fileName, const juce::String& text) const;
    bool wroteBefore (const juce::File&) const;   // the Dojo wrote this file and it is unchanged since

    // a REAPER project: one track named `trackName`, record-armed on input 1 (monitoring off), holding the chain
    static juce::String projectText (const juce::String& trackName, const juce::String& chainText);
    // writes that project for a saved chain file into ~/Documents/Axell Signature (a new file, never replacing one)
    // and opens REAPER on it; in a sandbox it only writes the project
    StepResult openInReaper (const juce::File& chainFile, const juce::String& trackName, juce::File* projectOut = nullptr) const;

private:
    juce::File ledger() const { return paths.dojoData().getChildFile ("written-files.xml"); }
    void remember (const juce::File&) const;
    Paths paths;
};
} // namespace dojo
