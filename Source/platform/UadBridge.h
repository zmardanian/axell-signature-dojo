#pragma once
// Axell Signature Dojo - the UAD bridge panel (Workstation > UAD bridge), for machines running Universal Audio's UADx
// plugins on Linux through the UADX toolkit (~/.local/share/uadx-bazzite: a pinned Wine build + yabridge, with UA
// Connect and the licences installed and activated through UA's own software). The checks only READ (/proc, files, the
// toolkit's test results) - they never start Wine. Every fix runs a toolkit command, and only after the user confirms.
// On a machine without the toolkit the panel does not appear.
#include "SystemCheck.h"

namespace dojo
{
struct UadAcceptance
{
    int total = 0, pass = 0, unlicensed = 0;
    juce::StringArray failing, notLicensed;   // plugin names (without "uaudio_")
    juce::String date;
};

class UadBridge
{
public:
    explicit UadBridge (Paths p) : paths (std::move (p)) {}
    bool installed() const;
    std::vector<Check> checks() const;

    // parts, public for the tests
    static juce::String serverDir (unsigned long long dev, unsigned long long ino, unsigned uid);   // Wine's per-prefix server dir
    static UadAcceptance parseAcceptance (const juce::String& latestJson);
    static juce::String niceName (const juce::String& plugin);   // "uaudio_teletronix_la-2a_tc" -> "Teletronix La-2a Tc"

private:
    juce::File bin (const juce::String& name) const { return paths.home.getChildFile (".local/bin").getChildFile (name); }
    juce::File toolkit() const { return paths.home.getChildFile (".local/share/uadx-bazzite"); }
    juce::File prefix() const { return paths.home.getChildFile (".wine-uaconnect"); }
    juce::File pinnedWine() const { return paths.home.getChildFile ("opt/wine-uad-dcomp11"); }
    juce::File runtime() const { return paths.home.getChildFile (".local/share/yabridge-wine10-embedding"); }
    juce::String foreignServer() const;   // the executable of a Wine server of another build owning the prefix, or {}
    Check wineOwner() const;
    Check setup() const;
    Check plugins() const;
    Check snapshot() const;
    Check reaper() const;
    Paths paths;
};
} // namespace dojo
