// Axell Signature Dojo - headless screenshots of every page (docs/screenshots), from a sandbox: the installed
// plugins are read (and a Voice Profile is tuned through them), nothing is written outside the sandbox.
//   DojoSnapshot <output folder> [take.wav] [--scale 2]
#include "gui/MainComponent.h"
#include "gui/Pages.h"

using namespace dojo;
using namespace dojo::gui;

static void waitIdle (AppState& state, int maxMs)
{
    const auto t0 = juce::Time::getMillisecondCounter();
    do
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    while ((state.busy() || state.profileBusy) && juce::Time::getMillisecondCounter() - t0 < (juce::uint32) maxMs);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc < 2)
    {
        std::fprintf (stderr, "DojoSnapshot <output folder> [take.wav] [--scale 2]\n");
        return 1;
    }
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.createDirectory();
    juce::File take, reference, master;
    float scale = 2.0f;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--scale" && i + 1 < argc)
            scale = (float) juce::String (argv[++i]).getDoubleValue();
        else if (a == "--reference" && i + 1 < argc)
            reference = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else if (a == "--master" && i + 1 < argc)
            master = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else
            take = juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]);
    }

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dojo-snapshot-" + juce::String::toHexString (juce::Random().nextInt64()));
    const auto real = Paths::fromEnvironment();
    auto paths = Paths::sandbox (root);
    paths.vst3 = real.vst3;   // read only
    paths.reaperConfig().createDirectory();
    real.reaperConfig().getChildFile ("reaper-vstplugins64.ini").copyFileTo (paths.reaperConfig().getChildFile ("reaper-vstplugins64.ini"));
    real.reaperConfig().getChildFile ("reaper.ini").copyFileTo (paths.reaperConfig().getChildFile ("reaper.ini"));

    DojoLookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    {
        AppState state (paths);
        AudioEngine audio;
        MainComponent main (state, audio);
        main.setSize (1320, 860);
        state.runChecks();
        state.checkReleases();
        waitIdle (state, 60000);
        // two chains in "REAPER" (the sandbox), so the page shows both states
        state.installChain (ChainTemplate::all()[0]);
        state.installChain (ChainTemplate::all()[7]);
        waitIdle (state, 60000);
        if (take.existsAsFile())
        {
            state.reference = reference;
            state.analyse (take, true);
            waitIdle (state, 300000);
            // a backup (so the Tools page has something to say) and a batch list
            state.batchChain = paths.reaperChains().getChildFile ("Rap Lead.RfxChain");
            state.batchFiles.add (take);
        }
        if (master.existsAsFile())
        {
            state.previewMaster (master);
            waitIdle (state, 120000);
        }
        auto shot = [&] (int section, const juce::String& name, int height)
        {
            main.show (section);
            main.setSize (1320, height);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            auto img = main.createComponentSnapshot (main.getLocalBounds(), true, scale);
            juce::PNGImageFormat png;
            const auto f = out.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
            std::printf ("%s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        };
        shot (overview, "overview", 900);
        shot (plugins, "plugins", 1180);
        shot (profile, "voice-profile", take.existsAsFile() ? 2050 : 860);
        shot (chains, "chains", 1500);
        shot (studio, "tools", 1250);
        shot (workstation, "workstation", 1400);
        main.show (overview);
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    root.deleteRecursively();
    return 0;
}
