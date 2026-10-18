#include "Pages.h"

namespace dojo::gui
{
namespace
{
const char* iconFor (Check::Status s)
{
    switch (s)
    {
        case Check::Status::ok:      return "check";
        case Check::Status::info:    return "info";
        case Check::Status::warning: return "warning";
        case Check::Status::problem: return "problem";
    }
    return "info";
}
constexpr int kTextLeft = 64, kFixW = 136;
} // namespace

//== CheckRow ====================================================================================================
CheckRow::CheckRow (AppState& s, Check c) : state (s), check (std::move (c))
{
    for (const auto& f : check.fixes)
    {
        auto* b = fixes.add (new juce::TextButton (f.label));
        variant (*b, "ghost", theme::statusColour (check.status == Check::Status::ok ? Check::Status::info : check.status));
        const Fix fix = f;
        b->onClick = [this, fix]
        {
            auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                            .withTitle (check.title + ": " + fix.label)
                            .withMessage (fix.confirm + (fix.undo.isNotEmpty() ? "\n\nTo undo: " + fix.undo : juce::String()))
                            .withButton ("Do it").withButton ("Cancel");
            juce::AlertWindow::showAsync (opts, [this, fix] (int r) { if (r == 1) state.applyFix (fix); });
        };
        addAndMakeVisible (b);
    }
}

int CheckRow::heightFor (int width)
{
    const float tw = (float) (width - kTextLeft - 24 - (fixes.isEmpty() ? 0 : kFixW + 16));
    float h = 18.0f + 22.0f + theme::paragraphHeight (check.detail, tw, theme::body(), 8);
    if (check.manual.isNotEmpty())
        h += 10.0f + theme::paragraphHeight (check.manual, tw, theme::small(), 6);
    const float fixH = (float) fixes.size() * 40.0f + 24.0f;
    return (int) std::ceil (juce::jmax (h + 18.0f, fixH, 76.0f));
}

void CheckRow::resized()
{
    int y = 16;
    for (auto* b : fixes)
    {
        b->setBounds (getWidth() - kFixW - 18, y, kFixW, 32);
        y += 40;
    }
}

void CheckRow::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto col = theme::statusColour (check.status);
    theme::card (g, r);
    g.setColour (col.withAlpha (0.14f));
    g.fillEllipse (r.getX() + 16.0f, r.getY() + 16.0f, 34.0f, 34.0f);
    theme::icon (g, iconFor (check.status), { r.getX() + 23.0f, r.getY() + 23.0f, 20.0f, 20.0f }, col, 2.0f);
    const float tw = r.getWidth() - (float) kTextLeft - 24.0f - (fixes.isEmpty() ? 0.0f : (float) kFixW + 16.0f);
    float y = r.getY() + 16.0f;
    g.setColour (theme::text);
    g.setFont (theme::bodyBold().withHeight (15.0f));
    g.drawText (check.title, (int) (r.getX() + kTextLeft), (int) y, (int) tw, 22, juce::Justification::centredLeft);
    y += 24.0f;
    y += theme::paragraph (g, check.detail, { r.getX() + kTextLeft, y, tw, 400.0f }, theme::body(), theme::dim, 8);
    if (check.manual.isNotEmpty())
        theme::paragraph (g, check.manual, { r.getX() + kTextLeft, y + 8.0f, tw, 200.0f }, theme::small(), col.brighter (0.3f), 6);
}

//== WorkstationPage =============================================================================================
WorkstationPage::WorkstationPage (AppState& s) : state (s)
{
    variant (rerun, "ghost");
    rerun.getProperties().set ("icon", "refresh");
    rerun.setButtonText ("Run the checks again");
    rerun.onClick = [this] { state.runChecks(); };
    content().addAndMakeVisible (rerun);
    content().addChildComponent (spinner);
}

void WorkstationPage::refresh()
{
    if (! state.checksRun && ! state.checksBusy)
        state.runChecks();
    rerun.setEnabled (! state.checksBusy);
    spinner.setActive (state.checksBusy);
    if (shownGeneration != state.checksGeneration)   // a new set of results
    {
        rows.clear();
        for (const auto& c : state.checks)
            content().addAndMakeVisible (rows.add (new CheckRow (state, c)));
        shownGeneration = state.checksGeneration;
    }
    Page::refresh();
}

int WorkstationPage::layout (int width)
{
    const int m = kMargin;
    rerun.setBounds (width - m - 210, 24, 210, 34);
    spinner.setBounds (width - m - 248, 29, 24, 24);
    int y = kHeader + 50;
    groupTitles.clear();
    juce::String group;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto& c = state.checks[(size_t) i];
        if (c.group != group)
        {
            group = c.group;
            groupTitles.push_back ({ group, y });
            y += 34;
        }
        const int h = rows[i]->heightFor (width - 2 * m);
        rows[i]->setBounds (m, y, width - 2 * m, h);
        y += h + 10;
    }
    return y + 30;
}

void WorkstationPage::paintContent (juce::Graphics& g)
{
    paintHeader (g, getWidth(), "Workstation", "Is this computer ready for the suite? Checks only read; a fix runs in your account only after you confirm it.");
    // summary chips
    float x = (float) kMargin;
    const float y = (float) kHeader + 4.0f;
    if (state.checksBusy && state.checks.empty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::body());
        g.drawText ("Checking...", kMargin, (int) y, 300, 24, juce::Justification::centredLeft);
    }
    for (auto st : { Check::Status::ok, Check::Status::info, Check::Status::warning, Check::Status::problem })
    {
        const int n = state.countChecks (st);
        if (n == 0)
            continue;
        const auto t = juce::String (n) + " " + theme::statusName (st);
        theme::chip (g, { x, y, theme::chipWidth (t), 24.0f }, t, theme::statusColour (st));
        x += theme::chipWidth (t) + 8.0f;
    }
    if (state.fixMessage.isNotEmpty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawText ("Last fix: " + state.fixMessage, (int) x + 12, (int) y, getWidth() - (int) x - kMargin - 12, 24, juce::Justification::centredLeft);
    }
    for (const auto& [title, ty] : groupTitles)
    {
        g.setColour (theme::dim);
        g.setFont (theme::label());
        g.drawText (title.toUpperCase(), kMargin, ty + 8, 300, 18, juce::Justification::centredLeft);
    }
}
} // namespace dojo::gui
