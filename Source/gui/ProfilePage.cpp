#include "Pages.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace dojo::gui
{
namespace
{
juce::String hz (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
} // namespace

ProfilePage::ProfilePage (AppState& s, AudioEngine& a) : state (s), audio (a)
{
    variant (record, "primary", theme::teal);
    record.getProperties().set ("icon", "record");
    record.setButtonText ("Record");
    record.setTooltip ("Record 30-60 seconds of yourself rapping or talking, the way you record a real take.");
    record.onClick = [this] { toggleRecord(); };
    variant (chooseTake, "ghost");
    chooseTake.getProperties().set ("icon", "folder");
    chooseTake.setButtonText ("Use a recording");
    chooseTake.setTooltip ("A raw take of your voice: WAV, AIFF, FLAC, Ogg or MP3. You can also drop a file here.");
    chooseTake.onClick = [this] { chooseFile(); };
    variant (audioSettings, "quiet");
    audioSettings.setButtonText ("Audio settings");
    audioSettings.onClick = [this] { openAudioSettings(); };
    variant (playStop, "ghost");
    playStop.getProperties().set ("icon", "play");
    playStop.onClick = [this]
    {
        juce::String err;
        if (audio.isPlaying())
            audio.stop();
        else if (audio.open (err))
            audio.play();
        refresh();
    };
    for (auto* b : { &abOriginal, &abChain })
    {
        variant (*b, "ghost", theme::teal);
        b->setClickingTogglesState (false);
    }
    abOriginal.setButtonText ("Original");
    abChain.setButtonText ("Through your chain");
    abOriginal.onClick = [this] { audio.setProcessed (false); refresh(); };
    abChain.onClick = [this] { audio.setProcessed (true); refresh(); };
    variant (tune, "primary", theme::accent);
    tune.getProperties().set ("icon", "sparkle");
    tune.setButtonText ("Tune with my plugins");
    tune.setTooltip ("Renders your take through the installed plugins and sets compression and levels by measurement (30-90 s).");
    tune.onClick = [this] { if (state.take.existsAsFile()) state.analyse (state.take, true); };
    variant (saveButton, "primary", theme::ok);
    saveButton.getProperties().set ("icon", "download");
    saveButton.setButtonText ("Save presets and REAPER chain");
    saveButton.onClick = [this] { save(); };
    profileName.setText ("My Voice", false);
    profileName.setFont (theme::body());
    profileName.setIndents (10, 8);
    profileName.setInputRestrictions (40);

    envelope.onSeek = [this] (double t) { audio.seek (t); };
    spectrum.setUnit ("dB");
    for (auto* c : std::initializer_list<juce::Component*> { &record, &chooseTake, &audioSettings, &playStop, &abOriginal, &abChain, &tune,
                                                              &saveButton, &profileName, &envelope, &spectrum, &lowMids, &pitch, &chain })
        content().addAndMakeVisible (c);
    content().addChildComponent (spinner);
    spinner.setColour (theme::teal);
    startTimerHz (30);
}

ProfilePage::~ProfilePage()
{
    audio.stop();
    audio.stopRecording();
}

bool ProfilePage::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

void ProfilePage::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        state.analyse (juce::File (files[0]), false);
}

void ProfilePage::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("A recording of your voice", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
            state.analyse (fc.getResult(), false);
    });
}

void ProfilePage::toggleRecord()
{
    if (audio.isRecording())
    {
        audio.stopRecording();
        state.analyse (recordingFile, false);
        refresh();
        return;
    }
    juce::String err;
    audio.stop();
    if (! audio.open (err))
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("No audio input").withMessage (err).withButton ("OK"), nullptr);
        return;
    }
    recordingFile = state.getPaths().takes().getChildFile ("Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
    if (! audio.startRecording (recordingFile, err))
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Cannot record").withMessage (err).withButton ("OK"), nullptr);
    refresh();
}

void ProfilePage::openAudioSettings()
{
    juce::String err;
    audio.open (err);
    auto* sel = new juce::AudioDeviceSelectorComponent (audio.devices(), 1, 2, 2, 2, false, false, true, false);
    sel->setSize (520, 380);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = theme::surface;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void ProfilePage::loadPlayback()
{
    juce::AudioBuffer<float> original;
    double rate = 48000.0;
    juce::String err;
    if (shown == nullptr || ! ProfileEngine::loadTake (shown->take, original, rate, err))
        return;
    audio.stop();
    audio.setPlayback (original, shown->processed, rate);
    audio.setProcessed (shown->processed.getNumSamples() > 0);
}

void ProfilePage::save()
{
    ProfileEngine::WriteOptions o;
    o.name = profileName.getText().trim().isEmpty() ? juce::String ("My Voice") : profileName.getText().trim();
    const auto files = ProfileEngine (state.getPaths()).preview (*state.profile, o);
    auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Save \"" + o.name + "\"?")
                    .withMessage ("The Dojo writes:\n\n" + files.joinIntoString ("\n")
                                  + "\n\nA preset or chain of yours with the same name is never replaced.")
                    .withButton ("Save").withButton ("Cancel");
    juce::AlertWindow::showAsync (opts, [this, o] (int r) { if (r == 1) state.writeProfile (o); });
}

void ProfilePage::timerCallback()
{
    if (audio.isRecording() || audio.isPlaying())
    {
        envelope.setCursor (audio.isPlaying() ? audio.position() : -1.0);
        content().repaint (takeCard);
    }
    else if (envelope.isShowing())
        envelope.setCursor (-1.0);
}

void ProfilePage::refresh()
{
    if (state.profile != shown)
    {
        shown = state.profile;
        if (shown != nullptr)
        {
            const auto& p = shown->profile;
            envelope.setEnvelope (p.envelope, 0.02);
            spectrum.setCurve (p.ltas, theme::teal);
            std::vector<CurvePlot::Marker> mk;
            if (p.shHz > 0) mk.push_back ({ p.shHz, "sh", theme::warn });
            if (p.sHz > 0) mk.push_back ({ p.sHz, "s", theme::accent });
            if (p.f0Median > 0) mk.push_back ({ p.f0Median, "voice", theme::teal });
            spectrum.setMarkers (mk);
            spectrum.setTooltip ("Your voice's long-term spectrum (voiced parts, 1/3 octave). Markers: your voice's pitch and the centres of your esses.");
            std::vector<float> lo, hi;
            for (float sd : p.lowMidSpread)
            {
                lo.push_back (-2.0f * sd);
                hi.push_back (2.0f * sd);
            }
            lowMids.setCurve (p.lowMid, p.lowMidOutside ? theme::warn : theme::accent);
            lowMids.setBand (lo, hi, "27 studio voices");
            lowMids.setTooltip ("Your low-mid balance against 27 dry studio voices (shaded: where 95 % of them fall). Outside the band can be a boxy room "
                                "or simply your voice: the Dojo shows it, it does not cut it.");
            pitch.setProfile (p);
            std::vector<ChainDiagram::Block> blocks;
            for (const auto& st : shown->stages)
            {
                if (st.product == nullptr)
                    continue;
                juce::String detail;
                for (auto& [k, v] : st.values)
                    if (! k.contains ("On") && ! k.contains ("Slope"))
                        detail << (detail.isEmpty() ? "" : ", ") << k << " " << v;
                blocks.push_back ({ st.product->name.fromFirstOccurrenceOf ("Axell ", false, false), detail, st.product->accent,
                                    shown->built.size() > 0 && ! st.available });
            }
            chain.setBlocks (blocks);
            loadPlayback();
        }
    }
    const bool has = shown != nullptr;
    const bool rec = audio.isRecording();
    record.setButtonText (rec ? "Stop" : "Record");
    record.getProperties().set ("icon", rec ? "stop" : "record");
    variant (record, rec ? "danger" : "primary", theme::teal);
    record.setEnabled (! state.profileBusy);
    chooseTake.setEnabled (! state.profileBusy && ! rec);
    for (auto* c : std::initializer_list<juce::Component*> { &envelope, &playStop, &abOriginal, &abChain, &spectrum, &lowMids, &pitch, &chain, &tune })
        c->setVisible (has);
    const bool tuned = has && state.profileTuned && ! shown->built.empty();
    saveButton.setVisible (tuned);
    profileName.setVisible (tuned);
    saveButton.setEnabled (! state.profileBusy);
    tune.setEnabled (! state.profileBusy);
    tune.setButtonText (tuned ? "Tune again" : "Tune with my plugins");
    abChain.setEnabled (tuned);
    playStop.setButtonText (audio.isPlaying() ? "Stop" : "Play");
    playStop.getProperties().set ("icon", audio.isPlaying() ? "stop" : "play");
    variant (abOriginal, audio.processedSelected() && tuned ? "ghost" : "primary", theme::teal);
    variant (abChain, audio.processedSelected() && tuned ? "primary" : "ghost", theme::teal);
    spinner.setActive (state.profileBusy);
    spinner.setProgress (state.profileBusy ? state.profileProgress : -1.0f);
    Page::refresh();
}

int ProfilePage::layout (int width)
{
    const int m = kMargin, gap = 16;
    int y = kHeader + 4;
    const bool has = shown != nullptr;
    takeCard = { m, y, width - 2 * m, has ? 214 : 176 };
    {
        auto r = takeCard.reduced (22, 20);
        auto row = r.removeFromTop (38).withY (takeCard.getY() + 74);
        record.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (10);
        chooseTake.setBounds (row.removeFromLeft (180));
        row.removeFromLeft (10);
        audioSettings.setBounds (row.removeFromLeft (140));
        spinner.setBounds (takeCard.getRight() - 62, takeCard.getY() + 18, 40, 40);
        if (has)
        {
            auto env = juce::Rectangle<int> (takeCard.getX() + 22, takeCard.getY() + 126, takeCard.getWidth() - 44 - 330, 66);
            envelope.setBounds (env);
            playStop.setBounds (env.getRight() + 14, env.getY(), 92, 32);
            abOriginal.setBounds (env.getRight() + 14, env.getY() + 36, 100, 30);
            abChain.setBounds (env.getRight() + 118, env.getY() + 36, 184, 30);
        }
    }
    y = takeCard.getBottom() + 22;
    metricCells.clear();
    if (! has)
        return y + 20;

    const int tw = (width - 2 * m - 2 * gap) / 3;
    for (int i = 0; i < 6; ++i)
        metricCells.push_back ({ m + (i % 3) * (tw + gap), y + (i / 3) * (104 + gap), tw, 104 });
    y += 2 * 104 + gap + 22;

    plotsCard = { m, y, width - 2 * m, 270 };
    {
        auto r = plotsCard.reduced (20, 0).withTrimmedTop (44).withTrimmedBottom (16);
        const int w1 = (int) (r.getWidth() * 0.46f), w2 = (int) (r.getWidth() * 0.27f);
        spectrum.setBounds (r.removeFromLeft (w1));
        r.removeFromLeft (20);
        lowMids.setBounds (r.removeFromLeft (w2));
        r.removeFromLeft (20);
        pitch.setBounds (r);
    }
    y = plotsCard.getBottom() + 22;

    const auto& notes = shown->plan.notes;
    float nh = 0.0f;
    for (auto& n : notes)
        nh += theme::paragraphHeight (n, (float) (width - 2 * m - 70), theme::body(), 4) + 12.0f;
    notesCard = notes.empty() ? juce::Rectangle<int>() : juce::Rectangle<int> (m, y, width - 2 * m, (int) nh + 54);
    if (! notes.empty())
        y = notesCard.getBottom() + 22;

    // the chain: diagram, then why each setting is what it is
    float whyH = 0.0f;
    const float colW = (float) (width - 2 * m - 60) * 0.5f;
    std::vector<float> colH (2, 0.0f);
    int k = 0;
    for (const auto& st : shown->stages)
    {
        float h = 26.0f;
        for (auto& w : st.why)
            h += theme::paragraphHeight ("- " + w, colW, theme::small(), 4) + 4.0f;
        colH[(size_t) (k++ % 2)] += h + 10.0f;
    }
    whyH = juce::jmax (colH[0], colH[1]);
    chainCard = { m, y, width - 2 * m, 70 + 96 + 24 + (int) whyH + 14 };
    chain.setBounds (chainCard.getX() + 22, chainCard.getY() + 62, chainCard.getWidth() - 44, 96);
    whyTop = chain.getBottom() + 22;
    tune.setBounds (chainCard.getRight() - 22 - 220, chainCard.getY() + 16, 220, 36);
    y = chainCard.getBottom() + 22;

    const bool tuned = state.profileTuned && ! shown->built.empty();
    saveCard = tuned ? juce::Rectangle<int> (m, y, width - 2 * m, 120 + 20 * juce::jmax (0, state.writeLog.size())) : juce::Rectangle<int>();
    if (tuned)
    {
        profileName.setBounds (saveCard.getX() + 22, saveCard.getY() + 60, 260, 36);
        saveButton.setBounds (saveCard.getX() + 296, saveCard.getY() + 60, 280, 36);
        y = saveCard.getBottom() + 22;
    }
    return y + 30;
}

void ProfilePage::paintContent (juce::Graphics& g)
{
    paintHeader (g, getWidth(), "Voice Profile", "Your voice, your room and your mic, measured. Then the vocal chain is tuned on your take by the installed plugins.");
    const bool has = shown != nullptr;

    // the take
    {
        const auto r = takeCard.toFloat();
        theme::card (g, r, theme::teal);
        g.setColour (theme::text);
        g.setFont (theme::heading());
        g.drawText (has ? shown->take.getFileName() : juce::String ("Your take"), (int) r.getX() + 22, (int) r.getY() + 18, (int) r.getWidth() - 300, 24,
                    juce::Justification::centredLeft);
        g.setColour (theme::dim);
        g.setFont (theme::small());
        juce::String sub;
        if (audio.isRecording())
            sub = "Recording " + juce::String (audio.recordedSeconds(), 1) + " s. Rap or talk as you do on a real take; 30-60 s is plenty.";
        else if (state.profileBusy)
            sub = state.profileStatus + "...";
        else if (state.profileError.isNotEmpty())
            sub = state.profileError;
        else if (has)
            sub = juce::String (shown->profile.seconds, 1) + " s, " + juce::String (shown->profile.activeSeconds, 1) + " s of voice, "
                  + juce::String (shown->sampleRate / 1000.0, 1) + " kHz" + (state.profileTuned ? ", tuned through your plugins" : "");
        else
            sub = "Record 30-60 seconds the way you record a real take, or use a raw recording (drop a file here). The same mic, room and level you always use.";
        g.setColour (state.profileError.isNotEmpty() && ! state.profileBusy ? theme::problem : theme::dim);
        g.drawFittedText (sub, juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 44.0f, r.getWidth() - 110.0f, 20.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.9f);
        // input meters while the audio device is open
        if (audio.isOpen())
        {
            const float mx = (float) audioSettings.getRight() + 24.0f, mw = juce::jmin (260.0f, r.getRight() - mx - 80.0f);
            for (int c = 0; c < 2; ++c)
            {
                const float lvl = audio.inputLevel (c);
                const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
                const float v = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                auto bar = juce::Rectangle<float> (mx, (float) record.getY() + 8.0f + (float) c * 12.0f, mw, 7.0f);
                g.setColour (ui::col::well);
                g.fillRoundedRectangle (bar, 3.5f);
                g.setColour (db > -1.0f ? theme::problem : (db > -12.0f ? theme::warn : theme::ok));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v), 3.5f);
            }
            g.setColour (theme::faint);
            g.setFont (theme::small().withHeight (11.0f));
            g.drawText (audio.deviceDescription(), (int) mx, record.getBottom() + 2, (int) mw + 60, 16, juce::Justification::centredLeft);
        }
    }
    if (! has)
        return;
    const auto& p = shown->profile;

    // metrics
    {
        const auto loudCol = p.clippedRuns > 0 ? theme::problem : theme::ok;
        metricTile (g, metricCells[0].toFloat(), "Loudness", juce::String (p.integratedLufs, 1) + " LUFS",
                    "Peak " + juce::String (p.peakDbfs, 1) + " dBFS" + (p.clippedRuns > 0 ? ", clips " + juce::String (p.clippedRuns) + "x" : ""), loudCol);
        metricTile (g, metricCells[1].toFloat(), "Background", juce::String (juce::roundToInt (p.snrDb)) + " dB under you",
                    "Noise floor " + juce::String (juce::roundToInt (p.noiseFloorDb)) + " dBFS", p.snrDb >= 35.0f ? theme::ok : (p.snrDb >= 20.0f ? theme::warn : theme::problem));
        metricTile (g, metricCells[2].toFloat(), "Pitch", p.f0Median > 0 ? hz (p.f0Median) : juce::String ("-"),
                    p.f0Median > 0 ? "Range " + juce::String (juce::roundToInt (p.f0Low)) + "-" + juce::String (juce::roundToInt (p.f0High)) + " Hz (10-90 %)" : "Not enough voiced audio", theme::teal);
        metricTile (g, metricCells[3].toFloat(), "Esses", p.sHz > 0 ? hz (p.sHz) : juce::String ("-"),
                    "sh at " + hz (p.shHz) + "; " + juce::String (juce::roundToInt (100.0f * p.sShare)) + " % of their energy in the s", theme::accent);
        metricTile (g, metricCells[4].toFloat(), "Room", p.processedTake ? juce::String ("Not measurable") : juce::String (p.rt60, 2) + " s",
                    p.processedTake ? "Effects are printed into this take" : "Decay (RT60, measured blind from your words)", p.processedTake ? theme::warn : theme::info);
        metricTile (g, metricCells[5].toFloat(), "Low end", juce::String (juce::roundToInt (p.rumbleDb)) + " dB",
                    "Rumble under the voice; pops and bumps reach " + juce::String (juce::roundToInt (p.lfBurstDb)) + " dB", p.rumbleDb > -25.0f ? theme::warn : theme::ok);
    }
    // plots
    {
        const auto r = plotsCard.toFloat();
        theme::card (g, r);
        g.setFont (theme::label());
        g.setColour (theme::dim);
        g.drawText ("YOUR VOICE", spectrum.getX(), plotsCard.getY() + 18, 300, 16, juce::Justification::centredLeft);
        g.drawText ("LOW MIDS VS STUDIO VOICES", lowMids.getX(), plotsCard.getY() + 18, 300, 16, juce::Justification::centredLeft);
        g.drawText ("PITCH", pitch.getX(), plotsCard.getY() + 18, 300, 16, juce::Justification::centredLeft);
    }
    // notes
    if (! notesCard.isEmpty())
    {
        const auto r = notesCard.toFloat();
        theme::card (g, r, theme::warn);
        g.setColour (theme::text);
        g.setFont (theme::heading());
        g.drawText ("About this take", (int) r.getX() + 22, (int) r.getY() + 16, 400, 24, juce::Justification::centredLeft);
        float y = r.getY() + 50.0f;
        for (auto& n : shown->plan.notes)
        {
            theme::icon (g, "info", { r.getX() + 22.0f, y, 18.0f, 18.0f }, theme::warn, 1.8f);
            y += theme::paragraph (g, n, { r.getX() + 50.0f, y, r.getWidth() - 74.0f, 200.0f }, theme::body(), theme::dim, 4) + 12.0f;
        }
    }
    // the chain
    {
        const auto r = chainCard.toFloat();
        theme::card (g, r, theme::accent);
        g.setColour (theme::text);
        g.setFont (theme::heading());
        g.drawText ("Your vocal chain", (int) r.getX() + 22, (int) r.getY() + 18, 400, 24, juce::Justification::centredLeft);
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawText (state.profileTuned ? "Tuned on your take through the installed plugins. Each plugin's output matches its input loudness."
                                       : "Planned from the measurements. Tuning sets compression and levels by rendering your take.",
                    (int) r.getX() + 22, (int) r.getY() + 40, (int) r.getWidth() - 300, 18, juce::Justification::centredLeft);
        const float colW = (r.getWidth() - 60.0f) * 0.5f;
        float colY[2] = { (float) whyTop, (float) whyTop };
        int k = 0;
        for (const auto& st : shown->stages)
        {
            if (st.product == nullptr)
                continue;
            const int c = k++ % 2;
            const float x = r.getX() + 22.0f + (float) c * (colW + 16.0f);
            g.setColour (st.product->accent);
            g.fillEllipse (x, colY[c] + 6.0f, 8.0f, 8.0f);
            g.setColour (theme::text);
            g.setFont (theme::bodyBold());
            g.drawText (st.product->name + (st.problem.isNotEmpty() ? "  -  " + st.problem : juce::String()), (int) x + 16, (int) colY[c], (int) colW - 16, 20,
                        juce::Justification::centredLeft);
            colY[c] += 26.0f;
            for (auto& w : st.why)
                colY[c] += theme::paragraph (g, "- " + w, { x + 16.0f, colY[c], colW - 16.0f, 200.0f }, theme::small(), theme::dim, 4) + 4.0f;
            colY[c] += 10.0f;
        }
    }
    // save
    if (! saveCard.isEmpty())
    {
        const auto r = saveCard.toFloat();
        theme::card (g, r, theme::ok);
        g.setColour (theme::text);
        g.setFont (theme::heading());
        g.drawText ("Keep it", (int) r.getX() + 22, (int) r.getY() + 18, 400, 24, juce::Justification::centredLeft);
        g.setColour (theme::dim);
        g.setFont (theme::small());
        g.drawText ("Presets for Gate, Penta EQ, Duo Compressor and Velvet, and one REAPER chain with all five, under this name.",
                    (int) r.getX() + 22, (int) r.getY() + 40, (int) r.getWidth() - 44, 18, juce::Justification::centredLeft);
        float y = r.getY() + 108.0f;
        for (auto& line : state.writeLog)
        {
            const bool ok = line.contains ("saved");
            theme::icon (g, ok ? "check" : "warning", { r.getX() + 22.0f, y, 16.0f, 16.0f }, ok ? theme::ok : theme::warn, 2.0f);
            g.setColour (theme::dim);
            g.drawText (line, (int) r.getX() + 46, (int) y - 1, (int) r.getWidth() - 70, 18, juce::Justification::centredLeft);
            y += 20.0f;
        }
    }
}
} // namespace dojo::gui
