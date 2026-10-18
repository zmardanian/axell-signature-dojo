#pragma once
// Axell Signature Dojo - hosting an installed Axell plugin as a DAW would (JUCE's VST3 host, not the plugin's own
// classes): set parameters by the names and texts the plugin shows, pick factory programs, render audio offline and
// read the VST3 state a DAW would save. The Voice Profile tunes settings on the user's take through exactly the
// plugin version that is installed, and the presets and REAPER chains it writes are the plugin's own state.
//
// Threading: instances are created, queried for state and destroyed on the message thread (callSync); rendering
// runs on the calling thread.
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace dojo
{
class HostedPlugin
{
public:
    // loads the first plugin in `bundle`; nullptr and `error` on failure
    static std::unique_ptr<HostedPlugin> load (const juce::File& bundle, double sampleRate, int blockSize, juce::String& error);
    ~HostedPlugin();

    juce::StringArray programNames() const;
    bool selectProgram (const juce::String& name);
    juce::String programName() const;

    // set by the parameter's display name and a text the plugin parses ("85 Hz", "On", "-12.0 dB"); false if the
    // parameter does not exist or the plugin's text for the value it took differs from what was asked
    bool set (const juce::String& param, const juce::String& text, juce::String* why = nullptr);
    bool setNormalised (const juce::String& param, float value);
    juce::String text (const juce::String& param) const;
    float normalised (const juce::String& param) const;
    bool hasParameter (const juce::String& param) const { return find (param) != nullptr; }

    // processes `in` (stereo; mono is duplicated) offline after a preroll of silence; the output is aligned with the
    // input (the plugin's reported latency is removed). `cancel` stops it early (the rest of the output stays silent).
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& in, const std::atomic<bool>* cancel = nullptr);

    struct Vst3State
    {
        std::vector<uint8_t> component, controller;
    };
    Vst3State vst3State() const;            // what a DAW saves
    bool setVst3State (const Vst3State&);   // what a DAW restores

    const juce::PluginDescription& description() const { return desc; }
    int latency() const;
    int mainInputs() const { return ins; }
    int sideInputs() const { return side; }
    int mainOutputs() const { return outs; }

private:
    HostedPlugin() = default;
    juce::AudioProcessorParameter* find (const juce::String& name) const;
    std::unique_ptr<juce::AudioPluginInstance> inst;
    juce::PluginDescription desc;
    double rate = 48000.0;
    int block = 512, ins = 2, side = 0, outs = 2;
};
} // namespace dojo
