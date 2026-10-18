#include "Theme.h"

namespace dojo::gui
{
namespace theme
{
juce::Colour statusColour (Check::Status s)
{
    switch (s)
    {
        case Check::Status::ok:      return ok;
        case Check::Status::info:    return info;
        case Check::Status::warning: return warn;
        case Check::Status::problem: return problem;
    }
    return dim;
}

juce::String statusName (Check::Status s)
{
    switch (s)
    {
        case Check::Status::ok:      return "Ready";
        case Check::Status::info:    return "Note";
        case Check::Status::warning: return "Attention";
        case Check::Status::problem: return "Problem";
    }
    return {};
}

void card (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accentColour, bool hover)
{
    const float rad = 12.0f;
    // soft shadow
    g.setColour (juce::Colours::black.withAlpha (hover ? 0.35f : 0.22f));
    g.fillRoundedRectangle (r.translated (0.0f, hover ? 4.0f : 2.0f).expanded (hover ? 1.5f : 0.5f), rad + 1.0f);
    g.setGradientFill (juce::ColourGradient (hover ? surfaceHi : surface, r.getX(), r.getY(),
                                             (hover ? surfaceHi : surface).darker (0.18f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    if (! accentColour.isTransparent())
    {
        // an accent wash along the top edge
        g.setGradientFill (juce::ColourGradient (accentColour.withAlpha (hover ? 0.16f : 0.10f), r.getX(), r.getY(),
                                                 accentColour.withAlpha (0.0f), r.getX(), r.getY() + 70.0f, false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (accentColour.withAlpha (0.9f));
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (r, rad);
        g.reduceClipRegion (clip);
        g.fillRect (r.withHeight (2.0f));
        g.restoreState();
    }
    g.setColour (hover ? line.brighter (0.25f) : line);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

float chipWidth (const juce::String& t)
{
    return juce::GlyphArrangement::getStringWidth (label(), t.toUpperCase()) + 22.0f;
}

void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c)
{
    g.setColour (c.withAlpha (0.14f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    const float d = 5.0f;
    g.setColour (c);
    g.fillEllipse (r.getX() + 8.0f, r.getCentreY() - d * 0.5f, d, d);
    g.setFont (label());
    g.setColour (c.brighter (0.35f));
    g.drawText (t.toUpperCase(), r.withTrimmedLeft (16.0f).withTrimmedRight (6.0f), juce::Justification::centred);
}

void ring (juce::Graphics& g, juce::Point<float> c, float rad, float th, float v, juce::Colour col, float phase)
{
    const juce::PathStrokeType stroke (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path bg;
    bg.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    g.setColour (line);
    g.strokePath (bg, stroke);
    juce::Path fg;
    if (v < 0.0f)
        fg.addCentredArc (c.x, c.y, rad, rad, 0.0f, phase, phase + 1.6f, true);
    else if (v > 0.0f)
        fg.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * juce::jlimit (0.0f, 1.0f, v), true);
    g.setColour (col);
    g.strokePath (fg, stroke);
}

void icon (juce::Graphics& g, const juce::String& n, juce::Rectangle<float> r, juce::Colour c, float strokeW)
{
    juce::Path p;   // drawn in a 24 x 24 box
    bool fill = false;
    if (n == "home")
    {
        p.startNewSubPath (3, 11); p.lineTo (12, 3.5f); p.lineTo (21, 11);
        p.startNewSubPath (5.5f, 9.5f); p.lineTo (5.5f, 20); p.lineTo (18.5f, 20); p.lineTo (18.5f, 9.5f);
        p.startNewSubPath (10, 20); p.lineTo (10, 14); p.lineTo (14, 14); p.lineTo (14, 20);
    }
    else if (n == "plugins")
    {
        // a knob with its pointer and an arc
        p.addCentredArc (12, 12, 8.5f, 8.5f, 0.0f, -2.4f, 2.4f, true);
        p.addEllipse (7.5f, 7.5f, 9, 9);
        p.startNewSubPath (12, 12); p.lineTo (15, 9);
    }
    else if (n == "voice")
    {
        p.addRoundedRectangle (9, 3, 6, 11, 3);
        p.startNewSubPath (5.5f, 11); p.cubicTo (5.5f, 19, 18.5f, 19, 18.5f, 11);
        p.startNewSubPath (12, 17.5f); p.lineTo (12, 21);
        p.startNewSubPath (8.5f, 21); p.lineTo (15.5f, 21);
    }
    else if (n == "chains")
    {
        p.addRoundedRectangle (2.5f, 9, 5, 6, 1.5f);
        p.addRoundedRectangle (9.5f, 9, 5, 6, 1.5f);
        p.addRoundedRectangle (16.5f, 9, 5, 6, 1.5f);
        p.startNewSubPath (7.5f, 12); p.lineTo (9.5f, 12);
        p.startNewSubPath (14.5f, 12); p.lineTo (16.5f, 12);
    }
    else if (n == "workstation")
    {
        p.startNewSubPath (12, 2.5f); p.lineTo (20, 5.5f); p.lineTo (20, 11);
        p.cubicTo (20, 16.5f, 16.5f, 19.5f, 12, 21.5f); p.cubicTo (7.5f, 19.5f, 4, 16.5f, 4, 11);
        p.lineTo (4, 5.5f); p.closeSubPath();
        p.startNewSubPath (8.5f, 12); p.lineTo (11, 14.5f); p.lineTo (15.5f, 9.5f);
    }
    else if (n == "refresh")
    {
        p.addCentredArc (12, 12, 7.5f, 7.5f, 0.0f, 0.5f, 5.6f, true);
        p.startNewSubPath (19.5f, 4.5f); p.lineTo (19.6f, 9.4f); p.lineTo (14.8f, 9.0f);
    }
    else if (n == "folder")
    {
        p.startNewSubPath (3, 7); p.lineTo (3, 19); p.lineTo (21, 19); p.lineTo (21, 8.5f); p.lineTo (11.5f, 8.5f);
        p.lineTo (9.5f, 6); p.lineTo (3, 6); p.closeSubPath();
    }
    else if (n == "check")
    {
        p.startNewSubPath (5, 12.5f); p.lineTo (10, 17.5f); p.lineTo (19.5f, 7);
    }
    else if (n == "warning")
    {
        p.startNewSubPath (12, 3.5f); p.lineTo (21.5f, 20); p.lineTo (2.5f, 20); p.closeSubPath();
        p.startNewSubPath (12, 9.5f); p.lineTo (12, 14);
        p.addEllipse (11.2f, 16.3f, 1.6f, 1.6f);
    }
    else if (n == "problem")
    {
        p.addEllipse (3, 3, 18, 18);
        p.startNewSubPath (8.5f, 8.5f); p.lineTo (15.5f, 15.5f);
        p.startNewSubPath (15.5f, 8.5f); p.lineTo (8.5f, 15.5f);
    }
    else if (n == "info")
    {
        p.addEllipse (3, 3, 18, 18);
        p.startNewSubPath (12, 11); p.lineTo (12, 16.5f);
        p.addEllipse (11.2f, 7.2f, 1.6f, 1.6f);
    }
    else if (n == "play")
    {
        p.startNewSubPath (8, 5); p.lineTo (19, 12); p.lineTo (8, 19); p.closeSubPath();
        fill = true;
    }
    else if (n == "stop")
    {
        p.addRoundedRectangle (6.5f, 6.5f, 11, 11, 2);
        fill = true;
    }
    else if (n == "record")
    {
        p.addEllipse (5.5f, 5.5f, 13, 13);
        fill = true;
    }
    else if (n == "download")
    {
        p.startNewSubPath (12, 3.5f); p.lineTo (12, 15);
        p.startNewSubPath (7, 10.5f); p.lineTo (12, 15.5f); p.lineTo (17, 10.5f);
        p.startNewSubPath (4, 16); p.lineTo (4, 20); p.lineTo (20, 20); p.lineTo (20, 16);
    }
    else if (n == "sparkle")
    {
        p.startNewSubPath (12, 2.5f); p.quadraticTo (13, 11, 21.5f, 12); p.quadraticTo (13, 13, 12, 21.5f);
        p.quadraticTo (11, 13, 2.5f, 12); p.quadraticTo (11, 11, 12, 2.5f); p.closeSubPath();
        fill = true;
    }
    else if (n == "arrow")
    {
        p.startNewSubPath (4, 12); p.lineTo (19.5f, 12);
        p.startNewSubPath (14, 6.5f); p.lineTo (19.5f, 12); p.lineTo (14, 17.5f);
    }
    const float s = juce::jmin (r.getWidth(), r.getHeight()) / 24.0f;
    const auto t = juce::AffineTransform::scale (s).translated (r.getCentreX() - 12.0f * s, r.getCentreY() - 12.0f * s);
    g.setColour (c);
    if (fill)
        g.fillPath (p, t);
    else
        g.strokePath (p, juce::PathStrokeType (strokeW * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
}

// one layout for measuring and drawing: lines wrapped at the width, 1.25 line spacing
static juce::GlyphArrangement layoutText (const juce::String& t, float width, juce::Font f, int maxLines)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, t, 0.0f, 0.0f, width, f.getHeight() * 1.25f * (float) maxLines, juce::Justification::topLeft, maxLines, 1.0f);
    return ga;
}

float paragraphHeight (const juce::String& t, float width, juce::Font f, int maxLines)
{
    if (t.isEmpty())
        return 0.0f;
    return layoutText (t, width, f, maxLines).getBoundingBox (0, -1, true).getBottom();
}

float paragraph (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f, juce::Colour c, int maxLines)
{
    if (t.isEmpty())
        return 0.0f;
    auto ga = layoutText (t, r.getWidth(), f, maxLines);
    const float h = ga.getBoundingBox (0, -1, true).getBottom();
    g.setColour (c);
    ga.draw (g, juce::AffineTransform::translation (r.getX(), r.getY()));
    return h;
}
} // namespace theme

//==============================================================================
DojoLookAndFeel::DojoLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::chrome);
    setColour (juce::ScrollBar::thumbColourId, theme::line.brighter (0.3f));
    setColour (juce::ProgressBar::foregroundColourId, theme::accent);
    setColour (juce::ProgressBar::backgroundColourId, theme::line);
    setColour (juce::TextEditor::backgroundColourId, ui::col::well);
    setColour (juce::ComboBox::backgroundColourId, ui::col::well);
}

void variant (juce::Button& b, const char* name, juce::Colour accentColour)
{
    b.getProperties().set ("variant", name);
    b.setColour (juce::TextButton::buttonOnColourId, accentColour);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DojoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down)
{
    const auto v = b.getProperties()["variant"].toString();
    if (v.isEmpty())
    {
        ui::LookAndFeel::drawButtonBackground (g, b, c, over, down);
        return;
    }
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto a = b.findColour (juce::TextButton::buttonOnColourId);
    const float rad = juce::jmin (8.0f, r.getHeight() * 0.5f);
    const float alpha = b.isEnabled() ? 1.0f : 0.38f;
    if (v == "primary" || v == "danger")
    {
        const auto base = (v == "danger" ? theme::problem : a).withMultipliedAlpha (alpha);
        g.setGradientFill (juce::ColourGradient (base.brighter (over ? 0.2f : 0.08f), r.getX(), r.getY(),
                                                 base.darker (down ? 0.25f : 0.1f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        if (over && b.isEnabled())
        {
            g.setColour (base.withAlpha (0.25f));
            g.drawRoundedRectangle (r.expanded (1.5f), rad + 1.5f, 2.0f);
        }
    }
    else if (v == "ghost")
    {
        g.setColour ((over ? theme::surfaceHi : theme::surface).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, rad);
        g.setColour ((over ? a : theme::line.brighter (0.3f)).withMultipliedAlpha (alpha * (down ? 0.7f : 1.0f)));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }
    else if (over && b.isEnabled())   // quiet
    {
        g.setColour (theme::surfaceHi);
        g.fillRoundedRectangle (r, rad);
    }
}

juce::Font DojoLookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    if (b.getProperties()["variant"].toString().isEmpty())
        return ui::LookAndFeel::getTextButtonFont (b, h);
    return ui::font (juce::jmin (14.0f, (float) h * 0.42f), 1);
}

void DojoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto v = b.getProperties()["variant"].toString();
    if (v.isEmpty())
    {
        ui::LookAndFeel::drawButtonText (g, b, over, down);
        return;
    }
    const float alpha = b.isEnabled() ? 1.0f : 0.45f;
    auto col = (v == "primary" || v == "danger") ? juce::Colour (0xff121418) : (v == "quiet" && over ? b.findColour (juce::TextButton::buttonOnColourId) : theme::text);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto r = b.getLocalBounds().reduced (10, 0);
    const auto ic = b.getProperties()["icon"].toString();
    if (ic.isNotEmpty())
    {
        const float s = juce::jmin (18.0f, (float) b.getHeight() * 0.5f);
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), b.getButtonText());
        const float total = s + (b.getButtonText().isNotEmpty() ? 7.0f + tw : 0.0f);
        const float x0 = (float) r.getCentreX() - total * 0.5f;
        theme::icon (g, ic, { x0, (float) r.getCentreY() - s * 0.5f, s, s }, col.withMultipliedAlpha (alpha), 1.8f);
        r = r.withLeft ((int) (x0 + s + 7.0f)).withWidth ((int) tw + 2);
        g.setColour (col.withMultipliedAlpha (alpha));
        g.drawText (b.getButtonText(), r, juce::Justification::centredLeft);
        return;
    }
    g.setColour (col.withMultipliedAlpha (alpha));
    g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 1, 0.9f);
}

void DojoLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                                     int thumbSize, bool over, bool down)
{
    auto t = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    t = t.reduced (vertical ? 2 : 0, vertical ? 0 : 2);
    g.setColour (theme::line.brighter (down ? 0.6f : (over ? 0.45f : 0.25f)));
    g.fillRoundedRectangle (t.toFloat(), (float) juce::jmin (t.getWidth(), t.getHeight()) * 0.5f);
}

void DojoLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int w, int h, double progress, const juce::String& text)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (theme::line);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    const auto c = bar.findColour (juce::ProgressBar::foregroundColourId);
    if (progress >= 0.0 && progress <= 1.0)
    {
        g.setGradientFill (juce::ColourGradient (c.brighter (0.15f), 0.0f, 0.0f, c, (float) w, 0.0f, false));
        g.fillRoundedRectangle (r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * (float) progress)), r.getHeight() * 0.5f);
    }
    else
    {
        // indeterminate: a moving segment
        const float t = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1100.0, 1.0);
        const float seg = r.getWidth() * 0.3f;
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> ((r.getWidth() + seg) * t - seg, 0.0f, seg, r.getHeight())
                                    .getIntersection (r), r.getHeight() * 0.5f);
    }
    if (text.isNotEmpty())
    {
        g.setColour (theme::text);
        g.setFont (theme::small());
        g.drawText (text, r, juce::Justification::centred);
    }
}

void DojoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff1c1f25));
    g.setColour (theme::line);
    g.drawRect (0, 0, w, h);
}
} // namespace dojo::gui
