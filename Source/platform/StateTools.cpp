#include "StateTools.h"

namespace dojo::state
{
juce::ValueTree treeFromComponent (const std::vector<uint8_t>& c)
{
    if (c.size() < 8)
        return {};
    // Axell Penta EQ: int32 'PENQ' (little endian) + a binary ValueTree
    if (c[0] == 0x51 && c[1] == 0x4e && c[2] == 0x45 && c[3] == 0x50)
    {
        juce::MemoryInputStream in (c.data() + 4, c.size() - 4, false);
        return juce::ValueTree::readFromStream (in);
    }
    // JUCE's copyXmlToBinary ("VC2!" + size + XML); trailing wrapper data is ignored by the size
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (c.data(), (int) c.size()))
        return juce::ValueTree::fromXml (*xml);
    return {};
}

std::vector<uint8_t> withPresetName (const std::vector<uint8_t>& c, const juce::String& name)
{
    juce::MemoryBlock head;
    size_t used = 0;
    if (c.size() >= 8 && c[0] == 0x51 && c[1] == 0x4e && c[2] == 0x45 && c[3] == 0x50)
    {
        juce::MemoryInputStream in (c.data() + 4, c.size() - 4, false);
        auto tree = juce::ValueTree::readFromStream (in);
        if (! tree.isValid())
            return {};
        used = 4 + (size_t) in.getPosition();
        tree.setProperty ("presetName", name, nullptr);
        juce::MemoryOutputStream out (head, false);
        out.writeInt (0x50454e51);
        tree.writeToStream (out);
    }
    else if (c.size() >= 9 && c[0] == 'V' && c[1] == 'C' && c[2] == '2' && c[3] == '!')
    {
        // copyXmlToBinary: "VC2!", uint32 length of the XML text, the XML, a 0 byte
        const uint32_t len = (uint32_t) c[4] | ((uint32_t) c[5] << 8) | ((uint32_t) c[6] << 16) | ((uint32_t) c[7] << 24);
        if ((size_t) len + 9 > c.size() || c[8 + (size_t) len] != 0)
            return {};
        auto xml = juce::AudioProcessor::getXmlFromBinary (c.data(), (int) c.size());
        if (xml == nullptr)
            return {};
        used = 9 + (size_t) len;
        xml->setAttribute ("presetName", name);
        juce::AudioProcessor::copyXmlToBinary (*xml, head);
    }
    else
        return {};
    std::vector<uint8_t> out ((const uint8_t*) head.getData(), (const uint8_t*) head.getData() + head.getSize());
    out.insert (out.end(), c.begin() + (long) used, c.end());
    return out;
}

std::unique_ptr<juce::XmlElement> presetXml (const Product& p, juce::ValueTree tree, const juce::String& name,
                                             const juce::String& description)
{
    if (! tree.isValid())
        return nullptr;
    tree = tree.createCopy();
    // what each plugin's SAVE leaves out of a preset: the A/B pair and the editor's layout (Penta EQ), the learned
    // bleed path (Gate: it belongs to one recording)
    for (auto child : { "AB", "UI" })
        tree.removeChild (tree.getChildWithName (child), nullptr);
    tree.removeProperty ("bleedPath", nullptr);
    tree.removeProperty ("bleedFs", nullptr);
    tree.setProperty ("presetName", name, nullptr);
    if (p.key == "penta")
        tree.setProperty ("presetDescription", description, nullptr);
    tree.setProperty (kCreatedBy, kCreatedByValue, nullptr);
    return tree.createXml();
}

WriteResult writePreset (const Paths& paths, const Product& p, const juce::XmlElement& xml, const juce::String& name)
{
    WriteResult r;
    if (! p.hasUserPresets())
    {
        r.message = p.name + " has no user presets.";
        return r;
    }
    const auto clean = juce::File::createLegalFileName (name.trim());
    const auto folder = paths.presetFolder (p.presetFolder);
    if (clean.isEmpty() || ! folder.createDirectory())
    {
        r.message = "Cannot write to " + folder.getFullPathName();
        return r;
    }
    r.file = folder.getChildFile (clean + p.presetExt);
    if (r.file.existsAsFile())
    {
        auto old = juce::XmlDocument::parse (r.file);
        if (old == nullptr || old->getStringAttribute (kCreatedBy) != kCreatedByValue)
        {
            r.message = "A preset of yours is already called \"" + name + "\" in " + p.name + "; it was left as it is.";
            return r;
        }
    }
    // write next to it and rename, so a crash never leaves half a preset
    const auto tmp = r.file.getSiblingFile (r.file.getFileName() + ".tmp");
    if (! xml.writeTo (tmp) || ! tmp.moveFileTo (r.file))
    {
        tmp.deleteFile();
        r.message = "Cannot write " + r.file.getFullPathName();
        return r;
    }
    r.ok = true;
    r.message = p.name + ": preset \"" + name + "\" saved.";
    return r;
}

int32_t reaperUid (const Paths& paths, const Product& p, const juce::PluginDescription&)
{
    // reaper-vstplugins64.ini: <file_name_with_underscores>=<time>,<uid>{<class id>,<name>
    juce::StringArray lines;
    paths.reaperConfig().getChildFile ("reaper-vstplugins64.ini").readLines (lines);
    const auto classId = p.classId();
    for (const auto& line : lines)
    {
        const auto at = line.indexOf ("{" + classId);
        if (at < 0)
            continue;
        const auto before = line.substring (0, at);
        const auto uid = before.fromLastOccurrenceOf (",", false, false).trim();
        if (uid.isNotEmpty() && uid.containsOnly ("-0123456789"))
            return (int32_t) uid.getLargeIntValue();
    }
    return 0;
}

reaper::Vst3Plugin reaperPlugin (const Product& p, const HostedPlugin& h, int32_t uid)
{
    reaper::Vst3Plugin r;
    r.name = (p.name + " (" + Catalog::kVendor + ")").toStdString();
    r.file = p.bundle.toStdString();
    r.uid = uid;
    r.classId = p.classId().toStdString();
    r.inputs = h.mainInputs() + h.sideInputs();
    r.outputs = h.mainOutputs();
    const auto s = h.vst3State();
    r.component = s.component;
    r.controller = s.controller;
    r.program = h.programName().toStdString();
    return r;
}

juce::String newFxId()
{
    auto u = juce::Uuid().toDashedString().toUpperCase();
    return "{" + u + "}";
}
} // namespace dojo::state
