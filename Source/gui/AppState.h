#pragma once
// Axell Signature Dojo - the app's state in one place: the suite, releases, workstation checks, the Voice Profile and
// the chains, plus the background work that changes them. Pages read it on the message thread and listen for changes;
// every long task runs on a worker thread, reports progress, and can be cancelled when the app closes.
#include "../platform/Chains.h"
#include "../platform/ProfileEngine.h"
#include "../platform/Signature.h"
#include "../platform/Suite.h"
#include "../platform/SystemCheck.h"
#include "../platform/Tools.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <map>

namespace dojo::gui
{
class AppState : public juce::ChangeBroadcaster
{
public:
    explicit AppState (Paths);
    ~AppState() override;

    const Paths& getPaths() const { return paths; }
    const Suite& getSuite() const { return suite; }

    //== plugins ==
    struct ProductState
    {
        const Product* product = nullptr;
        Installed installed;
        juce::Array<Release> releases;        // newest first
        juce::Array<Backup> backups;
        bool busy = false;
        float progress = 0.0f;
        juce::String status, message;          // what it is doing / what last happened
        bool messageOk = true;
        const Release* latest() const { return releases.isEmpty() ? nullptr : &releases.getReference (0); }
        bool updateAvailable() const { return installed.present && latest() != nullptr && installed.version < latest()->version; }
    };
    std::vector<ProductState> products;
    ProductState* product (const juce::String& key);

    enum class Source { github, folder };
    Source source = Source::github;
    juce::File sourceFolder;
    juce::String releasesError;
    bool releasesBusy = false, releasesChecked = false;

    void refreshInstalled();                   // fast: local files only
    void checkReleases();                      // GitHub or the chosen folder
    void setSource (Source, const juce::File& folder = {});
    void install (const juce::String& key, const Release&);
    void uninstall (const juce::String& key);
    void restore (const juce::String& key, const Backup&);
    int updatesAvailable() const;
    int installedCount() const;

    //== workstation ==
    std::vector<Check> checks;
    bool checksBusy = false, checksRun = false;
    int checksGeneration = 0;                  // +1 for every new set of results
    juce::String fixMessage;
    void runChecks();
    void applyFix (const Fix&);
    int countChecks (Check::Status) const;

    //== Voice Profile ==
    juce::File take;
    std::shared_ptr<ProfileOutcome> profile;   // the latest analysis (and tuning)
    bool profileBusy = false, profileTuned = false;
    float profileProgress = 0.0f;
    juce::String profileStatus, profileError;
    juce::StringArray writeLog;
    juce::File reference;                      // Reference Match: the vocal to move toward ({} = none)
    float referenceAmount = 1.0f;
    void analyse (const juce::File& takeFile, bool tune);
    void writeProfile (const ProfileEngine::WriteOptions&);
    juce::Array<juce::File> savedProfiles() const;

    //== chains ==
    std::map<juce::String, juce::String> chainStatus;   // template id -> last message
    std::map<juce::String, bool> chainBusy;
    bool chainInstalled (const ChainTemplate&) const;
    void installChain (const ChainTemplate&);
    // brings every chain the Dojo saved up to the installed plugins (tools::updateSavedChains); the result goes to
    // reaperMessage
    void updateSavedChains();
    // a take through a signature sound, opened in REAPER as a session (gain-matched, master at `lufs`)
    std::vector<Signature> signatures() const { return Signature::all (paths); }
    void openSignature (const Signature&, const juce::File& take, float lufs);
    bool signatureBusy = false;
    bool chainsUpdating = false;
    // profile chains the Dojo saved ("<name> - Vocal Chain.RfxChain"), newest first
    juce::Array<juce::File> profileChains() const;
    juce::String reaperMessage;
    bool reaperMessageOk = true;
    void openInReaper (const juce::File& chainFile, const juce::String& trackName);

    //== tools ==
    juce::File batchChain;
    juce::Array<juce::File> batchFiles;
    std::vector<tools::RenderedFile> batchResults;
    bool batchBusy = false;
    float batchProgress = 0.0f;
    juce::String batchStatus;
    juce::File batchOutput() const { return paths.home.getChildFile ("Documents/Axell Signature/Rendered"); }
    void runBatch();

    std::shared_ptr<tools::MasterPreview> master;
    bool masterBusy = false;
    juce::String masterError;
    void previewMaster (const juce::File&);

    juce::String backupMessage;
    bool backupOk = true;
    void exportBackup (const juce::File& destination);
    void importBackup (const juce::File& folder);

    //== settings ==
    juce::PropertiesFile& settings() { return *props; }
    float uiScale() const;
    void setUiScale (float);

    //== background work ==
    bool busy() const { return running.load() > 0; }
    void cancelAll() { quitting = true; }      // every running task stops at its next check
    juce::String activity;                     // the newest running task, for the status line

private:
    using Work = std::function<void (const std::atomic<bool>& cancel)>;
    void run (const juce::String& label, Work work, std::function<void()> done = {});
    // a progress / result update from a worker thread, applied on the message thread while the state is alive
    template <typename Fn>
    void post (Fn&& fn)
    {
        juce::MessageManager::callAsync ([weak = std::weak_ptr<bool> (alive), fn = std::forward<Fn> (fn)]() mutable
        {
            if (! weak.expired())
                fn();
        });
    }
    void changed();

    Paths paths;
    Suite suite;
    std::unique_ptr<juce::PropertiesFile> props;
    juce::ThreadPool pool { 3 };
    std::atomic<bool> quitting { false };
    std::atomic<int> running { 0 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
} // namespace dojo::gui
