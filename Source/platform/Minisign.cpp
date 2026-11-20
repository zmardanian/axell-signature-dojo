#include "Minisign.h"

#include "monocypher-ed25519.h"
#include "monocypher.h"

namespace dojo::minisign
{
namespace
{
bool decode (const juce::String& text, juce::MemoryBlock& out, size_t expectedSize)
{
    juce::MemoryOutputStream s;
    if (! juce::Base64::convertFromBase64 (s, text.trim()))
        return false;
    out = s.getMemoryBlock();
    return out.getSize() == expectedSize;
}
}

Result verify (const juce::MemoryBlock& message, const juce::String& signatureText, const juce::String& publicKey)
{
    Result r;
    juce::MemoryBlock key;
    if (! decode (publicKey, key, 42) || std::memcmp (key.getData(), "Ed", 2) != 0)
    {
        r.error = "The release key is not a minisign Ed25519 key.";
        return r;
    }
    // untrusted comment / signature / trusted comment / global signature
    auto lines = juce::StringArray::fromLines (signatureText.trimEnd());
    lines.removeEmptyStrings();
    if (lines.size() != 4 || ! lines[0].startsWith ("untrusted comment:") || ! lines[2].startsWith ("trusted comment: "))
    {
        r.error = "The signature file is not a minisign signature.";
        return r;
    }
    juce::MemoryBlock sig, global;
    if (! decode (lines[1], sig, 74) || ! decode (lines[3], global, 64))
    {
        r.error = "The signature file is damaged.";
        return r;
    }
    const auto* s = static_cast<const uint8_t*> (sig.getData());
    const auto* k = static_cast<const uint8_t*> (key.getData());
    const bool prehashed = s[0] == 'E' && s[1] == 'D';
    if (! prehashed && ! (s[0] == 'E' && s[1] == 'd'))
    {
        r.error = "The signature uses an unknown algorithm.";
        return r;
    }
    if (std::memcmp (s + 2, k + 2, 8) != 0)
    {
        r.error = "The release was signed with a different key than the suite's.";
        return r;
    }
    uint8_t hash[64];
    const uint8_t* signedData = static_cast<const uint8_t*> (message.getData());
    size_t signedSize = message.getSize();
    if (prehashed)
    {
        crypto_blake2b (hash, sizeof (hash), signedData, signedSize);
        signedData = hash;
        signedSize = sizeof (hash);
    }
    if (crypto_ed25519_check (s + 10, k + 10, signedData, signedSize) != 0)
    {
        r.error = "The signature does not match: the checksums are not the published ones.";
        return r;
    }
    // the trusted comment is signed together with the signature
    const auto comment = lines[2].fromFirstOccurrenceOf ("trusted comment: ", false, false);
    juce::MemoryBlock both (s + 10, 64);
    both.append (comment.toRawUTF8(), comment.getNumBytesAsUTF8());
    if (crypto_ed25519_check (static_cast<const uint8_t*> (global.getData()), k + 10,
                              static_cast<const uint8_t*> (both.getData()), both.getSize()) != 0)
    {
        r.error = "The signature's comment was altered.";
        return r;
    }
    r.ok = true;
    r.trustedComment = comment;
    return r;
}

Result verifyFile (const juce::File& message, const juce::File& signature, const juce::String& publicKey)
{
    juce::MemoryBlock data;
    if (! message.loadFileAsData (data))
        return { false, "Cannot read " + message.getFileName() + ".", {} };
    if (! signature.existsAsFile() || signature.getSize() > 4096)
        return { false, "The signature file is missing.", {} };
    return verify (data, signature.loadFileAsString(), publicKey);
}
}
