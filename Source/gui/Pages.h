#pragma once
// Axell Signature Dojo - the five sections.
#include "MainComponent.h"

namespace dojo::gui
{
//== Overview ====================================================================================================
class OverviewPage : public Page
{
public:
    explicit OverviewPage (AppState&);
    int layout (int width) override;
    void paintContent (juce::Graphics&) override;
    void refresh() override;

private:
    struct Step { juce::String text; juce::Colour colour; int section; };
    std::vector<Step> nextSteps() const;
    AppState& state;
    juce::TextButton toPlugins, toWorkstation, toProfile;
    juce::OwnedArray<juce::TextButton> stepButtons;
    juce::Rectangle<int> cards[3], strip, stepsArea;
    std::vector<juce::Rectangle<int>> tiles;
    std::map<juce::String, juce::Image> thumbs;
};

//== Plugins =====================================================================================================
class ProductCard : public juce::Component
{
public:
    ProductCard (AppState&, const juce::String& key);
    void paint (juce::Graphics&) override;
    void resized() override;
    void update();
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    void showVersions();
    AppState& state;
    juce::String key;
    juce::Image thumb;
    juce::TextButton main, versions;
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    bool hover = false;
};

class PluginsPage : public Page
{
public:
    explicit PluginsPage (AppState&);
    int layout (int width) override;
    void paintContent (juce::Graphics&) override;
    void refresh() override;

private:
    AppState& state;
    juce::TextButton fromGitHub { "GitHub" }, fromFolder { "Folder..." }, check, updateAll;
    juce::OwnedArray<ProductCard> cards;
    std::unique_ptr<juce::FileChooser> chooser;
};

//== Voice Profile ===============================================================================================
class ProfilePage : public Page, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    ProfilePage (AppState&, AudioEngine&);
    ~ProfilePage() override;
    int layout (int width) override;
    void paintContent (juce::Graphics&) override;
    void refresh() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void chooseFile();
    void toggleRecord();
    void openAudioSettings();
    void loadPlayback();
    void save();
    AppState& state;
    AudioEngine& audio;
    juce::TextButton record, chooseTake, audioSettings, playStop, abOriginal, abChain, tune, saveButton;
    juce::TextEditor profileName;
    EnvelopeView envelope;
    CurvePlot spectrum { 60.0f, 16000.0f, -60.0f, 0.0f }, lowMids { 150.0f, 1500.0f, -15.0f, 15.0f };
    PitchHistogram pitch;
    ChainDiagram chain;
    Spinner spinner;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File recordingFile;
    std::shared_ptr<ProfileOutcome> shown;
    juce::Rectangle<int> takeCard, metricsArea, plotsCard, notesCard, chainCard, saveCard;
    std::vector<juce::Rectangle<int>> metricCells;
    int whyTop = 0;
};

//== Chains ======================================================================================================
class TemplateCard : public juce::Component
{
public:
    TemplateCard (AppState&, const ChainTemplate&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void update();

private:
    AppState& state;
    const ChainTemplate& tmpl;
    ChainDiagram diagram;
    juce::TextButton add;
};

class ChainsPage : public Page
{
public:
    explicit ChainsPage (AppState&);
    int layout (int width) override;
    void paintContent (juce::Graphics&) override;
    void refresh() override;

private:
    AppState& state;
    juce::TextButton addAll;
    juce::OwnedArray<TemplateCard> cards;
    std::vector<std::pair<juce::String, int>> groupTitles;   // title, y
};

//== Workstation =================================================================================================
class CheckRow : public juce::Component
{
public:
    CheckRow (AppState&, Check);
    void paint (juce::Graphics&) override;
    void resized() override;
    int heightFor (int width);

private:
    AppState& state;
    Check check;
    juce::OwnedArray<juce::TextButton> fixes;
};

class WorkstationPage : public Page
{
public:
    explicit WorkstationPage (AppState&);
    int layout (int width) override;
    void paintContent (juce::Graphics&) override;
    void refresh() override;

private:
    AppState& state;
    juce::TextButton rerun;
    juce::OwnedArray<CheckRow> rows;
    std::vector<std::pair<juce::String, int>> groupTitles;
    int shownGeneration = -1;
    Spinner spinner;
};
} // namespace dojo::gui
