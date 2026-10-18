#pragma once
// Axell Signature Dojo - the visual building blocks the pages share: plots of a voice, a chain diagram, metric tiles,
// a spinner and a scrolling page.
#include "Theme.h"
#include "../model/VoiceAnalysis.h"
#include "../platform/Chains.h"

namespace dojo::gui
{
// a log-frequency plot: one curve, an optional band (lo..hi per point) and labelled markers
class CurvePlot : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Marker { float hz; juce::String text; juce::Colour colour; };
    CurvePlot (float hzLo, float hzHi, float dbLo, float dbHi) : fLo (hzLo), fHi (hzHi), dLo (dbLo), dHi (dbHi) {}
    void setCurve (const voice::Curve& c, juce::Colour col) { curve = c; colour = col; repaint(); }
    void setBand (std::vector<float> lo, std::vector<float> hi, juce::String bandLabel) { bandLo = std::move (lo); bandHi = std::move (hi); bandText = std::move (bandLabel); repaint(); }
    void setMarkers (std::vector<Marker> m) { markers = std::move (m); repaint(); }
    void setUnit (juce::String u) { unit = std::move (u); }
    void paint (juce::Graphics&) override;

private:
    float x (float hz) const;
    float y (float db) const;
    juce::Rectangle<float> plotArea() const;
    float fLo, fHi, dLo, dHi;
    voice::Curve curve;
    juce::Colour colour = theme::accent;
    std::vector<float> bandLo, bandHi;
    juce::String bandText, unit = "dB";
    std::vector<Marker> markers;
};

// voiced hops per semitone, C2..C6, the 10-90 % range highlighted
class PitchHistogram : public juce::Component
{
public:
    void setProfile (const voice::Profile& p) { hist = p.pitchHist; lo = p.f0Low; hi = p.f0High; med = p.f0Median; repaint(); }
    void paint (juce::Graphics&) override;

private:
    std::vector<float> hist;
    float lo = 0, hi = 0, med = 0;
};

// the take as a level envelope, optionally a playback cursor; clicking seeks
class EnvelopeView : public juce::Component
{
public:
    void setEnvelope (std::vector<float> dbFrames, double frameSeconds) { env = std::move (dbFrames); frameSec = frameSeconds; repaint(); }
    void setCursor (double seconds) { cursor = seconds; repaint(); }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    std::function<void (double)> onSeek;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }

private:
    std::vector<float> env;
    double frameSec = 0.02, cursor = -1.0;
    juce::Colour colour = theme::teal;
};

// plugins in signal order: coloured blocks with the program each starts from
class ChainDiagram : public juce::Component
{
public:
    struct Block { juce::String name, detail; juce::Colour colour; bool missing = false; };
    void setBlocks (std::vector<Block> b) { blocks = std::move (b); repaint(); }
    static std::vector<Block> fromTemplate (const ChainTemplate&, const std::function<bool (const juce::String&)>& installed);
    void paint (juce::Graphics&) override;

private:
    std::vector<Block> blocks;
};

// label / big value / note, with a status dot
void metricTile (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, const juce::String& value,
                 const juce::String& note, juce::Colour status);

// an animated ring while something runs
class Spinner : public juce::Component, private juce::Timer
{
public:
    Spinner() { setInterceptsMouseClicks (false, false); }
    void setActive (bool a);
    void setProgress (float p) { progress = p; repaint(); }   // < 0: indeterminate
    void setColour (juce::Colour c) { colour = c; }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { phase += 0.18f; repaint(); }
    float phase = 0.0f, progress = -1.0f;
    juce::Colour colour = theme::accent;
};

// a page whose content scrolls vertically: the page lays out and paints its content (cards, text) through
// layout() and paintContent(); child components go into content()
class ScrollPage : public juce::Component
{
public:
    ScrollPage();
    void resized() override;
    juce::Component& content() { return holder; }
    virtual int layout (int width) = 0;            // returns the content height
    virtual void paintContent (juce::Graphics&) {}
    void relayout();
    // the standard page header: a title, a line under it, and room on the right for actions
    static void paintHeader (juce::Graphics&, int width, const juce::String& title, const juce::String& subtitle);
    static constexpr int kHeader = 96, kMargin = 32;

private:
    struct Holder : juce::Component
    {
        explicit Holder (ScrollPage& p) : page (p) {}
        void paint (juce::Graphics& g) override { page.paintContent (g); }
        ScrollPage& page;
    };
    juce::Viewport viewport;
    Holder holder { *this };
};
} // namespace dojo::gui
