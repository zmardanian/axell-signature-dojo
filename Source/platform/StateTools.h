#pragma once
// Axell Signature Dojo - turning a plugin's saved state into its own user preset file and into a REAPER FX chain
// entry. A preset is written exactly as the plugin's SAVE button writes it (the parameter tree, its name and
// description), from the state the hosted plugin itself produced; nothing is reconstructed by hand.
#include "Catalog.h"
#include "Paths.h"
#include "PluginHost.h"
#include "../model/ReaperChunk.h"

#include <juce_data_structures/juce_data_structures.h>

namespace dojo::state
{
// the plugin's parameter tree inside a VST3 component stream (JUCE's wrapper puts the plugin's own state first:
// "VC2!" XML for most Axell plugins, Penta EQ's 'PENQ' binary tree)
juce::ValueTree treeFromComponent (const std::vector<uint8_t>& component);

// the same component stream with the plugin's preset name replaced (the plugin then shows that name), everything
// after the plugin's own state - JUCE's wrapper data - kept byte for byte; empty on an unknown format
std::vector<uint8_t> withPresetName (const std::vector<uint8_t>& component, const juce::String& name);

// the preset file content for `product` from a state tree, as the plugin's own SAVE writes it
std::unique_ptr<juce::XmlElement> presetXml (const Product&, juce::ValueTree tree, const juce::String& name,
                                             const juce::String& description);

constexpr const char* kCreatedBy = "createdBy";            // marks a preset the Dojo wrote (it may replace it later)
constexpr const char* kCreatedByValue = "Axell Signature Dojo";

struct WriteResult
{
    bool ok = false;
    juce::File file;
    juce::String message;
};
// never replaces a preset the user saved; replaces one the Dojo wrote before
WriteResult writePreset (const Paths&, const Product&, const juce::XmlElement&, const juce::String& name);

// REAPER's numeric id for a VST3 plugin: from REAPER's own plugin cache when REAPER has scanned it, otherwise the
// value REAPER derives from the class id (docs/DESIGN.md, D4); 0 if unknown
int32_t reaperUid (const Paths&, const Product&, const juce::PluginDescription&);
reaper::Vst3Plugin reaperPlugin (const Product&, const HostedPlugin&, int32_t uid);
juce::String newFxId();   // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
} // namespace dojo::state
