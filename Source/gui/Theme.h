#pragma once
// Axell Signature Dojo - the look: the Axell plugins' palette and type (axell-core), extended for an application:
// app chrome, cards, chips, vector icons, progress rings and button variants.
#include "../platform/SystemCheck.h"

#include <axell/gui/LookAndFeel.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dojo::gui
{
namespace theme
{
const juce::Colour chrome   (0xff0e1013);   // window background behind the cards
const juce::Colour sidebar  (0xff13161a);
const juce::Colour surface  (0xff1a1d23);   // cards
const juce::Colour surfaceHi (0xff21252c);
const juce::Colour line     (0xff2a2e36);
const juce::Colour text     = ui::col::text;
const juce::Colour dim      = ui::col::dim;
const juce::Colour faint    = ui::col::faint;
const juce::Colour accent   = ui::col::accent;
const juce::Colour teal     = ui::col::teal;
const juce::Colour ok       = ui::col::ok;
const juce::Colour warn     = ui::col::warn;
const juce::Colour problem  = ui::col::clip;
const juce::Colour info     (0xff5aa6e8);

juce::Colour statusColour (Check::Status);
juce::String statusName (Check::Status);

inline juce::Font title()   { return ui::font (26.0f, 2); }
inline juce::Font heading() { return ui::font (17.0f, 1); }
inline juce::Font body()    { return ui::font (14.0f, 0); }
inline juce::Font bodyBold(){ return ui::font (14.0f, 1); }
inline juce::Font small()   { return ui::font (12.5f, 0); }
inline juce::Font label()   { return ui::caps (11.0f, 1); }
inline juce::Font metric()  { return ui::font (24.0f, 2); }

// a card: soft gradient surface, hairline border, optional accent line along the top and a hover lift
void card (juce::Graphics&, juce::Rectangle<float>, juce::Colour accent = {}, bool hover = false);
// a status / tag pill with a tinted fill
void chip (juce::Graphics&, juce::Rectangle<float>, const juce::String&, juce::Colour);
float chipWidth (const juce::String&);
// 0..1 ring (value < 0: an indeterminate spinner at `phase`)
void ring (juce::Graphics&, juce::Point<float> centre, float radius, float thickness, float value, juce::Colour, float phase = 0.0f);
// a vector icon in a square: home, plugins, voice, chains, workstation, refresh, folder, check, warning, problem,
// info, play, stop, record, download, sparkle, arrow
void icon (juce::Graphics&, const juce::String& name, juce::Rectangle<float>, juce::Colour, float stroke = 1.6f);
// text that wraps inside `r`; returns the height it used
float paragraph (juce::Graphics&, const juce::String&, juce::Rectangle<float> r, juce::Font, juce::Colour, int maxLines = 20);
float paragraphHeight (const juce::String&, float width, juce::Font, int maxLines = 20);
} // namespace theme

// the app's look and feel: the Axell look plus button variants set through the button's "variant" property:
// "primary" (accent fill), "ghost" (outline), "quiet" (text only), "danger"
class DojoLookAndFeel : public ui::LookAndFeel
{
public:
    DojoLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                        int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 10; }
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
};

// helpers to style buttons
void variant (juce::Button&, const char* name, juce::Colour accent = theme::accent);
} // namespace dojo::gui
