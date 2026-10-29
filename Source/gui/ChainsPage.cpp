#include "Pages.h"

namespace dojo::gui
{
//== TemplateCard ================================================================================================
TemplateCard::TemplateCard (AppState& s, const ChainTemplate& t) : state (s), tmpl (t)
{
    variant (add, "primary");
    add.onClick = [this] { state.installChain (tmpl); };
    addAndMakeVisible (add);
    variant (open, "ghost", theme::ok);
    open.getProperties().set ("icon", "play");
    open.setButtonText ("Open in REAPER");
    open.onClick = [this]
    {
        state.openInReaper (state.getPaths().reaperChains().getChildFile (juce::File::createLegalFileName (tmpl.name) + ".RfxChain"), tmpl.name);
    };
    addChildComponent (open);
    addAndMakeVisible (diagram);
    update();
}

void TemplateCard::update()
{
    diagram.setBlocks (ChainDiagram::fromTemplate (tmpl, [this] (const juce::String& key)
    {
        auto* p = state.product (key);
        return p != nullptr && p->installed.present;
    }));
    const bool busy = state.chainBusy.count (tmpl.id) > 0 && state.chainBusy.at (tmpl.id);
    const bool in = state.chainInstalled (tmpl);
    add.setButtonText (busy ? "Building..." : (in ? "Update in REAPER" : "Add to REAPER"));
    variant (add, in ? "ghost" : "primary");
    add.getProperties().set ("icon", in ? "refresh" : "download");
    add.setEnabled (! busy);
    open.setVisible (in && ! busy);
    repaint();
}

void TemplateCard::resized()
{
    auto r = getLocalBounds().reduced (20, 18);
    r.removeFromTop (58);
    auto bottom = r.removeFromBottom (36);
    add.setBounds (bottom.removeFromRight (190));
    bottom.removeFromRight (10);
    open.setBounds (bottom.removeFromRight (180));
    r.removeFromBottom (14);
    diagram.setBounds (r);
}

void TemplateCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = tmpl.group == "Master" ? theme::ok : (tmpl.group == "Beats" ? juce::Colour (0xffd9b44a) : theme::accent);
    theme::card (g, r, accent);
    g.setColour (theme::text);
    g.setFont (ui::font (18.0f, 2));
    g.drawText (tmpl.name, (int) r.getX() + 20, (int) r.getY() + 16, (int) r.getWidth() - 200, 24, juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (theme::small());
    g.drawFittedText (tmpl.description, juce::Rectangle<float> (r.getX() + 20.0f, r.getY() + 42.0f, r.getWidth() - 40.0f, 18.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.85f);
    if (state.chainInstalled (tmpl))
        theme::chip (g, { r.getRight() - 20.0f - theme::chipWidth ("In REAPER"), r.getY() + 18.0f, theme::chipWidth ("In REAPER"), 22.0f }, "In REAPER", theme::ok);
    const auto it = state.chainStatus.find (tmpl.id);
    if (it != state.chainStatus.end() && it->second.isNotEmpty())
    {
        g.setColour (it->second.contains ("saved") ? theme::ok : theme::warn);
        g.setFont (theme::small());
        g.drawFittedText (it->second, juce::Rectangle<float> (r.getX() + 20.0f, r.getBottom() - 52.0f, r.getWidth() - 430.0f, 36.0f).toNearestInt(),
                          juce::Justification::centredLeft, 2, 0.85f);
    }
}

//== ChainsPage ==================================================================================================
ChainsPage::ChainsPage (AppState& s) : state (s)
{
    variant (addAll, "ghost");
    addAll.getProperties().set ("icon", "download");
    addAll.setButtonText ("Add all to REAPER");
    addAll.onClick = [this]
    {
        for (const auto& t : ChainTemplate::all())
            state.installChain (t);
    };
    content().addAndMakeVisible (addAll);
    variant (updateSaved, "ghost");
    updateSaved.getProperties().set ("icon", "refresh");
    updateSaved.setButtonText ("Update saved chains");
    updateSaved.setTooltip ("Brings the chains the Dojo saved up to the installed plugins: template chains are rebuilt from the "
                            "current presets; your Voice Profile chains keep every setting and gain the new features (TRUE TONE, "
                            "PITCH HOLD, VOICE HOLD and ESS HOLD). The old files are kept in the Dojo's chain-backups folder.");
    updateSaved.onClick = [this] { state.updateSavedChains(); };
    content().addAndMakeVisible (updateSaved);
    for (const auto& t : ChainTemplate::all())
        content().addAndMakeVisible (cards.add (new TemplateCard (state, t)));
}

void ChainsPage::refresh()
{
    for (auto* c : cards)
        c->update();
    auto sigs = state.signatures();
    auto key = [] (const std::vector<Signature>& v) { juce::String k; for (auto& s : v) { k << s.name << "|"; for (auto& t : s.targets) k << t.lufs << ","; } return k; };
    if (key (sigs) != key (signatures))
    {
        signatures = std::move (sigs);
        signatureButtons.clear();
        for (const auto& s : signatures)
            for (const auto& t : s.targets)
            {
                auto* b = signatureButtons.add (new juce::TextButton ("Take at " + juce::String (juce::roundToInt (t.lufs)) + " LUFS..."));
                variant (*b, "primary", theme::ok);
                b->getProperties().set ("icon", "play");
                b->setTooltip ("Choose a dry take: it is matched to the level " + s.name + " was tuned on and opens in REAPER with the "
                               "vocal chain on its track and the master chain on the master, landing at " + juce::String (juce::roundToInt (t.lufs)) + " LUFS.");
                b->onClick = [this, s, lufs = t.lufs]
                {
                    chooser = std::make_unique<juce::FileChooser> ("A dry take for " + s.name, juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                                   "*.wav;*.flac;*.mp3;*.ogg");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                          [this, s, lufs] (const juce::FileChooser& fc)
                                          {
                                              if (fc.getResult().existsAsFile())
                                                  state.openSignature (s, fc.getResult(), lufs);
                                          });
                };
                content().addAndMakeVisible (b);
            }
    }
    for (auto* b : signatureButtons)
        b->setEnabled (! state.signatureBusy);
    if (const auto now = state.profileChains(); now != voiceChains)
    {
        voiceChains = now;
        voiceButtons.clear();
        for (const auto& f : voiceChains)
        {
            auto* b = voiceButtons.add (new juce::TextButton ("Open in REAPER"));
            variant (*b, "primary", theme::ok);
            b->getProperties().set ("icon", "play");
            const auto name = f.getFileNameWithoutExtension().upToLastOccurrenceOf (" - Vocal Chain", false, false);
            b->onClick = [this, f, name] { state.openInReaper (f, "Vocal - " + name); };
            content().addAndMakeVisible (b);
        }
    }
    Page::refresh();
}

int ChainsPage::layout (int width)
{
    const int m = kMargin;
    addAll.setBounds (width - m - 200, 24, 200, 34);
    updateSaved.setBounds (addAll.getX() - 12 - 210, 24, 210, 34);
    updateSaved.setEnabled (! state.chainsUpdating);
    int y = kHeader + 6;
    groupTitles.clear();
    // your signature sounds: approved chain pairs, one click from a take to a session
    signatureArea = {};
    if (! signatures.empty())
    {
        groupTitles.push_back ({ "Your signature sound", y });
        y += 34;
        signatureArea = { m, y, width - 2 * m, 4 + 70 * (int) signatures.size() };
        int bi = 0;
        for (size_t i = 0; i < signatures.size(); ++i)
        {
            // the targets left to right in the order the signature lists them
            int x = signatureArea.getRight() - 20 - 190 * (int) signatures[i].targets.size() + 10;
            const int by = signatureArea.getY() + 18 + 70 * (int) i;
            for (size_t k = 0; k < signatures[i].targets.size(); ++k, ++bi, x += 190)
                if (bi < signatureButtons.size())
                    signatureButtons[bi]->setBounds (x, by, 180, 36);
        }
        y = signatureArea.getBottom() + 26;
    }
    // your voice: the chains the Voice Profile saved
    voiceArea = {};
    if (! voiceChains.isEmpty())
    {
        groupTitles.push_back ({ "Your voice", y });
        y += 34;
        voiceArea = { m, y, width - 2 * m, 20 + 58 * voiceChains.size() };
        for (int i = 0; i < voiceButtons.size(); ++i)
            voiceButtons[i]->setBounds (voiceArea.getRight() - 210, voiceArea.getY() + 12 + 58 * i, 190, 36);
        y = voiceArea.getBottom() + 26;
    }
    const int cols = width >= 900 ? 2 : 1, gap = 18, ch = 200;
    const int cw = (width - 2 * m - (cols - 1) * gap) / cols;
    juce::String group;
    int col = 0;
    const auto& all = ChainTemplate::all();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].group != group)
        {
            if (col != 0)
                y += ch + gap;
            col = 0;
            group = all[i].group;
            groupTitles.push_back ({ group, y });
            y += 34;
        }
        cards[(int) i]->setBounds (m + col * (cw + gap), y, cw, ch);
        if (++col == cols)
        {
            col = 0;
            y += ch + gap;
        }
    }
    if (col != 0)
        y += ch + gap;
    return y + 90;
}

void ChainsPage::paintContent (juce::Graphics& g)
{
    paintHeader (g, getWidth(), "Chains", "Ready-made REAPER FX chains from the plugins' own presets. In REAPER: FX browser > FX Chains > Axell Signature.");
    if (! signatureArea.isEmpty())
    {
        theme::card (g, signatureArea.toFloat(), theme::ok);
        for (size_t i = 0; i < signatures.size(); ++i)
        {
            const auto& s = signatures[i];
            const float y = (float) signatureArea.getY() + 14.0f + 70.0f * (float) i;
            const int textW = signatureArea.getWidth() - 40 - 190 * (int) s.targets.size();
            g.setColour (theme::text);
            g.setFont (theme::bodyBold().withHeight (16.0f));
            g.drawText (s.name, signatureArea.getX() + 20, (int) y, textW, 22, juce::Justification::centredLeft);
            g.setColour (theme::dim);
            g.setFont (theme::small());
            g.drawFittedText (s.description.isNotEmpty() ? s.description
                                                         : "Your approved vocal + master chains. A take is matched to the level of \"" + s.referenceTake + "\" first.",
                              signatureArea.getX() + 20, (int) y + 22, textW, 34, juce::Justification::topLeft, 2);
        }
    }
    if (! voiceArea.isEmpty())
    {
        theme::card (g, voiceArea.toFloat(), theme::ok);
        for (int i = 0; i < voiceChains.size(); ++i)
        {
            const float y = (float) voiceArea.getY() + 12.0f + 58.0f * (float) i;
            const auto& f = voiceChains.getReference (i);
            g.setColour (theme::text);
            g.setFont (theme::bodyBold().withHeight (15.0f));
            g.drawText (f.getFileNameWithoutExtension(), voiceArea.getX() + 20, (int) y, voiceArea.getWidth() - 260, 20, juce::Justification::centredLeft);
            g.setColour (theme::dim);
            g.setFont (theme::small());
            g.drawText ("Made by the Voice Profile, " + f.getLastModificationTime().formatted ("%d %b %Y %H:%M")
                            + ". Opens a project with an armed vocal track holding it.",
                        voiceArea.getX() + 20, (int) y + 20, voiceArea.getWidth() - 260, 18, juce::Justification::centredLeft);
        }
    }
    if (state.reaperMessage.isNotEmpty())
    {
        g.setColour (state.reaperMessageOk ? theme::ok : theme::warn);
        g.setFont (theme::small());
        g.drawText (state.reaperMessage, kMargin, kHeader - 8, getWidth() - 2 * kMargin, 18, juce::Justification::centredLeft);
    }
    for (const auto& [title, ty] : groupTitles)
    {
        g.setColour (theme::dim);
        g.setFont (theme::label());
        g.drawText (title.toUpperCase(), kMargin, ty + 8, 300, 18, juce::Justification::centredLeft);
    }
    // where they go, and the personal chain
    g.setColour (theme::faint);
    g.setFont (theme::small());
    g.drawFittedText ("Saved in " + state.getPaths().reaperChains().getFullPathName()
                          + ". Your personal chain is made on the Voice Profile page. A chain file you edited yourself is never replaced.",
                      kMargin, content().getHeight() - 70, getWidth() - 2 * kMargin, 40, juce::Justification::centredLeft, 2);
}
} // namespace dojo::gui
