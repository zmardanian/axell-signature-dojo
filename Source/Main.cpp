// Axell Signature Dojo - the application.
//   Axell Signature Dojo            the app
//   Axell Signature Dojo --check    the workstation report on the terminal (exit code 1 if a problem was found)
//   Axell Signature Dojo --profile <take> [--reference <vocal>] [--name <name>] [--save [--open]]
//                                   a Voice Profile on the terminal: analysis, tuning through the installed plugins,
//                                   and with --save the presets and REAPER chain (never replacing the user's own)
//   AXELL_DOJO_SANDBOX=<folder>     every folder the Dojo touches lives under <folder> (testing, demos)
#include "gui/MainComponent.h"
#include "platform/ProfileEngine.h"
#include "gui/Theme.h"

#include <cstdio>

namespace dojo::gui
{
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (AppState& state, AudioEngine& audio)
        : DocumentWindow ("Axell Signature Dojo", theme::chrome, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (state, audio), true);
        setResizable (true, false);
        setResizeLimits (1080, 700, 6000, 4000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class DojoApplication : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Axell Signature Dojo"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--check"))
        {
            setApplicationReturnValue (printChecks());
            quit();
            return;
        }
        if (commandLine.contains ("--profile"))
        {
            setApplicationReturnValue (runProfile (getCommandLineParameterArray()));
            quit();
            return;
        }
        if (commandLine.contains ("--version"))
        {
            std::printf ("Axell Signature Dojo %s\n", JUCE_APPLICATION_VERSION_STRING);
            quit();
            return;
        }
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        state = std::make_unique<AppState> (Paths::fromEnvironment());
        juce::Desktop::getInstance().setGlobalScaleFactor (state->uiScale());
        audio = std::make_unique<AudioEngine>();
        window = std::make_unique<MainWindow> (*state, *audio);
        state->runChecks();
    }

    void shutdown() override
    {
        window.reset();
        audio.reset();
        state.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    // a task that is running (a download, a render) is cancelled and allowed to finish cleanly first: the window
    // stays responsive meanwhile, and nothing is left half-written
    void systemRequestedQuit() override
    {
        if (state != nullptr && state->busy())
        {
            quitRequested = juce::Time::getMillisecondCounter();
            state->cancelAll();
            startTimer (100);
            return;
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
            window->toFront (true);
    }

private:
    void timerCallback() override
    {
        if (state == nullptr || ! state->busy() || juce::Time::getMillisecondCounter() - quitRequested > 20000)
        {
            stopTimer();
            quit();
        }
    }

    static int printChecks()
    {
        int problems = 0;
        for (const auto& c : SystemCheck (Paths::fromEnvironment()).runAll())
        {
            std::printf ("[%-9s] %-24s %s\n", theme::statusName (c.status).toRawUTF8(), c.title.toRawUTF8(), c.detail.toRawUTF8());
            if (c.manual.isNotEmpty())
                std::printf ("%38s%s\n", "", c.manual.toRawUTF8());
            problems += c.status == Check::Status::problem ? 1 : 0;
        }
        return problems > 0 ? 1 : 0;
    }

    static int runProfile (const juce::StringArray& args)
    {
        juce::File take;
        juce::String name = "My Voice";
        bool save = false, open = false;
        juce::File reference;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--profile" && i + 1 < args.size())
                take = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
            else if (args[i] == "--name" && i + 1 < args.size())
                name = args[++i];
            else if (args[i] == "--save")
                save = true;
            else if (args[i] == "--open")
                open = true;
            else if (args[i] == "--reference" && i + 1 < args.size())
                reference = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
        }
        if (! take.existsAsFile())
        {
            std::printf ("No such take: %s\n", take.getFullPathName().toRawUTF8());
            return 2;
        }
        ProfileEngine eng (Paths::fromEnvironment());
        auto o = eng.run (take, true, [] (float f, const juce::String& t) { std::printf ("\r%3d %%  %-60s", juce::roundToInt (f * 100.0f), t.toRawUTF8()); std::fflush (stdout); }, nullptr, reference);
        std::printf ("\n");
        if (! o.ok)
        {
            std::printf ("%s\n", o.error.toRawUTF8());
            return 1;
        }
        const auto& p = o.profile;
        std::printf ("%s: %.1f s (%.1f s of voice)\n", take.getFileName().toRawUTF8(), p.seconds, p.activeSeconds);
        std::printf ("  loudness %.1f LUFS, peak %.1f dBFS, background %.0f dB under the voice\n", p.integratedLufs, p.peakDbfs, p.snrDb);
        std::printf ("  pitch %.0f Hz (%.0f-%.0f), esses sh %.1f kHz / s %.1f kHz, room %.2f s%s\n", p.f0Median, p.f0Low, p.f0High,
                     p.shHz / 1000.0, p.sHz / 1000.0, p.rt60, p.processedTake ? " (effects printed in: room not measurable)" : "");
        for (auto& n : o.plan.notes)
            std::printf ("  note: %s\n", juce::String (n).toRawUTF8());
        if (reference.existsAsFile())
            std::printf ("  reference %s: tone difference %.1f -> %.1f dB RMS (%d bells)\n", reference.getFileName().toRawUTF8(), o.toneBeforeDb, o.toneAfterDb, (int) o.bells.size());
        for (const auto& s : o.stages)
        {
            if (s.product == nullptr)
                continue;
            juce::String v;
            for (auto& [k, t] : s.values)
                v << k << " " << t << ", ";
            std::printf ("  %-22s %s %s\n", s.product->name.toRawUTF8(), v.dropLastCharacters (2).toRawUTF8(), s.problem.toRawUTF8());
        }
        if (save)
        {
            const auto paths = Paths::fromEnvironment();
            for (auto& line : eng.write (o, { name, true, true, true }))
                std::printf ("  %s\n", line.toRawUTF8());
            if (open)
                std::printf ("  %s\n", ChainWriter (paths).openInReaper (paths.reaperChains().getChildFile (juce::File::createLegalFileName (name + " - Vocal Chain") + ".RfxChain"),
                                                                           "Vocal - " + name).message.toRawUTF8());
        }
        else
            for (auto& line : eng.preview (o, { name, true, true, true }))
                std::printf ("  would write %s\n", line.toRawUTF8());
        return 0;
    }

    DojoLookAndFeel lookAndFeel;
    std::unique_ptr<AppState> state;
    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<MainWindow> window;
    juce::uint32 quitRequested = 0;
};
} // namespace dojo::gui

START_JUCE_APPLICATION (dojo::gui::DojoApplication)
