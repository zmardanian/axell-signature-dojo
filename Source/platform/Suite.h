#pragma once
// Axell Signature Dojo - installing, updating, rolling back and removing the suite's plugins.
//
// Safety model (docs/DESIGN.md, D2-D3):
//  - a release is installed only from its archive plus the SHA256SUMS.txt published beside it, and only if the
//    checksum matches; the archive is unpacked by the strict reader in model/Tar.h into a private staging folder,
//    and only the plugin bundle is taken from it (the archive's scripts are never run);
//  - the staged bundle must hold the plugin binary and a moduleinfo.json whose version matches the release, and
//    every library it links must resolve (ldd), before anything in ~/.vst3 changes;
//  - the new bundle and the installed one trade places in one atomic step (renameat2 RENAME_EXCHANGE), then the old
//    one goes to the backups: at no moment is the plugin missing from ~/.vst3, even if the Dojo is killed. On a
//    filesystem without that call, two renames are used and every failure is reported as it is. Nothing else in
//    ~/.vst3 is ever touched;
//  - the library check must actually run: if ldd is missing, fails or times out, nothing is installed;
//  - removing a plugin moves it to the backups too, so every step can be undone from the Dojo.
#include "Catalog.h"
#include "Paths.h"
#include "../model/Version.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace dojo
{
struct Installed
{
    bool present = false;
    bool brokenLink = false;       // ~/.vst3/<bundle> is a symlink to nothing
    Version version;
    juce::File bundle;
    juce::String vendor;
};

struct Release
{
    juce::String productKey, tag;
    Version version;
    juce::String archiveUrl, sumsUrl;      // GitHub
    juce::File archiveFile, sumsFile;      // a local folder (USB stick, downloads)
    juce::int64 archiveSize = 0;
    juce::String notes, published;
    bool isLocal() const { return archiveFile != juce::File(); }
};

struct Backup
{
    juce::File folder;             // backups/<key>/<version>_<timestamp>/<bundle>
    Version version;
    juce::Time when;
};

struct StepResult
{
    bool ok = false;
    juce::String message;          // what happened, in the user's words
};

using ProgressFn = std::function<void (float, const juce::String&)>;

class Suite
{
public:
    explicit Suite (Paths p) : paths (std::move (p)) {}
    const Paths& getPaths() const { return paths; }

    Installed installed (const Product&) const;
    // a legacy plugin with the same VST3 identity is installed next to its replacement
    juce::StringArray identityConflicts() const;

    // releases, newest first: GitHub (curl) or a folder holding <prefix>-<version>-linux-x86_64.tar.gz + SHA256SUMS.txt
    juce::Array<Release> fetchGitHub (const Product&, juce::String& error, const std::atomic<bool>* cancel = nullptr) const;
    juce::Array<Release> scanFolder (const Product&, const juce::File& folder) const;

    // download a GitHub release's archive and checksums into the cache; fills archiveFile / sumsFile
    StepResult download (Release&, const ProgressFn&, const std::atomic<bool>* cancel = nullptr) const;
    StepResult install (const Product&, const Release&, const ProgressFn&, const std::atomic<bool>* cancel = nullptr) const;
    StepResult uninstall (const Product&) const;
    juce::Array<Backup> backups (const Product&) const;
    StepResult restore (const Product&, const Backup&) const;

    // checksum line for `fileName` in a SHA256SUMS file; empty if absent or malformed
    static juce::String expectedSha256 (const juce::File& sums, const juce::String& fileName);
    static juce::String sha256 (const juce::File&);
    static Version bundleVersion (const juce::File& bundle, juce::String* vendor = nullptr);
    // libraries the bundle's binary needs that do not resolve on this system (ldd); `ran` false if ldd is missing
    static juce::StringArray missingLibraries (const juce::File& bundle, bool& ran);

    static constexpr int kBackupsKept = 3;

    // test hooks: the program used for the library check; and stopping an install right after the swap (as if the
    // Dojo were killed there) to prove the plugin folder is never left without a working plugin
    static inline juce::String lddProgram { "ldd" };
    static inline bool stopAfterSwapForTest = false;

private:
    StepResult stage (const Product&, const Release&, juce::File& stagedBundle, const ProgressFn&, const std::atomic<bool>*) const;
    StepResult moveToBackup (const Product&, const juce::File& bundle) const;
    void pruneBackups (const Product&) const;
    Paths paths;
};
} // namespace dojo
