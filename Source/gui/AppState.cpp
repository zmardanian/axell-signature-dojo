#include "AppState.h"

namespace dojo::gui
{
AppState::AppState (Paths p) : paths (std::move (p)), suite (paths)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "dojo";
    o.filenameSuffix = ".settings";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    paths.settingsFile().getParentDirectory().createDirectory();
    props = std::make_unique<juce::PropertiesFile> (paths.settingsFile(), o);
    for (const auto& pr : Catalog::products())
    {
        ProductState s;
        s.product = &pr;
        products.push_back (s);
    }
    if (props->getValue ("source") == "folder" && juce::File::isAbsolutePath (props->getValue ("sourceFolder")))
    {
        source = Source::folder;
        sourceFolder = juce::File (props->getValue ("sourceFolder"));
    }
    refreshInstalled();
}

AppState::~AppState()
{
    quitting = true;
    pool.removeAllJobs (true, 20000);
}

void AppState::changed() { sendChangeMessage(); }

void AppState::run (const juce::String& label, Work work, std::function<void()> done)
{
    ++running;
    activity = label;
    changed();
    std::weak_ptr<bool> weak = alive;
    pool.addJob ([this, weak, work = std::move (work), done = std::move (done)]
    {
        work (quitting);
        juce::MessageManager::callAsync ([this, weak, done]
        {
            if (weak.expired())
                return;
            --running;
            if (running.load() == 0)
                activity.clear();
            if (done)
                done();
            changed();
        });
    });
}


//== plugins =====================================================================================================
AppState::ProductState* AppState::product (const juce::String& key)
{
    for (auto& s : products)
        if (s.product->key == key)
            return &s;
    return nullptr;
}

void AppState::refreshInstalled()
{
    for (auto& s : products)
    {
        s.installed = suite.installed (*s.product);
        s.backups = suite.backups (*s.product);
    }
    changed();
}

int AppState::updatesAvailable() const
{
    int n = 0;
    for (auto& s : products)
        n += s.updateAvailable() ? 1 : 0;
    return n;
}

int AppState::installedCount() const
{
    int n = 0;
    for (auto& s : products)
        n += s.installed.present ? 1 : 0;
    return n;
}

void AppState::setSource (Source s, const juce::File& folder)
{
    source = s;
    sourceFolder = folder;
    props->setValue ("source", s == Source::folder ? "folder" : "github");
    props->setValue ("sourceFolder", folder.getFullPathName());
    for (auto& p : products)
        p.releases.clear();
    releasesChecked = false;
    checkReleases();
}

void AppState::checkReleases()
{
    if (releasesBusy)
        return;
    releasesBusy = true;
    releasesError.clear();
    const auto src = source;
    const auto folder = sourceFolder;
    run ("Checking for updates", [this, src, folder] (const std::atomic<bool>& cancel)
    {
        std::map<juce::String, juce::Array<Release>> found;
        juce::String error;
        for (const auto& p : Catalog::products())
        {
            if (cancel)
                return;
            juce::String err;
            found[p.key] = src == Source::github ? suite.fetchGitHub (p, err, &cancel) : suite.scanFolder (p, folder);
            if (err.isNotEmpty() && error.isEmpty())
                error = err;
        }
        post ([this, found, error]
        {
            for (auto& s : products)
                if (const auto it = found.find (s.product->key); it != found.end())
                    s.releases = it->second;
            releasesError = error;
            releasesChecked = true;
        });
    }, [this] { releasesBusy = false; });
}

void AppState::install (const juce::String& key, const Release& release)
{
    auto* s = product (key);
    if (s == nullptr || s->busy)
        return;
    s->busy = true;
    s->progress = 0.0f;
    s->message.clear();
    s->status = "Starting";
    const Product& prod = *s->product;
    run ("Installing " + prod.name, [this, &prod, release, key] (const std::atomic<bool>& cancel)
    {
        auto progress = [this, key] (float f, const juce::String& t)
        {
            post ([this, key, f, t]
            {
                if (auto* st = product (key)) { st->progress = f; st->status = t; }
                changed();
            });
        };
        auto rel = release;
        StepResult r { true, {} };
        if (! rel.isLocal())
        {
            progress (0.0f, "Downloading");
            r = suite.download (rel, [&] (float f, const juce::String& t) { progress (0.5f * f, t); }, &cancel);
        }
        if (r.ok)
            r = suite.install (prod, rel, [&] (float f, const juce::String& t) { progress (rel.isLocal() ? f : 0.5f + 0.5f * f, t); }, &cancel);
        post ([this, key, r]
        {
            if (auto* st = product (key)) { st->message = r.message; st->messageOk = r.ok; }
        });
    }, [this, key]
    {
        if (auto* st = product (key)) { st->busy = false; st->status.clear(); }
        refreshInstalled();
    });
}

void AppState::uninstall (const juce::String& key)
{
    auto* s = product (key);
    if (s == nullptr || s->busy)
        return;
    const auto r = suite.uninstall (*s->product);   // a rename: instant
    s->message = r.message;
    s->messageOk = r.ok;
    refreshInstalled();
}

void AppState::restore (const juce::String& key, const Backup& b)
{
    auto* s = product (key);
    if (s == nullptr || s->busy)
        return;
    const auto r = suite.restore (*s->product, b);
    s->message = r.message;
    s->messageOk = r.ok;
    refreshInstalled();
}

//== workstation =================================================================================================
void AppState::runChecks()
{
    if (checksBusy)
        return;
    checksBusy = true;
    run ("Checking your workstation", [this] (const std::atomic<bool>&)
    {
        auto result = SystemCheck (paths).runAll();
        post ([this, result]
        {
            checks = result;
            checksRun = true;
            ++checksGeneration;
        });
    }, [this] { checksBusy = false; });
}

void AppState::applyFix (const Fix& f)
{
    run ("Applying: " + f.label, [this, f] (const std::atomic<bool>&)
    {
        const auto r = SystemCheck (paths).apply (f);
        post ([this, r]
        {
            fixMessage = r.message;
        });
    }, [this] { checksBusy = false; runChecks(); });
}

int AppState::countChecks (Check::Status st) const
{
    int n = 0;
    for (auto& c : checks)
        n += c.status == st ? 1 : 0;
    return n;
}

//== Voice Profile ===============================================================================================
void AppState::analyse (const juce::File& takeFile, bool tune)
{
    if (profileBusy)
        return;
    profileBusy = true;
    profileError.clear();
    writeLog.clear();
    profileProgress = 0.0f;
    take = takeFile;
    const auto ref = reference;
    const float amount = referenceAmount;
    run (tune ? "Tuning your chain" : "Analysing your take", [this, takeFile, tune, ref, amount] (const std::atomic<bool>& cancel)
    {
        ProfileEngine eng (paths);
        auto outcome = std::make_shared<ProfileOutcome> (eng.run (takeFile, tune, [this] (float f, const juce::String& t)
        {
            post ([this, f, t]
            {
                profileProgress = f;
                profileStatus = t;
                changed();
            });
        }, &cancel, ref, amount));
        post ([this, outcome, tune]
        {
            if (outcome->ok)
            {
                profile = outcome;
                profileTuned = tune;
            }
            else
                profileError = outcome->error;
        });
    }, [this] { profileBusy = false; profileStatus.clear(); });
}

void AppState::writeProfile (const ProfileEngine::WriteOptions& o)
{
    if (profile == nullptr || profileBusy)
        return;
    profileBusy = true;
    auto outcome = profile;
    run ("Saving your presets", [this, outcome, o] (const std::atomic<bool>&)
    {
        const auto lines = ProfileEngine (paths).write (*outcome, o);
        post ([this, lines]
        {
            writeLog = lines;
        });
    }, [this] { profileBusy = false; });
}

juce::Array<juce::File> AppState::savedProfiles() const
{
    auto list = paths.profiles().findChildFiles (juce::File::findFiles, false, "*.json");
    std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return list;
}

//== chains ======================================================================================================
bool AppState::chainInstalled (const ChainTemplate& t) const
{
    return paths.reaperChains().getChildFile (juce::File::createLegalFileName (t.name) + ".RfxChain").existsAsFile();
}

void AppState::installChain (const ChainTemplate& t)
{
    if (chainBusy[t.id])
        return;
    chainBusy[t.id] = true;
    chainStatus[t.id] = "Building";
    run ("Building " + t.name, [this, t] (const std::atomic<bool>&)
    {
        ChainWriter cw (paths);
        juce::StringArray problems;
        auto stages = cw.build (t, problems);
        juce::String msg;
        if (! problems.isEmpty())
            msg = problems.joinIntoString (" ");
        else
            msg = cw.write (t.name, cw.chainText (stages)).message;
        stages.clear();   // the hosted plugins close here (on the message thread, through their destructors)
        post ([this, msg, id = t.id]
        {
            chainStatus[id] = msg;
        });
    }, [this, id = t.id] { chainBusy[id] = false; });
}

juce::Array<juce::File> AppState::profileChains() const
{
    auto list = paths.reaperChains().findChildFiles (juce::File::findFiles, false, "* - Vocal Chain.RfxChain");
    std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return list;
}

void AppState::openInReaper (const juce::File& chainFile, const juce::String& trackName)
{
    const auto r = ChainWriter (paths).openInReaper (chainFile, trackName);   // a file and a process start: instant
    reaperMessage = r.message;
    reaperMessageOk = r.ok;
    changed();
}

//== tools =======================================================================================================
void AppState::runBatch()
{
    if (batchBusy || batchFiles.isEmpty() || ! batchChain.existsAsFile())
        return;
    batchBusy = true;
    batchResults.clear();
    const auto files = batchFiles;
    const auto chain = batchChain;
    const auto out = batchOutput();
    run ("Rendering " + juce::String (files.size()) + " files", [this, files, chain, out] (const std::atomic<bool>& cancel)
    {
        auto results = tools::batchRender (paths, chain, files, out, [this] (float f, const juce::String& t)
        {
            post ([this, f, t] { batchProgress = f; batchStatus = t; changed(); });
        }, &cancel);
        post ([this, results] { batchResults = results; });
    }, [this] { batchBusy = false; batchStatus.clear(); });
}

void AppState::previewMaster (const juce::File& f)
{
    if (masterBusy)
        return;
    masterBusy = true;
    masterError.clear();
    run ("Measuring " + f.getFileName(), [this, f] (const std::atomic<bool>&)
    {
        auto m = std::make_shared<tools::MasterPreview> (tools::previewMaster (f));
        post ([this, m] { if (m->ok) master = m; else masterError = m->error; });
    }, [this] { masterBusy = false; });
}

void AppState::exportBackup (const juce::File& destination)
{
    const auto r = tools::exportBackup (paths, destination);
    backupMessage = r.message;
    backupOk = r.ok;
    changed();
}

void AppState::importBackup (const juce::File& folder)
{
    const auto r = tools::importBackup (paths, folder);
    backupMessage = r.message;
    backupOk = r.ok;
    refreshInstalled();
}

//== settings ====================================================================================================
float AppState::uiScale() const
{
    return (float) juce::jlimit (0.75, 2.0, props->getDoubleValue ("uiScale", 1.0));
}

void AppState::setUiScale (float s)
{
    props->setValue ("uiScale", (double) s);
    changed();
}
} // namespace dojo::gui
