#include "Widgets.h"

namespace dojo::gui
{
//== CurvePlot ===================================================================================================
juce::Rectangle<float> CurvePlot::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedBottom (18.0f).withTrimmedTop (6.0f).withTrimmedRight (8.0f);
}
float CurvePlot::x (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (juce::jlimit (fLo, fHi, hz) / fLo) / std::log (fHi / fLo);
}
float CurvePlot::y (float db) const
{
    const auto a = plotArea();
    return a.getBottom() - a.getHeight() * (juce::jlimit (dLo, dHi, db) - dLo) / (dHi - dLo);
}

void CurvePlot::paint (juce::Graphics& g)
{
    const auto a = plotArea();
    g.setColour (ui::col::well);
    g.fillRoundedRectangle (a.expanded (2.0f), 6.0f);
    // grid: decades and the dB scale
    g.setFont (theme::small().withHeight (10.5f));
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        if (f >= fLo && f <= fHi)
        {
            g.setColour (theme::line.withAlpha (0.8f));
            g.drawVerticalLine ((int) x (f), a.getY(), a.getBottom());
            g.setColour (theme::faint);
            g.drawText (f >= 1000.f ? juce::String (juce::roundToInt (f / 1000.f)) + "k" : juce::String (juce::roundToInt (f)), (int) x (f) - 16, (int) a.getBottom() + 2, 32, 14,
                        juce::Justification::centred);
        }
    const float step = (dHi - dLo) > 30.0f ? 10.0f : ((dHi - dLo) > 12.0f ? 5.0f : 2.0f);
    for (float d = std::ceil (dLo / step) * step; d <= dHi; d += step)
    {
        g.setColour (std::abs (d) < 0.01f ? theme::line.brighter (0.4f) : theme::line.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y (d), a.getX(), a.getRight());
        g.setColour (theme::faint);
        g.drawText ((d > 0 ? "+" : "") + juce::String (juce::roundToInt (d)), 0, (int) y (d) - 7, 30, 14, juce::Justification::centredRight);
    }
    // band
    if (! bandLo.empty() && bandLo.size() == curve.hz.size() && bandHi.size() == curve.hz.size())
    {
        juce::Path band;
        for (size_t i = 0; i < curve.hz.size(); ++i)
            i == 0 ? band.startNewSubPath (x (curve.hz[i]), y (bandHi[i])) : band.lineTo (x (curve.hz[i]), y (bandHi[i]));
        for (size_t i = curve.hz.size(); i-- > 0;)
            band.lineTo (x (curve.hz[i]), y (bandLo[i]));
        band.closeSubPath();
        g.setColour (theme::info.withAlpha (0.13f));
        g.fillPath (band);
        g.setColour (theme::info.withAlpha (0.35f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
        if (bandText.isNotEmpty())
        {
            g.setColour (theme::info.brighter (0.2f));
            g.setFont (theme::small().withHeight (11.0f));
            g.drawText (bandText, a.reduced (8.0f, 4.0f), juce::Justification::topRight);
        }
    }
    // curve with a soft fill under it
    if (curve.hz.size() > 1)
    {
        juce::Path p, fill;
        bool started = false;
        for (size_t i = 0; i < curve.hz.size(); ++i)
        {
            if (! std::isfinite (curve.db[i]))
                continue;
            const float px = x (curve.hz[i]), py = y (curve.db[i]);
            if (! started)
            {
                p.startNewSubPath (px, py);
                fill.startNewSubPath (px, a.getBottom());
                fill.lineTo (px, py);
                started = true;
            }
            else
            {
                p.lineTo (px, py);
                fill.lineTo (px, py);
            }
        }
        if (started)
        {
            fill.lineTo (fill.getCurrentPosition().x, a.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, a.getY(), colour.withAlpha (0.0f), 0.0f, a.getBottom(), false));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    for (const auto& m : markers)
    {
        if (m.hz <= 0.0f)
            continue;
        const float mx = x (m.hz);
        g.setColour (m.colour.withAlpha (0.8f));
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ mx, a.getY(), mx, a.getBottom() }, dash, 2, 1.2f);
        g.setFont (theme::small().withHeight (11.0f).boldened());
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), m.text) + 10.0f;
        auto tag = juce::Rectangle<float> (tw, 17.0f).withCentre ({ juce::jlimit (a.getX() + tw * 0.5f, a.getRight() - tw * 0.5f, mx), a.getY() + 12.0f });
        g.setColour (m.colour.withAlpha (0.9f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (juce::Colour (0xff121418));
        g.drawText (m.text, tag, juce::Justification::centred);
    }
}

//== PitchHistogram ==============================================================================================
void PitchHistogram::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat().withTrimmedBottom (18.0f);
    g.setColour (ui::col::well);
    g.fillRoundedRectangle (a.expanded (2.0f), 6.0f);
    if (hist.empty())
        return;
    float top = 1.0f;
    for (float v : hist)
        top = std::max (top, v);
    const int n = (int) hist.size();
    const float w = a.getWidth() / (float) n;
    auto hzOf = [] (int semi) { return 440.0f * std::pow (2.0f, ((float) (semi + 36) - 69.0f) / 12.0f); };
    for (int i = 0; i < n; ++i)
    {
        const float hz = hzOf (i);
        const bool inRange = hz >= lo * 0.97f && hz <= hi * 1.03f;
        const float h = (a.getHeight() - 6.0f) * hist[(size_t) i] / top;
        auto bar = juce::Rectangle<float> (a.getX() + w * (float) i + 1.0f, a.getBottom() - h, w - 2.0f, h);
        g.setColour (inRange ? theme::teal : theme::teal.withAlpha (0.35f));
        g.fillRoundedRectangle (bar, juce::jmin (2.0f, w * 0.3f));
    }
    g.setFont (theme::small().withHeight (10.5f));
    g.setColour (theme::faint);
    static const char* names[] = { "C2", "C3", "C4", "C5", "C6" };
    for (int o = 0; o <= 4; ++o)
    {
        const float px = juce::jlimit (a.getX() + 14.0f, a.getRight() - 14.0f, a.getX() + w * (float) (o * 12) + w * 0.5f);
        g.drawText (names[o], (int) px - 14, (int) a.getBottom() + 2, 28, 14, juce::Justification::centred);
    }
    if (med > 0.0f)
    {
        const float semi = 12.0f * std::log2 (med / 440.0f) + 69.0f - 36.0f;
        const float px = a.getX() + w * semi + w * 0.5f;
        g.setColour (theme::text.withAlpha (0.8f));
        const float dash[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ px, a.getY(), px, a.getBottom() }, dash, 2, 1.0f);
    }
}

//== EnvelopeView ================================================================================================
void EnvelopeView::paint (juce::Graphics& g)
{
    const auto a = getLocalBounds().toFloat();
    g.setColour (ui::col::well);
    g.fillRoundedRectangle (a, 6.0f);
    if (env.empty())
        return;
    const float mid = a.getCentreY();
    const int w = (int) a.getWidth();
    g.setColour (colour.withAlpha (0.85f));
    for (int px = 0; px < w; ++px)
    {
        const size_t i0 = (size_t) px * env.size() / (size_t) w, i1 = std::max (i0 + 1, (size_t) (px + 1) * env.size() / (size_t) w);
        float m = -120.0f;
        for (size_t i = i0; i < std::min (i1, env.size()); ++i)
            m = std::max (m, env[i]);
        // map -60..0 dBFS to 0..1
        const float v = juce::jlimit (0.0f, 1.0f, (m + 60.0f) / 60.0f);
        const float h = v * (a.getHeight() * 0.5f - 3.0f);
        g.fillRect (a.getX() + (float) px, mid - h, 1.0f, juce::jmax (1.0f, 2.0f * h));
    }
    if (cursor >= 0.0)
    {
        const double total = (double) env.size() * frameSec;
        const float cx = a.getX() + a.getWidth() * (float) (cursor / juce::jmax (0.001, total));
        g.setColour (theme::text);
        g.fillRect (cx - 1.0f, a.getY(), 2.0f, a.getHeight());
    }
}

void EnvelopeView::mouseDown (const juce::MouseEvent& e)
{
    if (onSeek && ! env.empty())
        onSeek (juce::jlimit (0.0, 1.0, (double) e.x / (double) getWidth()) * (double) env.size() * frameSec);
}

//== ChainDiagram ================================================================================================
std::vector<ChainDiagram::Block> ChainDiagram::fromTemplate (const ChainTemplate& t, const std::function<bool (const juce::String&)>& installed)
{
    std::vector<Block> out;
    for (const auto& s : t.stages)
        if (auto* p = Catalog::find (s.product))
            out.push_back ({ p->name.fromFirstOccurrenceOf ("Axell ", false, false), s.program, p->accent, installed && ! installed (s.product) });
    return out;
}

void ChainDiagram::paint (juce::Graphics& g)
{
    if (blocks.empty())
        return;
    const auto a = getLocalBounds().toFloat();
    const float gap = 18.0f;
    const float bw = (a.getWidth() - gap * (float) (blocks.size() - 1)) / (float) blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        auto r = juce::Rectangle<float> (a.getX() + (bw + gap) * (float) i, a.getY(), bw, a.getHeight());
        g.setColour (b.colour.withAlpha (b.missing ? 0.06f : 0.13f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (b.colour.withAlpha (b.missing ? 0.3f : 0.75f));
        if (b.missing)
        {
            juce::Path p;
            p.addRoundedRectangle (r.reduced (0.5f), 8.0f);
            juce::Path d;
            const float dash[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
            g.fillPath (d);
        }
        else
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 8.0f), 1.5f);
        auto t = r.reduced (10.0f, 6.0f).withTrimmedLeft (2.0f);
        g.setColour (b.missing ? theme::faint : theme::text);
        g.setFont (theme::bodyBold().withHeight (juce::jmin (13.5f, t.getHeight() * 0.42f)));
        // "Duo Compressor" becomes "Duo" when the block is narrow, rather than being cut off
        const auto name = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), b.name) <= t.getWidth() * 1.05f
                              ? b.name : b.name.upToFirstOccurrenceOf (" ", false, false);
        g.drawFittedText (name, t.removeFromTop (t.getHeight() * 0.48f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.8f);
        g.setColour (b.missing ? theme::faint : theme::dim);
        g.setFont (theme::small().withHeight (juce::jmin (11.5f, t.getHeight() * 0.5f)));
        g.drawFittedText (b.missing ? juce::String ("not installed") : b.detail, t.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
        if (i + 1 < blocks.size())
        {
            const float ax = r.getRight() + 3.0f, ay = r.getCentreY();
            juce::Path arrow;
            arrow.startNewSubPath (ax, ay);
            arrow.lineTo (ax + gap - 6.0f, ay);
            arrow.startNewSubPath (ax + gap - 10.0f, ay - 4.0f);
            arrow.lineTo (ax + gap - 6.0f, ay);
            arrow.lineTo (ax + gap - 10.0f, ay + 4.0f);
            g.setColour (theme::faint);
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

//== metric tile =================================================================================================
void metricTile (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& lbl, const juce::String& value,
                 const juce::String& note, juce::Colour status)
{
    g.setColour (ui::col::well.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
    auto t = r.reduced (14.0f, 11.0f);
    g.setFont (theme::label());
    g.setColour (theme::dim);
    g.drawText (lbl.toUpperCase(), t.removeFromTop (14.0f), juce::Justification::centredLeft);
    if (! status.isTransparent())
    {
        g.setColour (status);
        g.fillEllipse (r.getRight() - 18.0f, r.getY() + 14.0f, 7.0f, 7.0f);
    }
    t.removeFromTop (4.0f);
    g.setFont (theme::metric().withHeight (juce::jmin (24.0f, t.getHeight() * 0.55f)));
    g.setColour (theme::text);
    g.drawFittedText (value, t.removeFromTop (juce::jmin (30.0f, t.getHeight() * 0.6f)).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    g.setFont (theme::small());
    g.setColour (theme::dim);
    g.drawFittedText (note, t.toNearestInt(), juce::Justification::topLeft, 2, 0.85f);
}

//== Spinner =====================================================================================================
void Spinner::setActive (bool a)
{
    if (a && ! isTimerRunning())
        startTimerHz (40);
    else if (! a)
        stopTimer();
    setVisible (a);
}

void Spinner::paint (juce::Graphics& g)
{
    const auto a = getLocalBounds().toFloat().reduced (2.0f);
    const float r = juce::jmin (a.getWidth(), a.getHeight()) * 0.5f;
    theme::ring (g, a.getCentre(), r - 2.0f, juce::jmax (2.0f, r * 0.18f), progress, colour, phase);
}

//== ScrollPage ==================================================================================================
ScrollPage::ScrollPage()
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible (viewport);
}

void ScrollPage::resized()
{
    viewport.setBounds (getLocalBounds());
    relayout();
}

void ScrollPage::relayout()
{
    const int w = juce::jmax (100, getWidth() - viewport.getScrollBarThickness());
    const int h = layout (w);
    holder.setSize (w, juce::jmax (h, getHeight()));
    holder.repaint();
}

void ScrollPage::paintHeader (juce::Graphics& g, int width, const juce::String& title, const juce::String& subtitle)
{
    g.setColour (theme::text);
    g.setFont (theme::title());
    g.drawText (title, kMargin, 26, width - 2 * kMargin, 34, juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (theme::body());
    g.drawFittedText (subtitle, kMargin, 60, width - 2 * kMargin, 22, juce::Justification::centredLeft, 1, 0.92f);
}
} // namespace dojo::gui
