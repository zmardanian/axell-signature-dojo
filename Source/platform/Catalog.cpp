#include "Catalog.h"

namespace dojo
{
juce::String Product::classId() const
{
    juce::String id ("ABCDEF019182FAEB");
    for (auto code : { manufacturerCode, pluginCode })
        for (int i = 0; i < 4; ++i)
            id << juce::String::toHexString ((int) (juce::uint8) code[i]).paddedLeft ('0', 2).toUpperCase();
    return id;
}

const std::vector<Product>& Catalog::products()
{
    // signal order of a vocal chain first, then the master
    static const std::vector<Product> list {
        { "dereverb", "Axell De-Reverb", "axell-dereverb", "Axell De-Reverb.vst3", "AxellDeReverb", "", "",
          "Axyb", "Drv1", "Removes the room from a vocal", "Room removal for vocals", "Vocal",
          juce::Colour (0xff4fb3d9), "dereverb_png" },
        { "gate", "Axell Gate", "axell-gate", "Axell Gate.vst3", "AxellGate", "AxellGate", ".axgpreset",
          "Axyb", "Gte1", "Opens for the voice, not for the room", "Smart vocal gate + drum shaper + rhythm", "Vocal",
          juce::Colour (0xff8b7fe0), "gate_png" },
        { "penta", "Axell Penta EQ", "axell-penta-eq", "Axell Penta EQ.vst3", "AxellPentaEQ", "AxellPentaEQ", ".pentapreset",
          "Axyb", "Pnq1", "Analog-accurate EQ with character modules", "Parametric, inductor, console, tube, air", "Mix",
          juce::Colour (0xffd9b44a), "penta_png" },
        { "duo", "Axell Duo Compressor", "axell-duo-compressor", "Axell Duo Compressor.vst3", "AxellDuoCompressor",
          "AxellDuoCompressor", ".duopreset", "Dcmp", "Dvc1", "FET for the peaks, opto for the level",
          "FET + opto + true-peak limiter", "Vocal", juce::Colour (0xfff08a3c), "duo_png" },
        { "velvet", "Axell Velvet", "axell-velvet", "Axell Velvet.vst3", "AxellVelvet", "AxellVelvet", ".axvpreset",
          "Axyb", "Vlv1", "Smooth esses, warm the tone", "Smart de-esser + analog heat", "Vocal",
          juce::Colour (0xff33c1b5), "velvet_png" },
        { "space", "Axell Space", "axell-space", "Axell Space.vst3", "AxellSpace", "", "",
          "Axyb", "Spc1", "Reverb and delay that keep the esses out", "Vocal reverb + tempo-synced delay", "Vocal",
          juce::Colour (0xffb48cf0), "space_png" },
        { "master", "Axell Master", "axell-master", "Axell Master.vst3", "AxellMaster", "AxellMaster", ".axmpreset",
          "Axyb", "Mst1", "Loud, clean, streaming-safe masters", "Clipper + true-peak limiter + loudness", "Master",
          juce::Colour (0xff58c27d), "master_png" },
    };
    return list;
}

const Product* Catalog::find (const juce::String& key)
{
    for (auto& p : products())
        if (p.key == key)
            return &p;
    return nullptr;
}

const std::vector<Catalog::Legacy>& Catalog::legacy()
{
    static const std::vector<Legacy> list { { "Duo Vocal Comp.vst3", "duo", "Duo Vocal Comp" } };
    return list;
}
} // namespace dojo
