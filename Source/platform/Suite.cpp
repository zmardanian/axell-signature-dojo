#include "Suite.h"
#include "Process.h"
#include "../model/Tar.h"

#include <juce_cryptography/juce_cryptography.h>

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

namespace dojo
{
namespace
{
constexpr juce::int64 kMaxArchive = 256LL << 20;       // a release archive is a few MB
constexpr size_t kMaxUnpacked = 512u << 20;

// moduleinfo.json as JUCE writes it may carry trailing commas; a strict JSON parser needs them removed
juce::var parseModuleInfo (const juce::File& f)
{
    auto t = f.loadFileAsString();
    juce::String clean;
    clean.preallocateBytes ((size_t) t.getNumBytesAsUTF8());
    for (int i = 0; i < t.length(); ++i)
    {
        if (t[i] == ',')
        {
            int j = i + 1;
            while (j < t.length() && juce::CharacterFunctions::isWhitespace (t[j]))
                ++j;
            if (j < t.length() && (t[j] == '}' || t[j] == ']'))
                continue;
        }
        clean += t[i];
    }
    return juce::JSON::parse (clean);
}

// a directory move: one rename when possible (same filesystem), otherwise copy to a temporary name, rename, delete
bool moveDir (const juce::File& from, const juce::File& to)
{
    if (::rename (from.getFullPathName().toRawUTF8(), to.getFullPathName().toRawUTF8()) == 0)
        return true;
    if (errno != EXDEV)
        return false;
    const auto tmp = to.getSiblingFile (to.getFileName() + ".partial");
    tmp.deleteRecursively();
    if (! from.copyDirectoryTo (tmp))
        return false;
    if (::rename (tmp.getFullPathName().toRawUTF8(), to.getFullPathName().toRawUTF8()) != 0)
    {
        tmp.deleteRecursively();
        return false;
    }
    from.deleteRecursively();
    return true;
}

bool isLink (const juce::File& f)
{
    struct stat st {};
    return ::lstat (f.getFullPathName().toRawUTF8(), &st) == 0 && S_ISLNK (st.st_mode);
}

bool existsOrLink (const juce::File& f) { return f.exists() || isLink (f); }

juce::String timestamp() { return juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"); }
} // namespace

//==============================================================================
Version Suite::bundleVersion (const juce::File& bundle, juce::String* vendor)
{
    const auto info = parseModuleInfo (bundle.getChildFile ("Contents/Resources/moduleinfo.json"));
    if (vendor != nullptr)
        *vendor = info["Factory Info"]["Vendor"].toString();
    return Version::parse (info["Version"].toString().toStdString());
}

Installed Suite::installed (const Product& p) const
{
    Installed r;
    r.bundle = paths.vst3.getChildFile (p.bundle);
    r.brokenLink = isLink (r.bundle) && ! r.bundle.exists();
    r.present = r.bundle.isDirectory();
    if (r.present)
        r.version = bundleVersion (r.bundle, &r.vendor);
    return r;
}

juce::StringArray Suite::identityConflicts() const
{
    juce::StringArray out;
    for (const auto& l : Catalog::legacy())
        if (paths.vst3.getChildFile (l.bundle).isDirectory())
            if (auto* p = Catalog::find (l.replacedBy); p != nullptr && installed (*p).present)
                out.add (l.name + " and " + p->name + " are both installed. They are the same plugin to your DAW, so it "
                         "may load either one. Remove " + l.bundle + " from your plugin folder.");
    return out;
}

//==============================================================================
juce::String Suite::sha256 (const juce::File& f)
{
    return juce::SHA256 (f).toHexString().removeCharacters (" ").toLowerCase();
}

juce::String Suite::expectedSha256 (const juce::File& sums, const juce::String& fileName)
{
    juce::StringArray lines;
    sums.readLines (lines);
    for (auto line : lines)
    {
        line = line.trim();
        const auto hash = line.upToFirstOccurrenceOf (" ", false, false).toLowerCase();
        auto name = line.fromFirstOccurrenceOf (" ", false, false).trimStart();
        if (name.startsWithChar ('*'))
            name = name.substring (1);   // binary mode marker
        if (name == fileName && hash.length() == 64 && hash.containsOnly ("0123456789abcdef"))
            return hash;
    }
    return {};
}

juce::StringArray Suite::missingLibraries (const juce::File& bundle, bool& ran)
{
    juce::StringArray missing;
    ran = false;
    for (const auto& so : bundle.getChildFile ("Contents/x86_64-linux").findChildFiles (juce::File::findFiles, false, "*.so"))
    {
        const auto r = Process::run ({ "ldd", so.getFullPathName() }, 15000);
        if (! r.started)
            return missing;
        ran = true;
        for (auto line : juce::StringArray::fromLines (r.output))
            if (line.contains ("=> not found"))
                missing.addIfNotAlreadyThere (line.upToFirstOccurrenceOf ("=>", false, false).trim());
    }
    return missing;
}

//==============================================================================
juce::Array<Release> Suite::fetchGitHub (const Product& p, juce::String& error, const std::atomic<bool>* cancel) const
{
    juce::Array<Release> out;
    const juce::String api = juce::String ("https://api.github.com/repos/") + Catalog::kOwner + "/" + p.repo + "/releases?per_page=30";
    const auto r = Process::run ({ "curl", "--fail", "--silent", "--show-error", "--location", "--proto", "=https",
                                   "--proto-redir", "=https", "--max-time", "20", "-H", "Accept: application/vnd.github+json",
                                   "-H", "User-Agent: AxellSignatureDojo", api }, 25000, cancel);
    if (! r.started)
    {
        error = "curl is not installed, so the Dojo cannot check GitHub. Install from a folder instead.";
        return out;
    }
    if (! r.ok())
    {
        error = r.timedOut ? "GitHub did not answer in time." : "Could not reach GitHub: " + r.output.trim().substring (0, 200);
        return out;
    }
    const auto json = juce::JSON::parse (r.output);
    if (! json.isArray())
    {
        error = "GitHub sent an unexpected answer.";
        return out;
    }
    // asset URLs must point at this product's own release downloads
    const juce::String expectPrefix = juce::String ("https://github.com/") + Catalog::kOwner + "/" + p.repo + "/releases/download/";
    for (const auto& rel : *json.getArray())
    {
        if ((bool) rel["draft"] || (bool) rel["prerelease"])
            continue;
        Release x;
        x.productKey = p.key;
        x.tag = rel["tag_name"].toString();
        x.version = Version::parse (x.tag.toStdString());
        if (! x.version.valid)
            continue;
        x.notes = rel["body"].toString();
        x.published = rel["published_at"].toString().substring (0, 10);
        const auto archive = p.archiveName (juce::String (x.version.toString()));
        if (auto* assets = rel["assets"].getArray())
            for (const auto& a : *assets)
            {
                const auto name = a["name"].toString(), url = a["browser_download_url"].toString();
                if (! url.startsWith (expectPrefix))
                    continue;
                if (name == archive)
                {
                    x.archiveUrl = url;
                    x.archiveSize = (juce::int64) a["size"];
                }
                else if (name == "SHA256SUMS.txt")
                    x.sumsUrl = url;
            }
        if (x.archiveUrl.isNotEmpty() && x.sumsUrl.isNotEmpty())
            out.add (x);
    }
    std::sort (out.begin(), out.end(), [] (const Release& a, const Release& b) { return b.version < a.version; });
    return out;
}

juce::Array<Release> Suite::scanFolder (const Product& p, const juce::File& folder) const
{
    juce::Array<Release> out;
    if (! folder.isDirectory())
        return out;
    // <prefix>-<version>-linux-x86_64.tar.gz with a SHA256SUMS.txt in the same folder; the folder and up to three
    // levels below it (a USB stick's layout), walked level by level so a large folder is never scanned whole
    juce::Array<juce::File> level { folder };
    for (int depth = 0; depth <= 3 && ! level.isEmpty(); ++depth)
    {
        juce::Array<juce::File> next;
        for (const auto& dir : level)
        {
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, p.assetPrefix + "-*-linux-x86_64.tar.gz"))
            {
                const auto ver = f.getFileName().fromFirstOccurrenceOf (p.assetPrefix + "-", false, false)
                                                .upToFirstOccurrenceOf ("-linux-x86_64.tar.gz", false, false);
                Release x;
                x.productKey = p.key;
                x.version = Version::parse (ver.toStdString());
                x.tag = "v" + ver;
                x.archiveFile = f;
                x.sumsFile = f.getSiblingFile ("SHA256SUMS.txt");
                x.archiveSize = f.getSize();
                x.published = f.getLastModificationTime().formatted ("%Y-%m-%d");
                if (x.version.valid && x.sumsFile.existsAsFile())
                    out.add (x);
            }
            if (depth < 3)
                for (const auto& d : dir.findChildFiles (juce::File::findDirectories, false))
                    if (! d.getFileName().startsWithChar ('.'))
                        next.add (d);
        }
        level = next;
    }
    std::sort (out.begin(), out.end(), [] (const Release& a, const Release& b) { return b.version < a.version; });
    // the same version found twice (a copy in two folders): keep the first
    for (int i = out.size() - 1; i > 0; --i)
        if (out[i].version == out[i - 1].version)
            out.remove (i);
    return out;
}

//==============================================================================
StepResult Suite::download (Release& rel, const ProgressFn& progress, const std::atomic<bool>* cancel) const
{
    const auto* p = Catalog::find (rel.productKey);
    if (p == nullptr || rel.archiveUrl.isEmpty() || rel.sumsUrl.isEmpty())
        return { false, "This release has nothing to download." };
    const auto dir = paths.downloads().getChildFile (p->key).getChildFile (juce::String (rel.version.toString()));
    if (! dir.createDirectory())
        return { false, "Cannot write to " + dir.getFullPathName() };
    const auto archive = dir.getChildFile (p->archiveName (juce::String (rel.version.toString())));
    const auto sums = dir.getChildFile ("SHA256SUMS.txt");
    auto fetch = [&] (const juce::String& url, const juce::File& to, bool reportSize) -> StepResult
    {
        const auto part = to.getSiblingFile (to.getFileName() + ".part");
        part.deleteFile();
        const auto r = Process::run ({ "curl", "--fail", "--silent", "--show-error", "--location", "--proto", "=https",
                                       "--proto-redir", "=https", "--max-time", "900", "-H", "User-Agent: AxellSignatureDojo",
                                       "-o", part.getFullPathName(), url },
                                     905000, cancel, [&]
                                     {
                                         if (reportSize && progress && rel.archiveSize > 0)
                                             progress ((float) juce::jlimit (0.0, 1.0, (double) part.getSize() / (double) rel.archiveSize),
                                                       "Downloading " + p->name);
                                     });
        if (! r.ok())
        {
            part.deleteFile();
            if (! r.started)
                return { false, "curl is not installed." };
            return { false, r.timedOut ? "The download stopped." : "Download failed: " + r.output.trim().substring (0, 200) };
        }
        to.deleteFile();
        return { part.moveFileTo (to), {} };
    };
    if (auto s = fetch (rel.sumsUrl, sums, false); ! s.ok)
        return s.message.isEmpty() ? StepResult { false, "Could not save the checksums." } : s;
    if (auto s = fetch (rel.archiveUrl, archive, true); ! s.ok)
        return s.message.isEmpty() ? StepResult { false, "Could not save the download." } : s;
    rel.archiveFile = archive;
    rel.sumsFile = sums;
    return { true, "Downloaded " + p->name + " " + juce::String (rel.version.toString()) + "." };
}

//==============================================================================
StepResult Suite::stage (const Product& p, const Release& rel, juce::File& stagedBundle, const ProgressFn& progress,
                         const std::atomic<bool>* cancel) const
{
    const auto ver = juce::String (rel.version.toString());
    if (! rel.archiveFile.existsAsFile() || ! rel.sumsFile.existsAsFile())
        return { false, "The release archive or its checksums are missing." };
    if (rel.archiveFile.getSize() > kMaxArchive)
        return { false, "The archive is far larger than any Axell release; it was not opened." };

    if (progress) progress (0.1f, "Checking the download's checksum");
    const auto expected = expectedSha256 (rel.sumsFile, rel.archiveFile.getFileName());
    if (expected.isEmpty())
        return { false, "SHA256SUMS.txt has no checksum for " + rel.archiveFile.getFileName() + "." };
    if (sha256 (rel.archiveFile) != expected)
        return { false, "The checksum does not match: the archive is damaged or not the published one. Nothing was installed." };
    if (cancel != nullptr && cancel->load())
        return { false, "Cancelled." };

    if (progress) progress (0.3f, "Unpacking");
    juce::MemoryBlock data;
    {
        juce::GZIPDecompressorInputStream gz (new juce::FileInputStream (rel.archiveFile), true,
                                              juce::GZIPDecompressorInputStream::gzipFormat);
        char buf[1 << 16];
        for (int n; (n = gz.read (buf, sizeof (buf))) > 0;)
        {
            data.append (buf, (size_t) n);
            if (data.getSize() > kMaxUnpacked)
                return { false, "The archive unpacks to far more than any Axell release; it was not installed." };
        }
        if (data.getSize() == 0)
            return { false, "The archive could not be unpacked." };
    }
    const auto tarResult = tar::read (static_cast<const uint8_t*> (data.getData()), data.getSize());
    if (! tarResult.ok())
        return { false, "The archive was rejected: " + juce::String (tarResult.error) };

    const auto top = p.assetPrefix + "-" + ver + "-linux-x86_64";
    const auto bundlePrefix = top + "/" + p.bundle;
    const auto stageRoot = paths.staging().getChildFile (p.key + "-" + ver + "-" + juce::String::toHexString (juce::Random().nextInt64()));
    stageRoot.deleteRecursively();
    if (! stageRoot.createDirectory())
        return { false, "Cannot write to " + stageRoot.getFullPathName() };
    stagedBundle = stageRoot.getChildFile (p.bundle);
    int files = 0;
    for (const auto& e : tarResult.entries)
    {
        const juce::String path (e.path);
        if (path != bundlePrefix && ! path.startsWith (bundlePrefix + "/"))
            continue;   // only the plugin bundle is taken; scripts and docs in the archive are not needed
        const auto target = stageRoot.getChildFile (path.substring (top.length() + 1));
        if (! target.isAChildOf (stageRoot) && target != stagedBundle)
            return { false, "The archive was rejected: unsafe path." };
        if (e.directory)
        {
            target.createDirectory();
            continue;
        }
        target.getParentDirectory().createDirectory();
        if (! target.replaceWithData (static_cast<const char*> (data.getData()) + e.offset, e.size))
            return { false, "Cannot write " + target.getFullPathName() };
        ::chmod (target.getFullPathName().toRawUTF8(), (mode_t) ((e.mode & 0111) ? 0755 : 0644));
        ++files;
    }
    if (files == 0)
        return { false, "The archive does not contain " + p.bundle + "." };

    if (progress) progress (0.6f, "Checking the plugin");
    if (stagedBundle.getChildFile ("Contents/x86_64-linux").findChildFiles (juce::File::findFiles, false, "*.so").isEmpty())
        return { false, "The bundle has no Linux plugin binary." };
    juce::String vendor;
    const auto bv = bundleVersion (stagedBundle, &vendor);
    if (bv != rel.version)
        return { false, "The bundle says it is version " + juce::String (bv.valid ? bv.toString() : "unknown")
                        + ", not " + ver + "." };
    if (vendor != Catalog::kVendor)
        return { false, "The bundle is not an Axell Yobag plugin." };
    bool ran = false;
    const auto missing = missingLibraries (stagedBundle, ran);
    if (! missing.isEmpty())
        return { false, "This system lacks libraries the plugin needs: " + missing.joinIntoString (", ")
                        + ". Nothing was installed." };
    return { true, {} };
}

StepResult Suite::moveToBackup (const Product& p, const juce::File& bundle) const
{
    const auto v = isLink (bundle) && ! bundle.exists() ? Version() : bundleVersion (bundle);
    const auto base = (v.valid ? juce::String (v.toString()) : juce::String ("unknown")) + "_" + timestamp();
    auto dir = paths.backups().getChildFile (p.key).getChildFile (base);
    for (int i = 2; dir.exists(); ++i)   // two backups within one second
        dir = dir.getSiblingFile (base + "-" + juce::String (i));
    if (! dir.createDirectory())
        return { false, "Cannot write the backup folder " + dir.getFullPathName() };
    if (! moveDir (bundle, dir.getChildFile (p.bundle)))
        return { false, "Could not move the installed " + p.name + " aside." };
    return { true, {} };
}

void Suite::pruneBackups (const Product& p) const
{
    auto list = backups (p);
    for (int i = kBackupsKept; i < list.size(); ++i)
        list[i].folder.getParentDirectory().deleteRecursively();
}

juce::Array<Backup> Suite::backups (const Product& p) const
{
    juce::Array<Backup> out;
    for (const auto& d : paths.backups().getChildFile (p.key).findChildFiles (juce::File::findDirectories, false))
    {
        const auto b = d.getChildFile (p.bundle);
        if (! existsOrLink (b))
            continue;
        Backup x;
        x.folder = b;
        x.version = b.isDirectory() ? bundleVersion (b) : Version();
        const auto stamp = d.getFileName().fromLastOccurrenceOf ("_", false, false);   // YYYYmmdd-HHMMSS[-n]
        x.when = juce::Time (stamp.substring (0, 4).getIntValue(), stamp.substring (4, 6).getIntValue() - 1,
                             stamp.substring (6, 8).getIntValue(), stamp.substring (9, 11).getIntValue(),
                             stamp.substring (11, 13).getIntValue(), stamp.substring (13, 15).getIntValue());
        out.add (x);
    }
    std::sort (out.begin(), out.end(), [] (const Backup& a, const Backup& b) { return a.when > b.when; });
    return out;
}

StepResult Suite::install (const Product& p, const Release& rel, const ProgressFn& progress, const std::atomic<bool>* cancel) const
{
    const auto ver = juce::String (rel.version.toString());
    juce::File staged;
    const auto s = stage (p, rel, staged, progress, cancel);
    const auto stageRoot = staged.getParentDirectory();
    if (! s.ok)
    {
        if (stageRoot.isAChildOf (paths.staging()))
            stageRoot.deleteRecursively();
        return s;
    }
    if (cancel != nullptr && cancel->load())
    {
        stageRoot.deleteRecursively();
        return { false, "Cancelled." };
    }
    if (progress) progress (0.8f, "Installing");
    if (! paths.vst3.createDirectory())
        return { false, "Cannot create " + paths.vst3.getFullPathName() };
    const auto target = paths.vst3.getChildFile (p.bundle);
    const auto before = installed (p);
    bool backedUp = false;
    if (existsOrLink (target))
    {
        if (auto b = moveToBackup (p, target); ! b.ok)
        {
            stageRoot.deleteRecursively();
            return b;
        }
        backedUp = true;
    }
    if (! moveDir (staged, target))
    {
        // put the previous version back exactly where it was
        if (backedUp)
            if (auto list = backups (p); ! list.isEmpty() && moveDir (list[0].folder, target))
                list[0].folder.getParentDirectory().deleteRecursively();
        stageRoot.deleteRecursively();
        return { false, "Could not place the plugin in " + paths.vst3.getFullPathName() + ". Nothing changed." };
    }
    stageRoot.deleteRecursively();
    pruneBackups (p);
    if (progress) progress (1.0f, "Installed");
    juce::String msg = p.name + " " + ver + " is installed.";
    if (before.present && before.version.valid)
        msg << " Version " << juce::String (before.version.toString()) << " is kept as a backup.";
    return { true, msg };
}

StepResult Suite::uninstall (const Product& p) const
{
    const auto target = paths.vst3.getChildFile (p.bundle);
    if (! existsOrLink (target))
        return { false, p.name + " is not installed." };
    if (auto b = moveToBackup (p, target); ! b.ok)
        return b;
    pruneBackups (p);
    return { true, p.name + " is removed. A copy is kept in the Dojo, so you can restore it." };
}

StepResult Suite::restore (const Product& p, const Backup& b) const
{
    if (! existsOrLink (b.folder))
        return { false, "That backup no longer exists." };
    const auto target = paths.vst3.getChildFile (p.bundle);
    const auto holder = b.folder.getParentDirectory();
    // the current version becomes a backup itself, so a restore can be undone too
    if (existsOrLink (target))
        if (auto r = moveToBackup (p, target); ! r.ok)
            return r;
    paths.vst3.createDirectory();
    if (! moveDir (b.folder, target))
        return { false, "Could not restore the backup." };
    holder.deleteRecursively();
    pruneBackups (p);
    return { true, p.name + " " + juce::String (b.version.valid ? b.version.toString() : "") + " is restored." };
}
} // namespace dojo
