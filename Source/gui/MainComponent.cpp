#include "MainComponent.h"

#include <axell/gui/BrandTitle.h>
#include "Pages.h"
#include "DojoAssets.h"

namespace dojo::gui
{
namespace
{
const char* const kNames[] = { "Overview", "Plugins", "Voice Profile", "Chains", "Tools", "Workstation" };
const char* const kIcons[] = { "home", "plugins", "voice", "chains", "tools", "workstation" };
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
    // Axell's Dojo: the family emblem, drawn as vectors (the brand's own red for the wave and the dot)
    axell::brand::drawEmblem (g, juce::Rectangle<float> (16.0f, 24.0f, 56.0f, 56.0f), axell::brand::kSun);
    // the brand's title: "Axell's" in gold over "Dojo" brushed, with the Dojo's seal (道, "the way")
    {
        auto small = axell::brand::brushPath (juce::String (juce::CharPointer_UTF8 ("Axell\xe2\x80\x99s")), 9.0f, 0.06f);
        small.applyTransform (juce::AffineTransform::translation (80.0f, 42.0f));
        g.setGradientFill (juce::ColourGradient (axell::brand::kGoldLight, 0.0f, 33.0f, axell::brand::kGoldDeep, 0.0f, 44.0f, false));
        axell::brand::fillInk (g, small, 0.3f);
        auto big = axell::brand::brushPath ("Dojo", 19.0f, -0.01f);
        const float wN = big.getBounds().getRight();
        big.applyTransform (juce::AffineTransform::translation (79.0f, 70.0f));
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 4, { 0, 1 }).drawForPath (g, big);
        g.setGradientFill (juce::ColourGradient (axell::brand::kIvory, 0.0f, 50.0f, axell::brand::kIvoryShade, 0.0f, 76.0f, false));
        axell::brand::fillInk (g, big, 0.4f);
        axell::brand::drawSeal (g, { 79.0f + wN + 9.0f, 50.0f, 22.0f, 22.0f }, juce::String (juce::CharPointer_UTF8 ("\xe9\x81\x93")));
    }

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
    pages[studio] = std::make_unique<ToolsPage> (state, audio);
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
