#pragma once
// Axell Signature Dojo - REAPER's text form of a VST3 plugin with its state, and FX chain files (.RfxChain).
// Framework independent.
//
// A VST3 plugin in a REAPER project or FX chain is a block
//     <VST "VST3: <name> (<vendor>)" "<file>.vst3" 0 "" <uid>{<class id, 32 hex>} ""
//       <segment 1: header, base64>
//       <segment 2: state, base64, wrapped at 128 characters>
//       <segment 3: trailer, base64>
//     >
// Each segment is encoded on its own (so a segment ends where a line ends with '=' padding or the block ends).
// Measured from projects REAPER 7 wrote (docs/DESIGN.md, D4):
//   header  = int32 uid, int32 0xFEED5EEE, int32 nIn, nIn x uint64 pin masks (1 << pin), int32 nOut,
//             nOut x uint64 masks, int32 state size, int32 1, int32 0                      (all little endian)
//   state   = int32 component size, int32 1, component bytes, int32 controller size, int32 0, controller bytes
//             (the VST3 IComponent / IEditController getState() streams)
//   trailer = 0x00, program name, 0x00, int32 0
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dojo::reaper
{
//== base64 (RFC 4648) ===========================================================================================
inline std::string base64Encode (const uint8_t* p, size_t n)
{
    static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve ((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3)
    {
        const uint32_t a = p[i], b = i + 1 < n ? p[i + 1] : 0u, c = i + 2 < n ? p[i + 2] : 0u;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        out += i + 2 < n ? tbl[v & 63] : '=';
    }
    return out;
}

// strict: only the base64 alphabet, length a multiple of 4, padding only at the end; false on anything else
inline bool base64Decode (std::string_view s, std::vector<uint8_t>& out)
{
    auto val = [] (char ch) -> int
    {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    if (s.size() % 4 != 0)
        return false;
    out.clear();
    out.reserve (s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4)
    {
        const bool last = i + 4 == s.size();
        const int a = val (s[i]), b = val (s[i + 1]);
        const int c = (last && s[i + 2] == '=') ? -2 : val (s[i + 2]);
        const int d = (last && s[i + 3] == '=') ? -2 : val (s[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2))
            return false;
        out.push_back ((uint8_t) ((a << 2) | (b >> 4)));
        if (c >= 0)
            out.push_back ((uint8_t) (((b & 15) << 4) | (c >> 2)));
        if (d >= 0)
            out.push_back ((uint8_t) (((c & 3) << 6) | d));
    }
    return true;
}

//== binary helpers ==============================================================================================
inline void putI32 (std::vector<uint8_t>& v, int32_t x)
{
    const auto u = (uint32_t) x;
    for (int i = 0; i < 4; ++i)
        v.push_back ((uint8_t) (u >> (8 * i)));
}
inline void putU64 (std::vector<uint8_t>& v, uint64_t x)
{
    for (int i = 0; i < 8; ++i)
        v.push_back ((uint8_t) (x >> (8 * i)));
}
inline int32_t getI32 (const uint8_t* p) noexcept
{
    return (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24));
}

constexpr int32_t kMagic = (int32_t) 0xFEED5EEE;

//== one plugin ==================================================================================================
struct Vst3Plugin
{
    std::string name;              // "Axell Gate (Axell Yobag)" - shown as "VST3: <name>"
    std::string file;              // "Axell Gate.vst3"
    int32_t uid = 0;               // REAPER's numeric id (reaper-vstplugins64.ini, or computed: D4)
    std::string classId;           // 32 upper-case hex digits
    int inputs = 2, outputs = 2;   // pins REAPER connects: main inputs + side-chain inputs, main outputs
    std::vector<uint8_t> component, controller;
    std::string program;           // shown by REAPER as the preset name
};

inline std::vector<uint8_t> header (const Vst3Plugin& p, size_t stateSize)
{
    std::vector<uint8_t> h;
    putI32 (h, p.uid);
    putI32 (h, kMagic);
    putI32 (h, p.inputs);
    for (int i = 0; i < p.inputs; ++i)
        putU64 (h, (uint64_t) 1 << i);
    putI32 (h, p.outputs);
    for (int i = 0; i < p.outputs; ++i)
        putU64 (h, (uint64_t) 1 << i);
    putI32 (h, (int32_t) stateSize);
    putI32 (h, 1);
    putI32 (h, 0);
    return h;
}

inline std::vector<uint8_t> state (const Vst3Plugin& p)
{
    std::vector<uint8_t> s;
    s.reserve (p.component.size() + p.controller.size() + 16);
    putI32 (s, (int32_t) p.component.size());
    putI32 (s, 1);
    s.insert (s.end(), p.component.begin(), p.component.end());
    putI32 (s, (int32_t) p.controller.size());
    putI32 (s, 0);
    s.insert (s.end(), p.controller.begin(), p.controller.end());
    return s;
}

inline std::vector<uint8_t> trailer (const Vst3Plugin& p)
{
    std::vector<uint8_t> t;
    t.push_back (0);
    for (char ch : p.program)
        if (ch != '\0')
            t.push_back ((uint8_t) ch);
    t.push_back (0);
    putI32 (t, 0);
    return t;
}

// a REAPER text value: quoted, with REAPER's rule for names that contain quotes (use the other quote / backtick)
inline std::string quote (const std::string& s)
{
    if (s.find ('"') == std::string::npos)
        return "\"" + s + "\"";
    if (s.find ('\'') == std::string::npos)
        return "'" + s + "'";
    return "`" + s + "`";
}

inline void appendWrapped (std::string& out, const std::vector<uint8_t>& bytes, const std::string& indent)
{
    const auto b64 = base64Encode (bytes.data(), bytes.size());
    for (size_t i = 0; i < b64.size(); i += 128)
        out += indent + b64.substr (i, 128) + "\n";
}

// the <VST ...> block (lines end with '\n'; `indent` precedes every line of the block)
inline std::string vstBlock (const Vst3Plugin& p, const std::string& indent = "")
{
    const auto st = state (p);
    std::string out = indent + "<VST " + quote ("VST3: " + p.name) + " " + quote (p.file) + " 0 \"\" "
                      + std::to_string (p.uid) + "{" + p.classId + "} \"\"\n";
    appendWrapped (out, header (p, st.size()), indent + "  ");
    appendWrapped (out, st, indent + "  ");
    appendWrapped (out, trailer (p), indent + "  ");
    out += indent + ">\n";
    return out;
}

//== FX chain file ===============================================================================================
struct ChainEntry
{
    Vst3Plugin plugin;
    bool bypassed = false;
    std::string fxid;              // "{8-4-4-4-12}" upper-case GUID, unique per entry
};

inline std::string fxChain (const std::vector<ChainEntry>& chain)
{
    std::string out;
    for (const auto& e : chain)
    {
        out += std::string ("BYPASS ") + (e.bypassed ? "1" : "0") + " 0 0\n";
        out += vstBlock (e.plugin);
        out += "PRESETNAME " + quote (e.plugin.program) + "\n";
        out += "FLOATPOS 0 0 0 0\n";
        out += "FXID " + e.fxid + "\n";
        out += "WAK 0 0\n";
    }
    return out;
}

//== reading (tests and verification) ============================================================================
struct Parsed
{
    int32_t uid = 0, inputs = 0, outputs = 0, stateSize = 0;
    std::vector<uint8_t> component, controller;
    std::string program;
    bool ok = false;
};

// the base64 lines of one <VST> block (without the "<VST" line and ">"). Each base64 run (ended by '=' padding, a
// short line or the block's end) decodes on its own; the decoded runs, joined, are header + state + trailer, and the
// header gives the state's size - so a state that happens to fill its last line exactly is still split correctly.
inline Parsed parseBlock (const std::vector<std::string>& lines)
{
    Parsed r;
    std::vector<uint8_t> all, run;
    std::string cur;
    size_t headerSize = 0;
    for (size_t k = 0; k < lines.size(); ++k)
    {
        std::string_view l (lines[k]);
        while (! l.empty() && (l.front() == ' ' || l.front() == '\t'))
            l.remove_prefix (1);
        while (! l.empty() && (l.back() == ' ' || l.back() == '\r'))
            l.remove_suffix (1);
        cur += l;
        if (k == 0 || l.size() < 128 || l.back() == '=' || k + 1 == lines.size())
        {
            if (! base64Decode (cur, run))
                return r;
            all.insert (all.end(), run.begin(), run.end());
            if (k == 0)
                headerSize = all.size();
            cur.clear();
        }
    }
    const uint8_t* h = all.data();
    if (headerSize < 12 || getI32 (h + 4) != kMagic)
        return r;
    r.uid = getI32 (h);
    size_t q = 8;
    r.inputs = getI32 (h + q);
    if (r.inputs < 0 || r.inputs > 64) return r;
    q += 4 + 8 * (size_t) r.inputs;
    if (q + 4 > headerSize) return r;
    r.outputs = getI32 (h + q);
    if (r.outputs < 0 || r.outputs > 64) return r;
    q += 4 + 8 * (size_t) r.outputs;
    if (q + 12 != headerSize) return r;
    r.stateSize = getI32 (h + q);
    if (r.stateSize < 16 || headerSize + (size_t) r.stateSize > all.size())
        return r;
    const uint8_t* st = h + headerSize;
    const size_t stSize = (size_t) r.stateSize;
    const auto cs = (size_t) (uint32_t) getI32 (st);
    if (16 + cs > stSize) return r;
    r.component.assign (st + 8, st + 8 + cs);
    const auto ks = (size_t) (uint32_t) getI32 (st + 8 + cs);
    if (16 + cs + ks != stSize) return r;
    r.controller.assign (st + 16 + cs, st + stSize);
    const uint8_t* t = st + stSize;
    const size_t tSize = all.size() - headerSize - stSize;
    if (tSize < 6 || t[0] != 0)
        return r;
    for (size_t i = 1; i < tSize && t[i] != 0; ++i)
        r.program += (char) t[i];
    r.ok = true;
    return r;
}
} // namespace dojo::reaper
