#pragma once
// Axell Signature Dojo - Voice Profile: analyse a take, plan the vocal chain, tune it by rendering the take through
// the installed plugins, then (when asked) write the plugins' presets and a REAPER chain (docs/DESIGN.md, D12-D13).
#include "Chains.h"
#include "Paths.h"
#include "../model/ToneMatch.h"
#include "../model/VoiceMapping.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>

namespace dojo
{
struct StageOutcome
{
    const Product* product = nullptr;
    bool available = false;
    juce::String program;
    std::vector<std::pair<juce::String, juce::String>> values;   // what the Dojo set, as the plugin shows it
    juce::StringArray why;
    float inLufs = -70.0f, outLufs = -70.0f;
    juce::String problem;
};

struct ProfileOutcome
{
    bool ok = false;
    juce::String error;
    juce::File take;
    voice::Profile profile;
    voice::Plan plan;
    std::vector<StageOutcome> stages;
    std::vector<BuiltStage> built;            // the tuned plugins (for writing); empty if not tuned
    juce::AudioBuffer<float> processed;       // the take through the tuned chain (for listening / display)
    std::vector<juce::AudioBuffer<float>> stageAudio;   // the take after each stage (takes up to 5 min), for step-through listening
    double sampleRate = 48000.0;
    // Reference Match (D16): the tone difference to a reference vocal before and after the tuned Penta EQ (dB RMS,
    // 120 Hz-10 kHz, shape only), measured on the rendered take
    juce::File reference;
    std::vector<tone::Bell> bells;
    float toneBeforeDb = 0.0f, toneAfterDb = 0.0f;
};

class ProfileEngine
{
public:
    explicit ProfileEngine (Paths p) : paths (std::move (p)) {}

    using Progress = std::function<void (float, const juce::String&)>;
    static bool loadTake (const juce::File&, juce::AudioBuffer<float>& audio, double& rate, juce::String& error,
                          double maxSeconds = 600.0);

    // analysis + plan; `tune` also renders through the installed plugins (seconds to a minute or two)
    // `reference`: an optional vocal whose tone the Penta EQ step moves toward (`amount` 0..1)
    ProfileOutcome run (const juce::File& take, bool tune, const Progress&, const std::atomic<bool>* cancel = nullptr,
                        const juce::File& reference = {}, float amount = 1.0f) const;

    // optional: changes the plan before tuning (the terminal's --reduction / --gate-range, for presets made across
    // several sessions, where one file's measurements do not stand for all of them)
    std::function<void (voice::Plan&)> adjustPlan;

    struct WriteOptions
    {
        juce::String name = "My Voice";
        bool presets = true, chain = true, profile = true;
    };
    // writes into the plugins' preset folders, REAPER's FX chain folder and the Dojo's profile folder; returns a line
    // per file (written or refused, and why)
    juce::StringArray write (ProfileOutcome&, const WriteOptions&) const;
    // what `write` would create, without writing
    juce::StringArray preview (const ProfileOutcome&, const WriteOptions&) const;

    // measurement helpers (also used by the tests)
    static float lufs (const juce::AudioBuffer<float>&, double rate);
    static std::vector<float> envelopeDb (const juce::AudioBuffer<float>&, double rate, double ms = 5.0);

private:
    Paths paths;
};
} // namespace dojo
