// Axell Signature Dojo - the application.
//   Axell Signature Dojo            the app
//   Axell Signature Dojo --check    the workstation report on the terminal (exit code 1 if a problem was found)
//   Axell Signature Dojo --profile <take> [--reference <vocal>] [--name <name>] [--reduction <%>] [--gate-range <dB>]
//                                   [--save [--open]]
//                                   a Voice Profile on the terminal: analysis, tuning through the installed plugins,
//                                   and with --save the presets and REAPER chain (never replacing the user's own)
//   Axell Signature Dojo --render <chain.RfxChain> <out folder> <take>...
//                                   Batch Render on the terminal: every take through the chain, 24-bit WAV, never replacing
//   AXELL_DOJO_SANDBOX=<folder>     every folder the Dojo touches lives under <folder> (testing, demos)
#include "gui/MainComponent.h"
#include "platform/ProfileEngine.h"
#include "platform/Signature.h"
#include "platform/Tools.h"
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
        if (commandLine.contains ("--signature"))
        {
            setApplicationReturnValue (runSignature (getCommandLineParameterArray()));
            quit();
            return;
        }
        if (commandLine.contains ("--update-chains"))
        {
            setApplicationReturnValue (runUpdateChains (getCommandLineParameterArray()));
            quit();
            return;
        }
        if (commandLine.contains ("--render"))
        {
            setApplicationReturnValue (runRender (getCommandLineParameterArray()));
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
        juce::String reduction, gateRange;
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
            else if (args[i] == "--reduction" && i + 1 < args.size())
                reduction = args[++i];
            else if (args[i] == "--gate-range" && i + 1 < args.size())
                gateRange = args[++i];
            else if (args[i] == "--reference" && i + 1 < args.size())
                reference = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
        }
        if (! take.existsAsFile())
        {
            std::printf ("No such take: %s\n", take.getFullPathName().toRawUTF8());
            return 2;
        }
        ProfileEngine eng (Paths::fromEnvironment());
        eng.adjustPlan = [reduction, gateRange] (voice::Plan& plan)
        {
            auto set = [&plan] (const char* plugin, const char* param, const juce::String& text, const char* why)
            {
                for (auto& pp : plan.chain)
                    if (pp.plugin == plugin)
                        for (auto& st : pp.settings)
                            if (st.param == param)
                            {
                                st.text = text.toStdString();
                                pp.why.push_back (why);
                            }
            };
            if (reduction.isNotEmpty())
                set ("dereverb", "Reduction", juce::String (reduction.getIntValue()) + " %", "Reduction set on the terminal (--reduction).");
            if (gateRange.isNotEmpty())
                set ("gate", "Range", juce::String (gateRange.getFloatValue(), 1) + " dB", "Range set on the terminal (--gate-range).");
        };
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
    static int runRender (const juce::StringArray& args)
    {
        const int at = args.indexOf ("--render");
        if (at < 0 || args.size() < at + 4)
        {
            std::printf ("usage: --render <chain.RfxChain> <out folder> <take>...\n");
            return 2;
        }
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        juce::Array<juce::File> takes;
        for (int i = at + 3; i < args.size(); ++i)
            takes.add (cwd.getChildFile (args[i]));
        const auto res = dojo::tools::batchRender (Paths::fromEnvironment(), cwd.getChildFile (args[at + 1]), takes, cwd.getChildFile (args[at + 2]), {}, nullptr);
        int failed = 0;
        for (const auto& r : res)
        {
            failed += r.ok ? 0 : 1;
            if (r.ok)
                std::printf ("%s -> %s: %.2f -> %.2f LUFS, %.1f dBTP\n", r.source.getFileName().toRawUTF8(), r.output.getFullPathName().toRawUTF8(),
                             r.inLufs, r.outLufs, r.outTruePeakDbtp);
            else
                std::printf ("%s: %s\n", r.source.getFileName().toRawUTF8(), r.message.toRawUTF8());
        }
        return failed > 0 ? 1 : 0;
    }

    // --signature [<name> --take <file> --target <LUFS> [--open]]: a take through a signature sound (gain-matched, master
    // at the target) as a REAPER session; without a take it lists the signatures
    static int runSignature (const juce::StringArray& args)
    {
        const auto paths = Paths::fromEnvironment();
        const auto sigs = Signature::all (paths);
        juce::String name, takeArg;
        float target = -14.0f;
        bool open = false;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--signature" && i + 1 < args.size() && ! args[i + 1].startsWith ("--"))
                name = args[++i];
            else if (args[i] == "--take" && i + 1 < args.size())
                takeArg = args[++i];
            else if (args[i] == "--target" && i + 1 < args.size())
                target = args[++i].getFloatValue();
            else if (args[i] == "--open")
                open = true;
        }
        if (name.isEmpty() || takeArg.isEmpty())
        {
            for (const auto& s : sigs)
            {
                juce::String t;
                for (const auto& x : s.targets)
                    t << juce::String (juce::roundToInt (x.lufs)) << " ";
                std::printf ("%-20s reference %.1f LUFS (%s), targets %s LUFS\n", s.name.toRawUTF8(), s.referenceLufs, s.referenceTake.toRawUTF8(), t.trim().toRawUTF8());
            }
            if (sigs.empty())
                std::printf ("No signature sounds in %s\n", Signature::folder (paths).getFullPathName().toRawUTF8());
            std::printf ("usage: --signature <name> --take <file> --target <LUFS> [--open]\n");
            return name.isEmpty() ? 0 : 2;
        }
        for (const auto& s : sigs)
            if (s.name == name)
            {
                const auto take = juce::File::getCurrentWorkingDirectory().getChildFile (takeArg);
                const auto r = makeSignatureSession (paths, s, take, target, open);
                std::printf ("%s\n", r.message.toRawUTF8());
                return r.ok ? 0 : 1;
            }
        std::printf ("No signature named \"%s\".\n", name.toRawUTF8());
        return 2;
    }

    // --update-chains [chain.RfxChain ...]: every chain the Dojo wrote (or the named ones) brought up to the
    // installed plugins; the old files are kept in the Dojo's chain-backups folder
    static int runUpdateChains (const juce::StringArray& args)
    {
        const auto paths = Paths::fromEnvironment();
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        juce::Array<juce::File> files;
        for (int i = args.indexOf ("--update-chains") + 1; i < args.size(); ++i)
            files.add (args[i].startsWithChar ('/') ? juce::File (args[i]) : paths.reaperChains().getChildFile (args[i]).existsAsFile()
                                                                                 ? paths.reaperChains().getChildFile (args[i])
                                                                                 : cwd.getChildFile (args[i]));
        int failed = 0;
        for (const auto& u : dojo::tools::updateSavedChains (paths, files))
        {
            failed += u.ok ? 0 : 1;
            std::printf ("%-48s %s\n", u.file.getFileName().toRawUTF8(), u.message.toRawUTF8());
            for (const auto& c : u.changes)
                std::printf ("%-48s   %s\n", "", c.toRawUTF8());
        }
        return failed > 0 ? 1 : 0;
    }

    juce::uint32 quitRequested = 0;
};
} // namespace dojo::gui

START_JUCE_APPLICATION (dojo::gui::DojoApplication)
