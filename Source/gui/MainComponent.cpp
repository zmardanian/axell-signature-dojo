#include "MainComponent.h"
#include "Pages.h"
#include "DojoAssets.h"

namespace dojo::gui
{
namespace
{
const char* const kNames[] = { "Overview", "Plugins", "Voice Profile", "Chains", "Workstation" };
const char* const kIcons[] = { "home", "plugins", "voice", "chains", "workstation" };
} // namespace

//== Sidebar =====================================================================================================
Sidebar::Sidebar (AppState& s) : state (s)
{
    logo = juce::ImageCache::getFromMemory (DojoAssets::icon_png, DojoAssets::icon_pngSize);
    spinner.setColour (theme::teal);
    addChildComponent (spinner);
    variant (scaleButton, "quiet");
    scaleButton.setTooltip ("Display size");
    scaleButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Display size");
        for (float sc : { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f })
            m.addItem (juce::roundToInt (sc * 100), juce::String (juce::roundToInt (sc * 100)) + " %", true,
                       std::abs (state.uiScale() - sc) < 0.01f);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scaleButton), [this] (int r)
        {
            if (r > 0)
            {
                state.setUiScale ((float) r / 100.0f);
                juce::Desktop::getInstance().setGlobalScaleFactor ((float) r / 100.0f);
            }
        });
    };
    addAndMakeVisible (scaleButton);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (4);
}

void Sidebar::timerCallback()
{
    const bool busy = state.busy();
    spinner.setActive (busy);
    scaleButton.setButtonText ("Display " + juce::String (juce::roundToInt (state.uiScale() * 100)) + " %");
    repaint (0, getHeight() - 120, getWidth(), 120);
}

void Sidebar::resized()
{
    spinner.setBounds (20, getHeight() - 104, 22, 22);
    scaleButton.setBounds (12, getHeight() - 46, getWidth() - 24, 30);
}

void Sidebar::paint (juce::Graphics& g)
{
    g.fillAll (theme::sidebar);
    g.setColour (theme::line);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    // brand
    g.drawImage (logo, juce::Rectangle<float> (22.0f, 30.0f, 44.0f, 44.0f), juce::RectanglePlacement::centred);
    g.setColour (theme::dim);
    g.setFont (ui::caps (10.5f, 1));
    g.drawText ("AXELL SIGNATURE", 78, 32, getWidth() - 86, 16, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (ui::font (22.0f, 2));
    g.drawText ("Dojo", 78, 48, getWidth() - 86, 28, juce::Justification::centredLeft);

    for (int i = 0; i < numSections; ++i)
    {
        const auto r = itemBounds (i).toFloat();
        const bool sel = i == selected, hov = i == hover;
        if (sel || hov)
        {
            g.setColour (sel ? theme::surfaceHi : theme::surface);
            g.fillRoundedRectangle (r, 9.0f);
        }
        if (sel)
        {
            g.setColour (theme::accent);
            g.fillRoundedRectangle (r.getX(), r.getY() + 10.0f, 3.0f, r.getHeight() - 20.0f, 1.5f);
        }
        const auto c = sel ? theme::text : (hov ? theme::text.withAlpha (0.9f) : theme::dim);
        theme::icon (g, kIcons[i], { r.getX() + 14.0f, r.getCentreY() - 10.0f, 20.0f, 20.0f }, sel ? theme::accent : c, 1.7f);
        g.setColour (c);
        g.setFont (sel ? theme::bodyBold() : theme::body());
        g.drawText (kNames[i], r.withTrimmedLeft (46.0f), juce::Justification::centredLeft);

        // badges: updates waiting, checks that need attention
        int count = 0;
        juce::Colour bc = theme::accent;
        if (i == plugins)
            count = state.updatesAvailable();
        else if (i == workstation)
        {
            count = state.countChecks (Check::Status::problem) + state.countChecks (Check::Status::warning);
            bc = state.countChecks (Check::Status::problem) > 0 ? theme::problem : theme::warn;
        }
        if (count > 0)
        {
            auto b = juce::Rectangle<float> (22.0f, 20.0f).withCentre ({ r.getRight() - 22.0f, r.getCentreY() });
            g.setColour (bc);
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (juce::Colour (0xff121418));
            g.setFont (ui::font (12.0f, 2));
            g.drawText (juce::String (count), b, juce::Justification::centred);
        }
    }

    // activity
    if (state.busy())
    {
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawFittedText (state.activity, 50, getHeight() - 110, getWidth() - 60, 34, juce::Justification::centredLeft, 2);
    }
    if (state.getPaths().isSandbox)
        theme::chip (g, { 16.0f, (float) getHeight() - 76.0f, theme::chipWidth ("Sandbox"), 22.0f }, "Sandbox", theme::warn);
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < numSections; ++i)
        if (itemBounds (i).contains (e.getPosition()))
            h = i;
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void Sidebar::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < numSections; ++i)
        if (itemBounds (i).contains (e.getPosition()) && onSelect)
            onSelect (i);
}

//== MainComponent ===============================================================================================
MainComponent::MainComponent (AppState& s, AudioEngine& audio) : state (s), sidebar (s)
{
    pages[overview] = std::make_unique<OverviewPage> (state);
    pages[plugins] = std::make_unique<PluginsPage> (state);
    pages[profile] = std::make_unique<ProfilePage> (state, audio);
    pages[chains] = std::make_unique<ChainsPage> (state);
    pages[workstation] = std::make_unique<WorkstationPage> (state);
    for (auto& p : pages)
    {
        p->navigate = [this] (int sec) { show (sec); };
        addChildComponent (*p);
    }
    sidebar.onSelect = [this] (int sec) { show (sec); };
    addAndMakeVisible (sidebar);
    state.addChangeListener (this);
    show (overview);
    setSize (1320, 860);
}

MainComponent::~MainComponent()
{
    state.removeChangeListener (this);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::chrome);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    sidebar.setBounds (r.removeFromLeft (248));
    for (auto& p : pages)
        p->setBounds (r);
}

void MainComponent::show (int s)
{
    section = juce::jlimit (0, numSections - 1, s);
    for (int i = 0; i < numSections; ++i)
        pages[(size_t) i]->setVisible (i == section);
    pages[(size_t) section]->refresh();
    sidebar.setSelected (section);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    pages[(size_t) section]->refresh();
    sidebar.repaint();
}
} // namespace dojo::gui
