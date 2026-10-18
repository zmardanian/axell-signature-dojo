#pragma once
// Axell Signature Dojo - recording a take and playing it back (original and through the tuned chain, A/B at the same
// position). The audio callback never allocates or blocks: recording goes through JUCE's threaded WAV writer, and the
// playback buffers are swapped in under a try-lock (a block that cannot get it plays silence).
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

namespace dojo::gui
{
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& devices() { return deviceManager; }
    bool open (juce::String& error);           // the default devices (or the last choice), 2 in / 2 out
    bool isOpen() const { return opened; }
    juce::String deviceDescription() const;

    // recording
    bool startRecording (const juce::File& wav, juce::String& error);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    double recordedSeconds() const;
    float inputLevel (int channel) const { return inPeak[juce::jlimit (0, 1, channel)].load(); }   // 0..1, decaying
    int inputChannels() const { return numInputs.load(); }

    // playback
    void setPlayback (const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>& processed, double sampleRate);
    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }
    double position() const;
    void seek (double seconds);
    void setProcessed (bool useProcessed) { wantProcessed = useProcessed; }
    bool processedSelected() const { return wantProcessed.load(); }

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    juce::AudioDeviceManager deviceManager;
    bool opened = false;
    double deviceRate = 48000.0;

    juce::TimeSliceThread writerThread { "Dojo recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::CriticalSection writerLock;          // guards `writer` against the callback (try-lock there)
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> recordedSamples { 0 };
    std::atomic<float> inPeak[2] {};
    std::atomic<int> numInputs { 0 };

    juce::SpinLock playLock;
    juce::AudioBuffer<float> bufA, bufB;       // original, processed
    double playRate = 48000.0;
    double readPos = 0.0;                      // in samples of the take
    std::atomic<double> posSeconds { 0.0 };
    std::atomic<double> seekTo { -1.0 };
    std::atomic<bool> playing { false }, wantProcessed { true };
    float mixB = 1.0f;                         // crossfade 0 = original, 1 = processed
};
} // namespace dojo::gui
