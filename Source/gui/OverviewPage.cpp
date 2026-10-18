#include "Pages.h"
#include "DojoAssets.h"

namespace dojo::gui
{
juce::Image productImage (const Product& p)
{
    int size = 0;
    if (auto* data = DojoAssets::getNamedResource (p.image.toRawUTF8(), size))
        return juce::ImageCache::getFromMemory (data, size);
    return {};
}

OverviewPage::OverviewPage (AppState& s) : state (s)
{
    for (auto* b : { &toPlugins, &toWorkstation, &toProfile })
    {
        variant (*b, "ghost");
        content().addAndMakeVisible (*b);
    }
    toPlugins.onClick = [this] { navigate (plugins); };
    toWorkstation.onClick = [this] { if (! state.checksRun) state.runChecks(); navigate (workstation); };
    toProfile.onClick = [this] { navigate (profile); };
    for (const auto& p : Catalog::products())
        thumbs[p.key] = productImage (p);
}

std::vector<OverviewPage::Step> OverviewPage::nextSteps() const
{
    std::vector<Step> steps;
    for (const auto& p : state.products)
    {
        if (p.updateAvailable())
            steps.push_back ({ "Update " + p.product->name + " to " + juce::String (p.latest()->version.toString()) + ".", theme::accent, plugins });
        else if (p.installed.brokenLink)
            steps.push_back ({ p.product->name + " points to a build that no longer exists: install it again.", theme::problem, plugins });
        else if (! p.installed.present)
            steps.push_back ({ "Install " + p.product->name + ": " + p.product->role.toLowerCase() + ".", theme::info, plugins });
    }
    for (const auto& c : state.checks)
        if (c.status == Check::Status::problem || c.status == Check::Status::warning)
            steps.push_back ({ c.title + ": " + c.detail.upToFirstOccurrenceOf (".", true, false), theme::statusColour (c.status), workstation });
    if (state.profile == nullptr && state.savedProfiles().isEmpty())
        steps.push_back ({ "Create your Voice Profile: 30 seconds of your voice give every plugin settings made for you.", theme::teal, profile });
    int inReaper = 0;
    for (const auto& t : ChainTemplate::all())
        inReaper += state.chainInstalled (t) ? 1 : 0;
    if (inReaper < (int) ChainTemplate::all().size())
        steps.push_back ({ "Add the " + juce::String ((int) ChainTemplate::all().size() - inReaper)
                               + " ready-made REAPER chains: vocals, beats and master in one click.", theme::accent, chains });
    if (! state.checksRun)
        steps.insert (steps.begin(), { "Check your workstation: plugin folders, DAW, audio buffer and realtime priority.", theme::info, workstation });
    if (steps.size() > 6)
        steps.resize (6);
    return steps;
}

void OverviewPage::refresh()
{
    const auto steps = nextSteps();
    stepButtons.clear();
    for (const auto& s : steps)
    {
        auto* b = stepButtons.add (new juce::TextButton ("Open"));
        variant (*b, "quiet");
        b->getProperties().set ("icon", "arrow");
        b->setButtonText ({});
        const int sec = s.section;
        b->onClick = [this, sec] { if (sec == workstation && ! state.checksRun) state.runChecks(); navigate (sec); };
        content().addAndMakeVisible (b);
    }
    toWorkstation.setButtonText (state.checksRun ? "Open workstation" : "Run checks");
    toProfile.setButtonText (state.profile != nullptr ? "Open profile" : "Create profile");
    toPlugins.setButtonText (state.updatesAvailable() > 0 ? "Update plugins" : "Open plugins");
    Page::refresh();
}

int OverviewPage::layout (int width)
{
    const int m = kMargin, gap = 18;
    int y = kHeader + 8;
    const int cw = (width - 2 * m - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i)
        cards[i] = { m + i * (cw + gap), y, cw, 210 };
    toPlugins.setBounds (cards[0].getX() + 20, cards[0].getBottom() - 54, 170, 36);
    toWorkstation.setBounds (cards[1].getX() + 20, cards[1].getBottom() - 54, 170, 36);
    toProfile.setBounds (cards[2].getX() + 20, cards[2].getBottom() - 54, 170, 36);
    y += 210 + 34;

    // the suite strip
    strip = { m, y, width - 2 * m, 0 };
    tiles.clear();
    const int n = (int) Catalog::products().size();
    const int tw = (width - 2 * m - (n - 1) * 14) / n;
    for (int i = 0; i < n; ++i)
        tiles.push_back ({ m + i * (tw + 14), y + 30, tw, (int) (tw * 0.62f) + 64 });
    y = tiles.back().getBottom() + 34;

    // next steps
    const int rows = stepButtons.size();
    stepsArea = { m, y, width - 2 * m, 54 + juce::jmax (1, rows) * 46 };
    for (int i = 0; i < rows; ++i)
        stepButtons[i]->setBounds (stepsArea.getRight() - 58, stepsArea.getY() + 50 + i * 46, 40, 34);
    return stepsArea.getBottom() + 40;
}

void OverviewPage::paintContent (juce::Graphics& g)
{
    paintHeader (g, getWidth(), "Welcome to the Dojo", "Your Axell Signature suite, your workstation and your voice, at a glance.");

    // 1: suite
    {
        const auto r = cards[0].toFloat();
        theme::card (g, r, theme::accent);
        const int inst = state.installedCount(), total = (int) state.products.size();
        theme::ring (g, { r.getRight() - 62.0f, r.getY() + 70.0f }, 34.0f, 7.0f, (float) inst / (float) total, theme::accent);
        g.setColour (theme::text);
        g.setFont (ui::font (20.0f, 2));
        g.drawText (juce::String (inst) + "/" + juce::String (total), juce::Rectangle<float> (68.0f, 68.0f).withCentre ({ r.getRight() - 62.0f, r.getY() + 70.0f }),
                    juce::Justification::centred);
        g.setFont (theme::label());
        g.setColour (theme::dim);
        g.drawText ("SUITE", r.getX() + 20.0f, r.getY() + 20.0f, 200.0f, 16.0f, juce::Justification::centredLeft);
        g.setFont (theme::heading());
        g.setColour (theme::text);
        g.drawText (juce::String (inst) + (inst == 1 ? " plugin installed" : " plugins installed"), r.getX() + 20.0f, r.getY() + 44.0f, r.getWidth() - 120.0f, 24.0f,
                    juce::Justification::centredLeft);
        const int up = state.updatesAvailable();
        const juce::String tag = ! state.releasesChecked ? "Updates not checked" : (up > 0 ? juce::String (up) + (up == 1 ? " update" : " updates") : "Up to date");
        const auto col = ! state.releasesChecked ? theme::dim : (up > 0 ? theme::accent : theme::ok);
        theme::chip (g, { r.getX() + 20.0f, r.getY() + 80.0f, theme::chipWidth (tag), 22.0f }, tag, col);
        juce::StringArray ups;
        for (const auto& p : state.products)
            if (p.updateAvailable())
                ups.add (p.product->name.fromFirstOccurrenceOf ("Axell ", false, false) + " " + juce::String (p.latest()->version.toString()));
        const auto line = ! state.releasesChecked ? juce::String ("Open Plugins to check the releases on GitHub or a USB folder.")
                          : (ups.isEmpty() ? "Every installed plugin is the newest release." : "Waiting: " + ups.joinIntoString (", ") + ".");
        theme::paragraph (g, line, { r.getX() + 20.0f, r.getY() + 114.0f, r.getWidth() - 40.0f, 40.0f }, theme::small(), theme::dim, 2);
    }
    // 2: workstation
    {
        const auto r = cards[1].toFloat();
        theme::card (g, r, theme::info);
        g.setFont (theme::label());
        g.setColour (theme::dim);
        g.drawText ("WORKSTATION", r.getX() + 20.0f, r.getY() + 20.0f, 200.0f, 16.0f, juce::Justification::centredLeft);
        g.setFont (theme::heading());
        g.setColour (theme::text);
        if (! state.checksRun)
            g.drawText (state.checksBusy ? "Checking..." : "Not checked yet", r.getX() + 20.0f, r.getY() + 44.0f, r.getWidth() - 40.0f, 24.0f, juce::Justification::centredLeft);
        else
        {
            const int bad = state.countChecks (Check::Status::problem), warn = state.countChecks (Check::Status::warning);
            g.drawText (bad + warn == 0 ? "Ready for the suite" : juce::String (bad + warn) + (bad + warn == 1 ? " thing to look at" : " things to look at"),
                        r.getX() + 20.0f, r.getY() + 44.0f, r.getWidth() - 40.0f, 24.0f, juce::Justification::centredLeft);
            float x = r.getX() + 20.0f;
            for (auto st : { Check::Status::ok, Check::Status::info, Check::Status::warning, Check::Status::problem })
            {
                const int n = state.countChecks (st);
                if (n == 0)
                    continue;
                const auto t = juce::String (n) + " " + theme::statusName (st);
                theme::chip (g, { x, r.getY() + 80.0f, theme::chipWidth (t), 22.0f }, t, theme::statusColour (st));
                x += theme::chipWidth (t) + 8.0f;
            }
            // the first thing to look at, or what the computer is
            juce::String line;
            for (const auto& c : state.checks)
                if (line.isEmpty() && (c.status == Check::Status::problem || c.status == Check::Status::warning))
                    line = c.title + ": " + c.detail;
            for (const auto& c : state.checks)
                if (line.isEmpty() && c.id == "system")
                    line = c.detail;
            theme::paragraph (g, line, { r.getX() + 20.0f, r.getY() + 114.0f, r.getWidth() - 40.0f, 40.0f }, theme::small(), theme::dim, 2);
        }
    }
    // 3: voice
    {
        const auto r = cards[2].toFloat();
        theme::card (g, r, theme::teal);
        g.setFont (theme::label());
        g.setColour (theme::dim);
        g.drawText ("VOICE PROFILE", r.getX() + 20.0f, r.getY() + 20.0f, 200.0f, 16.0f, juce::Justification::centredLeft);
        g.setFont (theme::heading());
        g.setColour (theme::text);
        if (state.profile != nullptr)
        {
            const auto& p = state.profile->profile;
            g.drawText (state.take.getFileNameWithoutExtension(), r.getX() + 20.0f, r.getY() + 44.0f, r.getWidth() - 40.0f, 24.0f, juce::Justification::centredLeft);
            g.setFont (theme::small());
            g.setColour (theme::dim);
            g.drawText ("Pitch " + juce::String (juce::roundToInt (p.f0Low)) + "-" + juce::String (juce::roundToInt (p.f0High)) + " Hz  |  " + juce::String (p.integratedLufs, 1)
                            + " LUFS  |  room " + juce::String (p.rt60, 2) + " s",
                        r.getX() + 20.0f, r.getY() + 78.0f, r.getWidth() - 40.0f, 20.0f, juce::Justification::centredLeft);
        }
        else
        {
            const auto saved = state.savedProfiles();
            g.drawText (saved.isEmpty() ? "Not created yet" : saved[0].getFileNameWithoutExtension(), r.getX() + 20.0f, r.getY() + 44.0f,
                        r.getWidth() - 40.0f, 24.0f, juce::Justification::centredLeft);
            theme::paragraph (g, saved.isEmpty() ? "Record 30 seconds of your voice: the Dojo measures it and tunes the vocal chain to it."
                                                 : "Saved " + saved[0].getLastModificationTime().formatted ("%d %b %Y") + ". Open it to listen, re-tune or save again.",
                              { r.getX() + 20.0f, r.getY() + 76.0f, r.getWidth() - 40.0f, 60.0f }, theme::small(), theme::dim, 3);
        }
    }

    // suite strip
    g.setColour (theme::text);
    g.setFont (theme::heading());
    g.drawText ("The suite", strip.getX(), strip.getY(), 300, 22, juce::Justification::centredLeft);
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& ps = state.products[i];
        const auto r = tiles[i].toFloat();
        theme::card (g, r, ps.product->accent);
        auto img = thumbs[ps.product->key];
        auto ir = r.reduced (10.0f).withHeight (r.getWidth() * 0.62f - 20.0f).withY (r.getY() + 12.0f);
        if (img.isValid())
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (ir, 7.0f);
            g.reduceClipRegion (clip);
            g.setOpacity (ps.installed.present ? 1.0f : 0.35f);
            g.drawImage (img, ir, juce::RectanglePlacement::fillDestination | juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop);
            g.restoreState();
        }
        auto t = r.withTop (ir.getBottom() + 8.0f).reduced (12.0f, 0.0f);
        g.setColour (theme::text);
        g.setFont (theme::bodyBold().withHeight (13.5f));
        g.drawFittedText (ps.product->name.fromFirstOccurrenceOf ("Axell ", false, false), t.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        const juce::String status = ps.installed.present ? (ps.updateAvailable() ? "Update " + juce::String (ps.latest()->version.toString())
                                                                                  : "v" + juce::String (ps.installed.version.toString()))
                                                         : "Not installed";
        const auto col = ps.installed.present ? (ps.updateAvailable() ? theme::accent : theme::ok) : theme::faint;
        g.setColour (col);
        g.fillEllipse (t.getX(), t.getY() + 7.0f, 6.0f, 6.0f);
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawText (status, t.withTrimmedLeft (12.0f).removeFromTop (20.0f), juce::Justification::centredLeft);
    }

    // next steps
    const auto sr = stepsArea.toFloat();
    theme::card (g, sr);
    g.setColour (theme::text);
    g.setFont (theme::heading());
    g.drawText ("Next steps", sr.getX() + 20.0f, sr.getY() + 16.0f, 300.0f, 24.0f, juce::Justification::centredLeft);
    const auto steps = nextSteps();
    if (steps.empty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::body());
        g.drawText ("Everything is in place. Make music.", sr.getX() + 20.0f, sr.getY() + 54.0f, sr.getWidth() - 40.0f, 24.0f, juce::Justification::centredLeft);
    }
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const float y = sr.getY() + 50.0f + 46.0f * (float) i;
        if (i > 0)
        {
            g.setColour (theme::line);
            g.drawHorizontalLine ((int) y - 6, sr.getX() + 20.0f, sr.getRight() - 20.0f);
        }
        g.setColour (steps[i].colour);
        g.fillEllipse (sr.getX() + 22.0f, y + 13.0f, 8.0f, 8.0f);
        g.setColour (theme::text);
        g.setFont (theme::body());
        g.drawFittedText (steps[i].text, juce::Rectangle<float> (sr.getX() + 42.0f, y, sr.getWidth() - 120.0f, 34.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.9f);
    }
}
} // namespace dojo::gui
