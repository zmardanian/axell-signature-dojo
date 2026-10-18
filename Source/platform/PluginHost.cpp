#include "PluginHost.h"

namespace dojo
{
namespace
{
template <typename Fn>
auto onMessageThread (Fn&& fn)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        return fn();
    using R = decltype (fn());
    R result {};
    juce::MessageManager::callSync ([&] { result = fn(); });
    return result;
}
} // namespace

std::unique_ptr<HostedPlugin> HostedPlugin::load (const juce::File& bundle, double sampleRate, int blockSize, juce::String& error)
{
    std::unique_ptr<HostedPlugin> h (new HostedPlugin());
    h->rate = sampleRate;
    h->block = blockSize;
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> found;
    vst3.findAllTypesForFile (found, bundle.getFullPathName());
    if (found.isEmpty())
    {
        error = "No plugin found in " + bundle.getFileName();
        return nullptr;
    }
    h->desc = *found[0];
    h->inst = onMessageThread ([&]
    {
        juce::String err;
        auto p = vst3.createInstanceFromDescription (h->desc, sampleRate, blockSize, err);
        if (p == nullptr)
            error = err.isNotEmpty() ? err : "The plugin could not be opened.";
        return p;
    });
    if (h->inst == nullptr)
        return nullptr;

    // main bus stereo in/out; a side-chain input, if any, stays connected but silent
    auto layout = h->inst->getBusesLayout();
    if (layout.inputBuses.size() > 0)
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (layout.outputBuses.size() > 0)
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    for (int b = 1; b < layout.inputBuses.size(); ++b)
        layout.inputBuses.getReference (b) = juce::AudioChannelSet::disabled();
    if (! h->inst->setBusesLayout (layout))
    {
        error = "The plugin refused a stereo layout.";
        return nullptr;
    }
    h->ins = layout.getMainInputChannels();
    h->outs = layout.getMainOutputChannels();
    // the pins a DAW connects include the side-chain bus when the plugin has one (REAPER's chunk records them)
    h->side = 0;
    for (int b = 1; b < h->inst->getBusCount (true); ++b)
        if (auto* bus = h->inst->getBus (true, b))
            h->side += bus->getDefaultLayout().size();
    return h;
}

HostedPlugin::~HostedPlugin()
{
    if (inst != nullptr)
        onMessageThread ([this] { inst.reset(); return true; });
}

juce::AudioProcessorParameter* HostedPlugin::find (const juce::String& name) const
{
    for (auto* p : inst->getParameters())
        if (p->getName (256) == name)
            return p;
    return nullptr;
}

juce::StringArray HostedPlugin::programNames() const
{
    juce::StringArray out;
    for (int i = 0; i < inst->getNumPrograms(); ++i)
        out.add (inst->getProgramName (i));
    return out;
}

juce::String HostedPlugin::programName() const
{
    return inst->getProgramName (inst->getCurrentProgram());
}

bool HostedPlugin::selectProgram (const juce::String& name)
{
    const auto names = programNames();
    const int i = names.indexOf (name);
    if (i < 0)
        return false;
    onMessageThread ([&] { inst->setCurrentProgram (i); return true; });
    return inst->getCurrentProgram() == i;
}

bool HostedPlugin::set (const juce::String& param, const juce::String& text, juce::String* why)
{
    auto* p = find (param);
    if (p == nullptr)
    {
        if (why != nullptr)
            *why = "no parameter '" + param + "'";
        return false;
    }
    const float v = p->getValueForText (text);
    p->setValueNotifyingHost (v);
    const auto got = p->getText (p->getValue(), 64);
    // the plugin may format the value its own way ("85 Hz" -> "85.0 Hz"): compare the numbers when both are numeric
    auto number = [] (const juce::String& t) { return t.retainCharacters ("0123456789.-+").getDoubleValue(); };
    const bool numeric = got.containsAnyOf ("0123456789") && text.containsAnyOf ("0123456789");
    const bool same = got.trim().equalsIgnoreCase (text.trim())
                      || (numeric && std::abs (number (got) - number (text)) <= std::max (0.051, 0.005 * std::abs (number (text))));
    if (! same && why != nullptr)
        *why = "'" + param + "' became '" + got + "', not '" + text + "'";
    return same;
}

bool HostedPlugin::setNormalised (const juce::String& param, float value)
{
    if (auto* p = find (param))
    {
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        return true;
    }
    return false;
}

juce::String HostedPlugin::text (const juce::String& param) const
{
    auto* p = find (param);
    return p != nullptr ? p->getText (p->getValue(), 64) : juce::String();
}

float HostedPlugin::normalised (const juce::String& param) const
{
    auto* p = find (param);
    return p != nullptr ? p->getValue() : -1.0f;
}

int HostedPlugin::latency() const { return inst->getLatencySamples(); }

juce::AudioBuffer<float> HostedPlugin::render (const juce::AudioBuffer<float>& in, const std::atomic<bool>* cancel)
{
    const int n = in.getNumSamples();
    const int nBuf = juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(), 2);
    inst->setNonRealtime (true);
    inst->prepareToPlay (rate, block);
    juce::AudioBuffer<float> buf (nBuf, block);
    juce::MidiBuffer midi;
    // preroll: parameter smoothing, warm-ups and any latency change settle on silence
    const int preroll = (int) (0.5 * rate);
    for (int pos = 0; pos < preroll; pos += block)
    {
        buf.setSize (nBuf, juce::jmin (block, preroll - pos), false, false, true);
        buf.clear();
        inst->processBlock (buf, midi);
    }
    const int lat = juce::jmax (0, inst->getLatencySamples());
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    const int total = n + lat;
    for (int pos = 0; pos < total;)
    {
        if (cancel != nullptr && cancel->load())
            break;
        const int m = juce::jmin (block, total - pos);
        buf.setSize (nBuf, m, false, false, true);
        buf.clear();
        for (int c = 0; c < 2; ++c)
            if (pos < n)
                buf.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, juce::jmin (m, n - pos));
        inst->processBlock (buf, midi);
        // output sample (pos + i) belongs to input sample (pos + i - lat)
        for (int c = 0; c < 2; ++c)
        {
            const int src = juce::jmin (c, inst->getTotalNumOutputChannels() - 1);
            for (int i = 0; i < m; ++i)
            {
                const int t = pos + i - lat;
                if (t >= 0 && t < n)
                    out.setSample (c, t, buf.getSample (src, i));
            }
        }
        pos += m;
    }
    inst->releaseResources();
    return out;
}

HostedPlugin::Vst3State HostedPlugin::vst3State() const
{
    return onMessageThread ([this]
    {
        Vst3State s;
        juce::MemoryBlock mb;
        inst->getStateInformation (mb);
        // JUCE's VST3 host stores <VST3PluginState><IComponent>base64</IComponent><IEditController>...</...>
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
        {
            auto decode = [] (const juce::String& b64)
            {
                juce::MemoryBlock m;
                m.fromBase64Encoding (b64);
                return std::vector<uint8_t> ((const uint8_t*) m.getData(), (const uint8_t*) m.getData() + m.getSize());
            };
            if (auto* c = xml->getChildByName ("IComponent"))
                s.component = decode (c->getAllSubText().trim());
            if (auto* e = xml->getChildByName ("IEditController"))
                s.controller = decode (e->getAllSubText().trim());
        }
        return s;
    });
}

bool HostedPlugin::setVst3State (const Vst3State& s)
{
    return onMessageThread ([&]
    {
        juce::XmlElement xml ("VST3PluginState");
        // JUCE's host encodes each stream with MemoryBlock's own base64 form (and decodes it the same way)
        auto encode = [] (const std::vector<uint8_t>& v) { return juce::MemoryBlock (v.data(), v.size()).toBase64Encoding(); };
        xml.createNewChildElement ("IComponent")->addTextElement (encode (s.component));
        if (! s.controller.empty())
            xml.createNewChildElement ("IEditController")->addTextElement (encode (s.controller));
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (xml, mb);
        inst->setStateInformation (mb.getData(), (int) mb.getSize());
        return true;
    });
}
} // namespace dojo
