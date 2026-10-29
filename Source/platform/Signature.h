#pragma once
// Axell Signature Dojo - signature sounds: a vocal chain and a master chain the artist approved by ear, with the level
// of the dry take they were tuned on. One click puts a new take through them: the take is gain-matched to that level
// (every level-dependent stage - gate threshold, compressors, de-esser - then works as it did on the approved take)
// and the master's loudness is set to the chosen target.
#include "Chains.h"
#include "Paths.h"

#include <vector>

namespace dojo
{
struct Signature
{
    struct Target
    {
        float lufs = -14.0f;          // integrated loudness of the result
        float masterInputDb = 0.0f;   // Axell Master's INPUT that gives it (measured on the tuning take)
    };
    juce::String name, description;
    juce::File vocalChain, masterChain;   // in REAPER's FX chain folder
    float referenceLufs = -70.0f;         // the tuning take's integrated loudness (BS.1770; a mono take as dual mono)
    juce::String referenceTake;           // its file name (shown, never opened)
    std::vector<Target> targets;
    juce::File file;                      // <Dojo data>/signatures/<name>.xml

    bool usable() const { return vocalChain.existsAsFile() && masterChain.existsAsFile() && referenceLufs > -60.0f && ! targets.empty(); }
    static juce::File folder (const Paths& p) { return p.dojoData().getChildFile ("signatures"); }
    static std::vector<Signature> all (const Paths&);   // every usable signature, by name
};

struct SignatureSession
{
    bool ok = false;
    juce::String message;
    juce::File project;
    float takeLufs = -70.0f, gainDb = 0.0f;
};

// the gain that brings `takeLufs` to the signature's reference level (clamped to -30 .. +40 dB)
float signatureGainDb (const Signature&, float takeLufs);
// the master chain's text with Axell Master's INPUT set for `target`; every other stage's state byte for byte
juce::String signatureMasterChain (const Paths&, const Signature&, const Signature::Target&, juce::String& error);
// a REAPER project: the master chain on the master track, one track holding the take (its volume = the gain match)
// and the vocal chain
juce::String signatureProjectText (const Signature&, const juce::File& take, double takeSeconds, float gainDb,
                                   const juce::String& vocalChainText, const juce::String& masterChainText);
// measures the take, writes "<take> - <signature> (<target> LUFS).RPP" into ~/Documents/Axell Signature (never
// replacing a file) and opens REAPER on it (not in a sandbox, not when `open` is false)
SignatureSession makeSignatureSession (const Paths&, const Signature&, const juce::File& take, float targetLufs, bool open);
} // namespace dojo
