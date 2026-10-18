#pragma once
// Axell Signature Dojo - the Axell Signature suite: what each plugin is called, where it is published, where it keeps
// its presets and how it looks on the Dojo's cards.
#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace dojo
{
struct Product
{
    juce::String key;            // "gate"
    juce::String name;           // "Axell Gate"
    juce::String repo;           // GitHub repository under the owner below
    juce::String bundle;         // "Axell Gate.vst3"
    juce::String assetPrefix;    // release archive "AxellGate-<version>-linux-x86_64.tar.gz"
    juce::String presetFolder;   // ~/.local/share/<presetFolder>/Presets ("" = no user presets)
    juce::String presetExt;      // ".axgpreset"
    juce::String manufacturerCode, pluginCode;   // JUCE codes: the VST3 class id is derived from them
    juce::String role;           // one line: what it is for
    juce::String tagline;        // the product's subtitle
    juce::String stage;          // "Vocal", "Mix", "Master"
    juce::Colour accent;
    juce::String image;          // DojoAssets resource name of its screenshot

    juce::String archiveName (const juce::String& version) const { return assetPrefix + "-" + version + "-linux-x86_64.tar.gz"; }
    // the VST3 class id JUCE gives a plugin: 16 fixed hex digits, then the manufacturer and plugin codes as ASCII hex
    juce::String classId() const;
    bool hasUserPresets() const { return presetFolder.isNotEmpty(); }
};

struct Catalog
{
    static constexpr const char* kOwner = "zmardanian";
    static constexpr const char* kVendor = "Axell Yobag";
    static const std::vector<Product>& products();
    static const Product* find (const juce::String& key);
    // a plugin that occupies the same VST3 identity as a suite product (Duo Vocal Comp 1.x -> Axell Duo Compressor)
    struct Legacy { juce::String bundle, replacedBy, name; };
    static const std::vector<Legacy>& legacy();
};
} // namespace dojo
