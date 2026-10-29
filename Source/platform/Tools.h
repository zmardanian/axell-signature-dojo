#pragma once
// Axell Signature Dojo - studio tools: open a saved REAPER chain as live plugins, render many takes through it,
// preview a master on the streaming services, and move presets, profiles and chains to another computer.
#include "Chains.h"
#include "ProfileEngine.h"
#include "../model/Streaming.h"

#include <atomic>

namespace dojo::tools
{
//== a saved .RfxChain as live plugins (every Axell plugin in it, with its saved state) ==========================
struct LoadedChain
{
    std::vector<BuiltStage> stages;
    juce::StringArray problems;    // a plugin that is not Axell, not installed, or whose state did not load
    bool ok() const { return problems.isEmpty() && ! stages.empty(); }
};
LoadedChain loadChain (const Paths&, const juce::File& rfxChain, double sampleRate);

//== bring saved chains up to the installed plugins ================================================================
// A chain named like a template is rebuilt from it (the current presets); any other chain (a Voice Profile's, a single
// De-Reverb setting) keeps every setting and gains the current features (currentFeatureSettings). The old file is
// copied to <Dojo data>/chain-backups/<time>/ first. Without `files`: every chain the Dojo wrote and that is unchanged
// since; a file named in `files` is updated even if the Dojo did not write it (the user chose it).
struct ChainUpdate
{
    juce::File file;
    bool ok = false, changed = false;
    juce::String message;
    juce::StringArray changes;
};
std::vector<ChainUpdate> updateSavedChains (const Paths&, const juce::Array<juce::File>& files = {});

//== Batch Render ================================================================================================
struct RenderedFile
{
    juce::File source, output;
    bool ok = false;
    juce::String message;
    float inLufs = -70.0f, outLufs = -70.0f, outTruePeakDbtp = -144.0f;
};
using Progress = std::function<void (float, const juce::String&)>;
// renders each file through the chain into `outFolder` as 24-bit WAV ("<name> - <chain>.wav", never replacing a file)
std::vector<RenderedFile> batchRender (const Paths&, const juce::File& rfxChain, const juce::Array<juce::File>& files,
                                       const juce::File& outFolder, const Progress&, const std::atomic<bool>* cancel);

//== Streaming Preview ===========================================================================================
struct MasterPreview
{
    bool ok = false;
    juce::String error;
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    streaming::Measurement measurement;
    std::vector<streaming::Playback> services;
};
MasterPreview previewMaster (const juce::File&);

//== Backup & Move ===============================================================================================
// everything the user made with the suite: plugin user presets, Voice Profiles, the Axell Signature REAPER chains
struct BackupItem
{
    juce::File file;
    juce::String relative;         // "presets/AxellGate/My Voice.axgpreset", "profiles/...", "chains/..."
};
std::vector<BackupItem> backupItems (const Paths&);
// a folder "Axell Signature Backup <date>" in `destination` with the files and a manifest of their SHA-256
StepResult exportBackup (const Paths&, const juce::File& destination, juce::File* folderOut = nullptr);
// verifies every file against the manifest and copies those that do not exist yet (never replacing one)
StepResult importBackup (const Paths&, const juce::File& backupFolder);
} // namespace dojo::tools
