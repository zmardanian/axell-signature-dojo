#include "Pages.h"

namespace dojo::gui
{
juce::Image productImage (const Product&);

//== ProductCard =================================================================================================
ProductCard::ProductCard (AppState& s, const juce::String& k) : state (s), key (k)
{
    if (auto* ps = state.product (key))
        thumb = productImage (*ps->product);
    variant (main, "primary", state.product (key)->product->accent);
    variant (versions, "ghost");
    versions.setButtonText ("Versions");
    main.onClick = [this]
    {
        auto* ps = state.product (key);
        if (ps == nullptr)
            return;
        if (ps->latest() != nullptr)
            state.install (key, *ps->latest());
        else
            state.checkReleases();
    };
    versions.onClick = [this] { showVersions(); };
    progress.setColour (juce::ProgressBar::foregroundColourId, state.product (key)->product->accent);
    addAndMakeVisible (main);
    addAndMakeVisible (versions);
    addChildComponent (progress);
    update();
}

void ProductCard::update()
{
    const auto* ps = state.product (key);
    const auto* latest = ps->latest();
    juce::String label;
    if (! ps->installed.present)
        label = latest != nullptr ? "Install " + juce::String (latest->version.toString()) : "Find releases";
    else if (ps->updateAvailable())
        label = "Update to " + juce::String (latest->version.toString());
    else
        label = latest != nullptr ? "Reinstall " + juce::String (latest->version.toString()) : "Check for updates";
    main.setButtonText (label);
    const bool action = ps->updateAvailable() || ! ps->installed.present;
    variant (main, action ? "primary" : "ghost", ps->product->accent);
    main.getProperties().set ("icon", action ? "download" : "refresh");
    main.setEnabled (! ps->busy && ! state.releasesBusy);
    versions.setEnabled (! ps->busy);
    progress.setVisible (ps->busy);
    progressValue = ps->progress;
    repaint();
}

void ProductCard::resized()
{
    auto r = getLocalBounds().reduced (20);
    const int imgW = (int) (getWidth() * 0.44f);
    r.removeFromLeft (imgW);
    auto bottom = r.removeFromBottom (38);
    versions.setBounds (bottom.removeFromRight (104));
    bottom.removeFromRight (10);
    main.setBounds (bottom.removeFromRight (juce::jmin (196, bottom.getWidth())));
    progress.setBounds (r.getX(), getHeight() - 74, r.getWidth(), 8);
}

void ProductCard::paint (juce::Graphics& g)
{
    const auto* ps = state.product (key);
    const auto& p = *ps->product;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::card (g, r, p.accent, hover);

    // screenshot, with the accent's glow behind it
    const float imgW = getWidth() * 0.44f;
    auto ir = juce::Rectangle<float> (r.getX() + 20.0f, r.getY() + 22.0f, imgW - 26.0f, r.getHeight() - 44.0f);
    if (thumb.isValid())
    {
        const float aspect = (float) thumb.getHeight() / (float) thumb.getWidth();
        ir = ir.withSizeKeepingCentre (ir.getWidth(), juce::jmin (ir.getHeight(), ir.getWidth() * aspect));
        g.setColour (p.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (ir.expanded (6.0f).translated (0.0f, 4.0f), 12.0f);
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (ir, 8.0f);
        g.reduceClipRegion (clip);
        g.setOpacity (ps->installed.present ? 1.0f : 0.55f);
        g.drawImage (thumb, ir, juce::RectanglePlacement::fillDestination);
        g.restoreState();
        g.setColour (theme::line.brighter (0.2f));
        g.drawRoundedRectangle (ir, 8.0f, 1.0f);
    }

    auto t = r.withTrimmedLeft (imgW + 14.0f).reduced (6.0f, 22.0f);
    g.setColour (theme::text);
    g.setFont (ui::font (20.0f, 2));
    g.drawText (p.name, t.removeFromTop (26.0f), juce::Justification::centredLeft);
    g.setColour (p.accent.brighter (0.2f));
    g.setFont (theme::label());
    g.drawText (p.tagline.toUpperCase(), t.removeFromTop (18.0f), juce::Justification::centredLeft);
    t.removeFromTop (10.0f);

    // status chips
    juce::String status;
    juce::Colour sc;
    if (ps->installed.brokenLink)        { status = "Broken link"; sc = theme::problem; }
    else if (! ps->installed.present)    { status = "Not installed"; sc = theme::faint; }
    else if (ps->updateAvailable())      { status = "Update available"; sc = theme::accent; }
    else                                 { status = "Installed " + juce::String (ps->installed.version.toString()); sc = theme::ok; }
    float x = t.getX();
    theme::chip (g, { x, t.getY(), theme::chipWidth (status), 22.0f }, status, sc);
    x += theme::chipWidth (status) + 8.0f;
    theme::chip (g, { x, t.getY(), theme::chipWidth (p.stage), 22.0f }, p.stage, theme::dim);
    t.removeFromTop (34.0f);

    g.setColour (theme::dim);
    g.setFont (theme::body());
    g.drawText (p.role + ".", t.removeFromTop (22.0f), juce::Justification::centredLeft);
    if (const auto* latest = ps->latest())
    {
        g.setFont (theme::small());
        g.setColour (theme::faint);
        g.drawText ("Latest " + juce::String (latest->version.toString()) + (latest->published.isNotEmpty() ? ", " + latest->published : juce::String())
                        + (latest->isLocal() ? "  (from your folder)" : "  (GitHub)"),
                    t.removeFromTop (20.0f), juce::Justification::centredLeft);
    }
    if (ps->busy)
    {
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawText (ps->status, (int) t.getX(), getHeight() - 96, (int) t.getWidth(), 18, juce::Justification::centredLeft);
    }
    else if (ps->message.isNotEmpty())
    {
        g.setColour (ps->messageOk ? theme::ok : theme::problem);
        g.setFont (theme::small());
        g.drawFittedText (ps->message, juce::Rectangle<float> (t.getX(), (float) getHeight() - 100.0f, t.getWidth(), 36.0f).toNearestInt(),
                          juce::Justification::bottomLeft, 2, 0.9f);
    }
}

void ProductCard::showVersions()
{
    const auto* ps = state.product (key);
    juce::PopupMenu m;
    m.addSectionHeader (ps->product->name);
    juce::PopupMenu rel;
    for (int i = 0; i < ps->releases.size(); ++i)
    {
        const auto& r = ps->releases.getReference (i);
        rel.addItem (100 + i, "Install " + juce::String (r.version.toString()) + (r.published.isNotEmpty() ? "   " + r.published : juce::String()),
                     true, ps->installed.present && r.version == ps->installed.version);
    }
    if (ps->releases.isEmpty())
        rel.addItem (1, state.releasesChecked ? "No releases found" : "Check for updates first", false);
    m.addSubMenu ("Releases", rel);
    juce::PopupMenu bk;
    for (int i = 0; i < ps->backups.size(); ++i)
    {
        const auto& b = ps->backups.getReference (i);
        bk.addItem (200 + i, "Restore " + juce::String (b.version.valid ? b.version.toString() : "unknown version") + "   (kept "
                                 + b.when.formatted ("%d %b %H:%M") + ")");
    }
    if (ps->backups.isEmpty())
        bk.addItem (2, "No earlier versions kept", false);
    m.addSubMenu ("Earlier versions", bk);
    m.addSeparator();
    m.addItem (3, "Remove " + ps->product->name, ps->installed.present || ps->installed.brokenLink);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (versions), [this] (int r)
    {
        auto* st = state.product (key);
        if (st == nullptr)
            return;
        if (r >= 100 && r < 200 && r - 100 < st->releases.size())
            state.install (key, st->releases[r - 100]);
        else if (r >= 200 && r - 200 < st->backups.size())
            state.restore (key, st->backups[r - 200]);
        else if (r == 3)
        {
            auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                            .withTitle ("Remove " + st->product->name + "?")
                            .withMessage ("It disappears from your DAW after its next scan. A copy is kept, so \"Earlier versions\" can restore it.")
                            .withButton ("Remove").withButton ("Cancel");
            juce::AlertWindow::showAsync (opts, [this] (int b) { if (b == 1) state.uninstall (key); });
        }
    });
}

//== PluginsPage =================================================================================================
PluginsPage::PluginsPage (AppState& s) : state (s)
{
    for (auto* b : { &fromGitHub, &fromFolder })
    {
        variant (*b, "ghost", theme::accent);
        b->setClickingTogglesState (false);
        content().addAndMakeVisible (*b);
    }
    fromGitHub.setTooltip ("Install and update from the Axell Signature releases on GitHub (checked by SHA-256).");
    fromFolder.setTooltip ("Install from a folder, for example the USB stick: release archives and their SHA256SUMS.txt.");
    fromGitHub.onClick = [this] { state.setSource (AppState::Source::github); };
    fromFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Folder with Axell release archives", state.sourceFolder.isDirectory() ? state.sourceFolder
                                                                                                                              : juce::File ("/run/media"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
                state.setSource (AppState::Source::folder, fc.getResult());
        });
    };
    variant (check, "ghost");
    check.getProperties().set ("icon", "refresh");
    check.setButtonText ("Check for updates");
    check.onClick = [this] { state.checkReleases(); };
    variant (updateAll, "primary");
    updateAll.getProperties().set ("icon", "download");
    updateAll.onClick = [this]
    {
        for (auto& p : state.products)
            if (p.updateAvailable())
                state.install (p.product->key, *p.latest());
    };
    content().addAndMakeVisible (check);
    content().addChildComponent (updateAll);
    for (const auto& p : Catalog::products())
        content().addAndMakeVisible (cards.add (new ProductCard (state, p.key)));
}

void PluginsPage::refresh()
{
    const int n = state.updatesAvailable();
    updateAll.setVisible (n > 0);
    updateAll.setButtonText (n == 1 ? "Update 1 plugin" : "Update " + juce::String (n) + " plugins");
    check.setEnabled (! state.releasesBusy);
    check.setButtonText (state.releasesBusy ? "Checking..." : "Check for updates");
    fromGitHub.setToggleState (state.source == AppState::Source::github, juce::dontSendNotification);
    fromFolder.setToggleState (state.source == AppState::Source::folder, juce::dontSendNotification);
    variant (fromGitHub, state.source == AppState::Source::github ? "primary" : "ghost", theme::teal);
    variant (fromFolder, state.source == AppState::Source::folder ? "primary" : "ghost", theme::teal);
    for (auto* c : cards)
        c->update();
    Page::refresh();
}

int PluginsPage::layout (int width)
{
    const int m = kMargin;
    int y = kHeader + 4;
    fromGitHub.setBounds (m, y, 96, 34);
    fromFolder.setBounds (m + 100, y, 106, 34);
    updateAll.setBounds (width - m - 180, y, 180, 34);
    check.setBounds ((updateAll.isVisible() ? updateAll.getX() - 12 : width - m) - 184, y, 184, 34);
    y += 34 + 34;
    const int cols = width >= 900 ? 2 : 1, gap = 18;
    const int cw = (width - 2 * m - (cols - 1) * gap) / cols, ch = 250;
    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setBounds (m + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap), cw, ch);
    return y + ((cards.size() + cols - 1) / cols) * (ch + gap) + 24;
}

void PluginsPage::paintContent (juce::Graphics& g)
{
    paintHeader (g, getWidth(), "Plugins", "Install, update, restore or remove the suite. Every download is checked against its published SHA-256.");
    g.setFont (theme::small());
    const int y = kHeader + 4 + 34 + 8;
    if (state.releasesError.isNotEmpty())
    {
        g.setColour (theme::problem);
        g.drawText (state.releasesError, kMargin, y, getWidth() - 2 * kMargin, 18, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (theme::faint);
        g.drawText (state.source == AppState::Source::folder ? "Source: " + state.sourceFolder.getFullPathName()
                                                             : juce::String ("Source: github.com/") + Catalog::kOwner + " (releases with SHA256SUMS)",
                    kMargin, y, getWidth() - 2 * kMargin, 18, juce::Justification::centredLeft);
    }
}
} // namespace dojo::gui
