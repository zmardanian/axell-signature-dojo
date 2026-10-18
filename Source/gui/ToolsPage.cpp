#include "Pages.h"

namespace dojo::gui
{
ToolsPage::ToolsPage (AppState& s, AudioEngine& a) : state (s), audio (a)
{
    // Batch Render
    variant (chainChoice, "ghost");
    chainChoice.setTooltip ("The REAPER chain every file goes through: your Voice Profile chains and the Axell Signature templates.");
    chainChoice.onClick = [this]
    {
        juce::PopupMenu m;
        const auto files = state.getPaths().reaperChains().findChildFiles (juce::File::findFiles, false, "*.RfxChain");
        for (int i = 0; i < files.size(); ++i)
            m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, files[i] == state.batchChain);
        if (files.isEmpty())
            m.addItem (-1, "No chains yet: save a Voice Profile or add a template", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (chainChoice), [this, files] (int r)
        {
            if (r > 0)
            {
                state.batchChain = files[r - 1];
                refresh();
            }
        });
    };
    variant (addFiles, "ghost");
    addFiles.getProperties().set ("icon", "folder");
    addFiles.setButtonText ("Add takes");
    addFiles.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Takes to render", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc)
        {
            for (const auto& f : fc.getResults())
                state.batchFiles.addIfNotAlreadyThere (f);
            refresh();
        });
    };
    variant (clearFiles, "quiet");
    clearFiles.setButtonText ("Clear");
    clearFiles.onClick = [this] { state.batchFiles.clear(); state.batchResults.clear(); refresh(); };
    variant (render, "primary");
    render.getProperties().set ("icon", "sparkle");
    render.setButtonText ("Render");
    render.onClick = [this] { state.runBatch(); };
    variant (showOutput, "quiet");
    showOutput.setButtonText ("Open folder");
    showOutput.onClick = [this] { state.batchOutput().createDirectory(); state.batchOutput().startAsProcess(); };

    // Streaming Preview
    variant (chooseMaster, "primary", theme::ok);
    chooseMaster.getProperties().set ("icon", "folder");
    chooseMaster.setButtonText ("Choose a master");
    chooseMaster.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("A finished master", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                state.previewMaster (fc.getResult());
        });
    };
    variant (playMaster, "ghost");
    playMaster.getProperties().set ("icon", "play");
    playMaster.onClick = [this]
    {
        juce::String err;
        if (audio.isPlaying() && audio.owner == this)
            audio.stop();
        else if (state.master != nullptr && audio.open (err))
        {
            claimAudio();
            audio.play();
        }
        refresh();
    };

    // Backup & Move
    variant (exportButton, "primary", theme::teal);
    exportButton.getProperties().set ("icon", "download");
    exportButton.setButtonText ("Back up to a folder");
    exportButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Where to put the backup (a USB stick, a cloud folder...)", juce::File ("/run/media"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
                state.exportBackup (fc.getResult());
        });
    };
    variant (importButton, "ghost", theme::teal);
    importButton.getProperties().set ("icon", "folder");
    importButton.setButtonText ("Restore a backup");
    importButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("An \"Axell Signature Backup\" folder", juce::File ("/run/media"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
                state.importBackup (fc.getResult());
        });
    };
    for (auto* c : std::initializer_list<juce::Component*> { &chainChoice, &addFiles, &clearFiles, &render, &showOutput, &chooseMaster,
                                                              &playMaster, &exportButton, &importButton })
        content().addAndMakeVisible (c);
    content().addChildComponent (spinner);
}

ToolsPage::~ToolsPage()
{
    if (audio.owner == this)
        audio.stop();
}

bool ToolsPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void ToolsPage::filesDropped (const juce::StringArray& files, int, int y)
{
    // dropped on the Streaming card: a master; anywhere else: takes to render
    const int contentY = y - content().getY();   // the content scrolls inside the page
    if (masterCard.contains (masterCard.getX() + 1, contentY) && files.size() == 1)
        state.previewMaster (juce::File (files[0]));
    else
        for (auto& f : files)
            if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
                state.batchFiles.addIfNotAlreadyThere (juce::File (f));
    refresh();
}

void ToolsPage::claimAudio()
{
    if (state.master == nullptr)
        return;
    audio.owner = this;
    audio.setPlayback (state.master->audio, state.master->audio, state.master->sampleRate, true);
    selectService (selected);
}

void ToolsPage::selectService (int i)
{
    selected = i;
    if (state.master == nullptr)
        return;
    if (i < 0)
    {
        audio.setProcessed (false);   // the master as it is
        audio.setGainB (1.0f);
    }
    else
    {
        audio.setGainB (juce::Decibels::decibelsToGain (state.master->services[(size_t) i].gainDb));
        audio.setProcessed (true);
    }
    refresh();
}

void ToolsPage::refresh()
{
    if (state.master != shownMaster)
    {
        shownMaster = state.master;
        serviceButtons.clear();
        if (shownMaster != nullptr)
        {
            auto* orig = serviceButtons.add (new juce::TextButton ("As mastered"));
            orig->onClick = [this] { selectService (-1); };
            for (size_t i = 0; i < shownMaster->services.size(); ++i)
            {
                auto* b = serviceButtons.add (new juce::TextButton (shownMaster->services[i].service->name));
                b->onClick = [this, i] { selectService ((int) i); };
            }
            for (auto* b : serviceButtons)
                content().addAndMakeVisible (b);
            if (audio.owner == this)
                claimAudio();
        }
    }
    for (int i = 0; i < serviceButtons.size(); ++i)
        variant (*serviceButtons[i], i - 1 == selected ? "primary" : "ghost", theme::ok);
    chainChoice.setButtonText (state.batchChain.existsAsFile() ? state.batchChain.getFileNameWithoutExtension() : juce::String ("Choose a chain"));
    render.setEnabled (! state.batchBusy && state.batchChain.existsAsFile() && ! state.batchFiles.isEmpty());
    render.setButtonText (state.batchBusy ? "Rendering..." : "Render " + juce::String (state.batchFiles.size()) + (state.batchFiles.size() == 1 ? " take" : " takes"));
    playMaster.setVisible (state.master != nullptr);
    const bool playing = audio.isPlaying() && audio.owner == this;
    playMaster.setButtonText (playing ? "Stop" : "Play");
    playMaster.getProperties().set ("icon", playing ? "stop" : "play");
    spinner.setActive (state.batchBusy || state.masterBusy);
    spinner.setProgress (state.batchBusy ? state.batchProgress : -1.0f);
    Page::refresh();
}

int ToolsPage::layout (int width)
{
    const int m = kMargin;
    int y = kHeader + 6;
    spinner.setBounds (width - m - 36, 30, 30, 30);

    // Batch Render
    const int rows = juce::jmax (state.batchFiles.size(), (int) state.batchResults.size());
    batchCard = { m, y, width - 2 * m, 150 + 24 * juce::jmin (rows, 14) + (rows > 14 ? 24 : 0) };
    {
        auto r = batchCard.reduced (22, 0).withTrimmedTop (64).withHeight (36);
        chainChoice.setBounds (r.removeFromLeft (300));
        r.removeFromLeft (10);
        addFiles.setBounds (r.removeFromLeft (150));
        r.removeFromLeft (6);
        clearFiles.setBounds (r.removeFromLeft (80));
        showOutput.setBounds (r.removeFromRight (130));
        r.removeFromRight (10);
        render.setBounds (r.removeFromRight (180));
    }
    y = batchCard.getBottom() + 22;

    // Streaming Preview
    const bool hasMaster = state.master != nullptr;
    masterCard = { m, y, width - 2 * m, hasMaster ? 124 + 34 * (int) state.master->services.size() + 76 : 130 };
    chooseMaster.setBounds (masterCard.getX() + 22, masterCard.getY() + 64, 200, 36);
    playMaster.setBounds (masterCard.getX() + 232, masterCard.getY() + 64, 110, 36);
    {
        int bx = masterCard.getX() + 22;
        const int by = masterCard.getBottom() - 56;
        for (auto* b : serviceButtons)
        {
            const int w = juce::jmax (104, (int) juce::GlyphArrangement::getStringWidth (theme::bodyBold(), b->getButtonText()) + 30);
            b->setBounds (bx, by, w, 34);
            bx += w + 8;
        }
    }
    y = masterCard.getBottom() + 22;

    // Backup & Move
    backupCard = { m, y, width - 2 * m, 150 };
    exportButton.setBounds (backupCard.getX() + 22, backupCard.getY() + 64, 210, 36);
    importButton.setBounds (backupCard.getX() + 242, backupCard.getY() + 64, 190, 36);
    return backupCard.getBottom() + 40;
}

void ToolsPage::paintContent (juce::Graphics& g)
{
    paintHeader (g, getWidth(), "Tools", "Render many takes through a chain, hear a master the way streaming services play it, and take your sound to another computer.");
    auto title = [&] (juce::Rectangle<int> r, const juce::String& t, const juce::String& sub)
    {
        g.setColour (theme::text);
        g.setFont (theme::heading());
        g.drawText (t, r.getX() + 22, r.getY() + 16, r.getWidth() - 44, 24, juce::Justification::centredLeft);
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawFittedText (sub, r.getX() + 22, r.getY() + 40, r.getWidth() - 44, 18, juce::Justification::centredLeft, 1, 0.9f);
    };

    // Batch Render
    theme::card (g, batchCard.toFloat(), theme::accent);
    title (batchCard, "Batch Render", "Every take through one chain, rendered as 24-bit WAV into Documents/Axell Signature/Rendered. Drop files here to add them.");
    {
        int y = batchCard.getY() + 112;
        const int x = batchCard.getX() + 22, w = batchCard.getWidth() - 44;
        g.setFont (theme::small());
        if (! state.batchResults.empty())
        {
            int shownRows = 0;
            for (const auto& r : state.batchResults)
            {
                if (shownRows++ >= 14)
                    break;
                const bool ok = r.ok;
                theme::icon (g, ok ? "check" : "warning", { (float) x, (float) y + 2.0f, 15.0f, 15.0f }, ok ? theme::ok : theme::warn, 2.0f);
                g.setColour (theme::text);
                g.drawText (r.source.getFileName(), x + 24, y, w / 3, 20, juce::Justification::centredLeft);
                g.setColour (theme::dim);
                const auto detail = ok ? juce::String (r.inLufs, 1) + " -> " + juce::String (r.outLufs, 1) + " LUFS, peak " + juce::String (r.outTruePeakDbtp, 1)
                                             + " dBTP   " + r.output.getFileName()
                                       : r.message;
                g.drawFittedText (detail, x + 24 + w / 3, y, w - w / 3 - 24, 20, juce::Justification::centredLeft, 1, 0.85f);
                y += 24;
            }
        }
        else if (state.batchFiles.isEmpty())
        {
            g.setColour (theme::faint);
            g.drawText ("No takes yet: add some, or drop audio files on this page.", x, y, w, 20, juce::Justification::centredLeft);
        }
        else
        {
            int shownRows = 0;
            for (const auto& f : state.batchFiles)
            {
                if (shownRows++ >= 14)
                    break;
                g.setColour (theme::dim);
                g.drawText (f.getFileName(), x + 24, y, w - 24, 20, juce::Justification::centredLeft);
                y += 24;
            }
        }
        if (state.batchBusy)
        {
            g.setColour (theme::dim);
            g.drawText (state.batchStatus, x, batchCard.getBottom() - 30, w, 18, juce::Justification::centredLeft);
        }
    }

    // Streaming Preview
    theme::card (g, masterCard.toFloat(), theme::ok);
    title (masterCard, "Streaming Preview", "What Spotify, Apple Music, YouTube, Tidal and Amazon Music do to your master's level - and how it sounds there.");
    if (state.masterError.isNotEmpty())
    {
        g.setColour (theme::problem);
        g.setFont (theme::small());
        g.drawText (state.masterError, masterCard.getX() + 360, masterCard.getY() + 72, masterCard.getWidth() - 380, 20, juce::Justification::centredLeft);
    }
    if (const auto m = state.master)
    {
        const auto& me = m->measurement;
        float x = (float) masterCard.getX() + 360.0f;
        for (const auto& [label, value] : std::initializer_list<std::pair<const char*, juce::String>> {
                 { "LOUDNESS", juce::String (me.integratedLufs, 1) + " LUFS" }, { "TRUE PEAK", juce::String (me.truePeakDbtp, 1) + " dBTP" },
                 { "RANGE", juce::String (me.rangeLu, 1) + " LU" }, { "LOUDEST 3 S", juce::String (me.maxShortTermLufs, 1) + " LUFS" } })
        {
            g.setColour (theme::dim);
            g.setFont (theme::label());
            g.drawText (label, (int) x, masterCard.getY() + 62, 150, 14, juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.setFont (ui::font (18.0f, 2));
            g.drawText (value, (int) x, masterCard.getY() + 78, 150, 24, juce::Justification::centredLeft);
            x += 156.0f;
        }
        int y = masterCard.getY() + 124;
        for (size_t i = 0; i < m->services.size(); ++i)
        {
            const auto& sv = m->services[i];
            const int x0 = masterCard.getX() + 22;
            g.setColour ((int) i == selected ? theme::ok : theme::text);
            g.setFont (theme::bodyBold());
            g.drawText (sv.service->name, x0, y, 150, 22, juce::Justification::centredLeft);
            const float gr = std::abs (sv.gainDb) < 0.05f ? 0.0f : sv.gainDb;   // never "-0.0"
            const auto gain = (gr > 0.0f ? "+" : "") + juce::String (gr, 1) + " dB";
            g.setColour (sv.gainDb < -0.05f ? theme::warn : (sv.gainDb > 0.05f ? theme::info : theme::ok));
            g.drawText (gain, x0 + 150, y, 90, 22, juce::Justification::centredLeft);
            g.setColour (theme::dim);
            g.setFont (theme::small());
            juce::String what = sv.headroomLostDb > 0.05f ? "turned down " + juce::String (sv.headroomLostDb, 1) + " dB: the extra loudness buys nothing there. "
                                                          : (sv.gainDb > 0.05f ? juce::String ("turned up. ") : juce::String ("played as it is. "));
            g.drawFittedText (what + sv.service->note, x0 + 240, y, masterCard.getWidth() - 284, 22, juce::Justification::centredLeft, 1, 0.85f);
            y += 34;
        }
    }

    // Backup & Move
    theme::card (g, backupCard.toFloat(), theme::teal);
    title (backupCard, "Backup & Move", "Your plugin presets, Voice Profiles and REAPER chains, with a checksum for every file. A restore never replaces a file.");
    const auto items = tools::backupItems (state.getPaths());
    g.setColour (state.backupMessage.isNotEmpty() ? (state.backupOk ? theme::ok : theme::warn) : theme::dim);
    g.setFont (theme::small());
    g.drawFittedText (state.backupMessage.isNotEmpty() ? state.backupMessage : juce::String ((int) items.size()) + " files to back up on this computer.",
                      backupCard.getX() + 22, backupCard.getY() + 112, backupCard.getWidth() - 44, 20, juce::Justification::centredLeft, 1, 0.85f);
}
} // namespace dojo::gui
