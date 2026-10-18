#pragma once
// Axell Signature Dojo - the window: a sidebar with the sections, the current page, and the state they share.
#include "AppState.h"
#include "AudioEngine.h"
#include "Theme.h"
#include "Widgets.h"

namespace dojo::gui
{
class Page : public ScrollPage
{
public:
    virtual void refresh() { relayout(); }
    std::function<void (int)> navigate;   // to another section
};

enum Section { overview, plugins, profile, chains, studio, workstation, numSections };

class Sidebar : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Sidebar (AppState&);
    std::function<void (int)> onSelect;
    void setSelected (int s) { selected = s; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> itemBounds (int i) const { return { 14, 128 + i * 48, getWidth() - 28, 42 }; }
    AppState& state;
    int selected = 0, hover = -1;
    juce::Image logo;
    Spinner spinner;
    juce::TextButton scaleButton;
};

class MainComponent : public juce::Component, private juce::ChangeListener
{
public:
    MainComponent (AppState&, AudioEngine&);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void show (int section);
    int current() const { return section; }
    Page* page (int s) { return pages[(size_t) s].get(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    AppState& state;
    Sidebar sidebar;
    std::array<std::unique_ptr<Page>, numSections> pages;
    int section = overview;
    juce::TooltipWindow tooltips { this, 500 };
};
} // namespace dojo::gui
