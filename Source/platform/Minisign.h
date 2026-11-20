#pragma once
// Axell Signature Dojo - minisign signatures (https://jedisct1.github.io/minisign/): every release's SHA256SUMS.txt is
// signed with the suite's release key, so a download is checked against the key built into the Dojo, not just against
// a checksum file that came from the same place. Ed25519 and BLAKE2b from Monocypher (CC0 / BSD-2-Clause).
#include <juce_core/juce_core.h>

namespace dojo::minisign
{
// the suite's release key (the private half never leaves the maintainer's machine)
inline constexpr const char* kReleaseKey = "RWQJLVJSETsP7RRz1rdiugX9w09wtGCtwEc5hHU8k9S+crL/ekfTkQKG";

struct Result
{
    bool ok = false;
    juce::String error;            // why it failed, in the user's words
    juce::String trustedComment;   // the signed comment ("Axell Gate 4.0.3 release checksums")
};

// checks `message` against a .minisig file's text with a minisign public key ("RW..." base64, key id + Ed25519 key):
// the key id must match, the signature (raw or BLAKE2b-prehashed) and the signed trusted comment must both verify
Result verify (const juce::MemoryBlock& message, const juce::String& signatureText, const juce::String& publicKey = kReleaseKey);
Result verifyFile (const juce::File& message, const juce::File& signature, const juce::String& publicKey = kReleaseKey);
}
