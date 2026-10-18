#include "AudioEngine.h"

namespace dojo::gui
{
AudioEngine::AudioEngine()
{
    writerThread.startThread();
}

AudioEngine::~AudioEngine()
{
    stopRecording();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    writerThread.stopThread (2000);
}

bool AudioEngine::open (juce::String& error)
{
    if (opened)
        return true;
    error = deviceManager.initialiseWithDefaultDevices (2, 2);
    if (error.isNotEmpty())
        return false;
    deviceManager.addAudioCallback (this);
    opened = true;
    return true;
}

juce::String AudioEngine::deviceDescription() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getName() + ", " + juce::String (juce::roundToInt (d->getCurrentSampleRate())) + " Hz, " + juce::String (d->getCurrentBufferSizeSamples()) + " samples";
    return "No audio device";
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    deviceRate = d->getCurrentSampleRate();
    numInputs = juce::jmin (2, d->getActiveInputChannels().countNumberOfSetBits());
}

bool AudioEngine::startRecording (const juce::File& wav, juce::String& error)
{
    stopRecording();
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
    {
        error = "No audio input is open.";
        return false;
    }
    const int ch = juce::jmax (1, juce::jmin (2, dev->getActiveInputChannels().countNumberOfSetBits()));
    if (dev->getActiveInputChannels().isZero())
    {
        error = "The audio device has no active input. Choose an input in Audio settings.";
        return false;
    }
    wav.getParentDirectory().createDirectory();
    wav.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
    if (stream == nullptr)
    {
        error = "Cannot write " + wav.getFullPathName();
        return false;
    }
    juce::WavAudioFormat fmt;
    auto options = juce::AudioFormatWriterOptions().withSampleRate (dev->getCurrentSampleRate())
                                                   .withNumChannels (ch)
                                                   .withBitsPerSample (24);
    auto w = fmt.createWriterFor (stream, options);
    if (w == nullptr)
    {
        error = "Could not start a WAV file.";
        return false;
    }
    {
        const juce::ScopedLock sl (writerLock);
        writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 1 << 17);
    }
    recordedSamples = 0;
    recording = true;
    return true;
}

void AudioEngine::stopRecording()
{
    recording = false;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> done;
    {
        const juce::ScopedLock sl (writerLock);
        done = std::move (writer);
    }
    done.reset();   // flushes and closes the file
}

double AudioEngine::recordedSeconds() const
{
    return (double) recordedSamples.load() / juce::jmax (1.0, deviceRate);
}

void AudioEngine::setPlayback (const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>& processed, double sampleRate,
                               bool keepPosition)
{
    juce::AudioBuffer<float> a (original), b (processed.getNumSamples() > 0 ? processed : original);
    const juce::SpinLock::ScopedLockType sl (playLock);
    bufA = std::move (a);
    bufB = std::move (b);
    playRate = sampleRate;
    if (! keepPosition)
    {
        readPos = 0.0;
        posSeconds = 0.0;
    }
}

void AudioEngine::play() { playing = true; }
void AudioEngine::stop() { playing = false; }
double AudioEngine::position() const { return posSeconds.load(); }
void AudioEngine::seek (double s) { seekTo = juce::jmax (0.0, s); }

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // input meters + recording
    const float decay = std::pow (0.5f, (float) n / (float) (0.12 * deviceRate));
    for (int c = 0; c < 2; ++c)
    {
        float pk = 0.0f;
        if (c < numIn && in[c] != nullptr)
            for (int i = 0; i < n; ++i)
                pk = juce::jmax (pk, std::abs (in[c][i]));
        inPeak[c] = juce::jmax (pk, inPeak[c].load() * decay);
    }
    if (recording.load() && numIn > 0)
    {
        const juce::ScopedTryLock tl (writerLock);
        if (tl.isLocked() && writer != nullptr && writer->write (in, n))
            recordedSamples += n;
    }

    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr)
            juce::FloatVectorOperations::clear (out[c], n);
    if (! playing.load())
        return;
    const juce::SpinLock::ScopedTryLockType tl (playLock);
    if (! tl.isLocked() || bufA.getNumSamples() == 0)
        return;
    if (const double s = seekTo.exchange (-1.0); s >= 0.0)
        readPos = juce::jmin (s * playRate, (double) bufA.getNumSamples() - 1.0);
    const double step = playRate / deviceRate;   // linear-interpolated rate conversion when the device runs elsewhere
    const float target = wantProcessed.load() ? 1.0f : 0.0f;
    const float fadeStep = 1.0f / (float) (0.02 * deviceRate);   // 20 ms crossfade between A and B
    const int len = juce::jmin (bufA.getNumSamples(), bufB.getNumSamples());
    const float gB = gainB.load();
    for (int i = 0; i < n; ++i)
    {
        if (readPos >= (double) (len - 1))
        {
            playing = false;
            readPos = 0.0;
            break;
        }
        mixB += juce::jlimit (-fadeStep, fadeStep, target - mixB);
        const int k = (int) readPos;
        const float fr = (float) (readPos - k);
        for (int c = 0; c < juce::jmin (2, numOut); ++c)
        {
            if (out[c] == nullptr)
                continue;
            const int src = juce::jmin (c, bufA.getNumChannels() - 1);
            const float a = bufA.getSample (src, k) + fr * (bufA.getSample (src, k + 1) - bufA.getSample (src, k));
            const float b = (bufB.getSample (src, k) + fr * (bufB.getSample (src, k + 1) - bufB.getSample (src, k))) * gB;
            out[c][i] = a + (b - a) * mixB;
        }
        readPos += step;
    }
    posSeconds = readPos / playRate;
}
} // namespace dojo::gui
