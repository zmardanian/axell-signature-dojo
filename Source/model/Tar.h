#pragma once
// Axell Signature Dojo - a strict, read-only tar reader (POSIX ustar, GNU long names, pax path records) for the
// release archives. Framework independent: it works on the decompressed bytes.
//
// Safety: an archive is accepted only if every entry is a regular file or a directory with a relative path that
// stays inside the destination (no "/" prefix, no "..", no empty or "." components, no backslashes, no NUL), every
// header checksum matches, and every size fits the data. Links, devices and FIFOs reject the whole archive, so a
// release can never make the installer write outside its staging folder.
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace dojo::tar
{
struct Entry
{
    std::string path;          // relative, '/'-separated, no trailing '/'
    bool directory = false;
    size_t offset = 0, size = 0;   // the file's bytes in the archive buffer
    unsigned mode = 0644;
};

struct Result
{
    std::vector<Entry> entries;
    std::string error;         // empty when the archive is valid
    bool ok() const noexcept { return error.empty(); }
};

namespace detail
{
inline bool octal (const uint8_t* p, size_t n, uint64_t& out) noexcept
{
    out = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0))
        ++i;
    bool any = false;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i)
    {
        if (out > (UINT64_MAX >> 3))
            return false;
        out = (out << 3) | (uint64_t) (p[i] - '0');
        any = true;
    }
    for (; i < n; ++i)   // only spaces / NULs may follow the digits
        if (p[i] != ' ' && p[i] != 0)
            return false;
    return any;
}

inline std::string cstr (const uint8_t* p, size_t n)
{
    size_t len = 0;
    while (len < n && p[len] != 0)
        ++len;
    return std::string (reinterpret_cast<const char*> (p), len);
}

// normalises "./a//b/" to "a/b"; empty result = unsafe or empty
inline std::string safePath (std::string_view raw)
{
    std::string out;
    if (raw.empty() || raw.front() == '/' || raw.find ('\\') != std::string_view::npos
        || raw.find ('\0') != std::string_view::npos)
        return {};
    size_t i = 0;
    while (i <= raw.size())
    {
        const size_t j = std::min (raw.find ('/', i), raw.size());
        const auto part = raw.substr (i, j - i);
        if (part == "..")
            return {};
        if (! part.empty() && part != ".")
        {
            if (! out.empty())
                out += '/';
            out.append (part.data(), part.size());
        }
        i = j + 1;
    }
    return out;
}

// pax extended header records: "<len> key=value\n"
inline bool paxPath (const uint8_t* p, size_t n, std::string& path)
{
    size_t i = 0;
    while (i < n)
    {
        size_t len = 0, k = i;
        while (k < n && p[k] >= '0' && p[k] <= '9')
            len = len * 10 + (size_t) (p[k++] - '0');
        if (k >= n || p[k] != ' ' || len == 0 || i + len > n || p[i + len - 1] != '\n')
            return false;
        const std::string_view rec (reinterpret_cast<const char*> (p + k + 1), i + len - 1 - (k + 1));
        const auto eq = rec.find ('=');
        if (eq == std::string_view::npos)
            return false;
        if (rec.substr (0, eq) == "path")
            path = std::string (rec.substr (eq + 1));
        i += len;
    }
    return true;
}
} // namespace detail

inline Result read (const uint8_t* data, size_t size)
{
    Result r;
    auto fail = [&r] (std::string why) { r.entries.clear(); r.error = std::move (why); return r; };
    std::string pendingLong, pendingPax;
    size_t pos = 0;
    bool ended = false;
    while (pos + 512 <= size)
    {
        const uint8_t* h = data + pos;
        bool zero = true;
        for (int i = 0; i < 512 && zero; ++i)
            zero = h[i] == 0;
        if (zero)
        {
            ended = true;   // end of archive (one or two zero blocks; anything after is padding)
            break;
        }
        // checksum: unsigned sum of the header with the checksum field read as spaces
        uint64_t stored = 0;
        if (! detail::octal (h + 148, 8, stored))
            return fail ("bad header checksum field at byte " + std::to_string (pos));
        uint64_t sum = 0;
        for (int i = 0; i < 512; ++i)
            sum += (i >= 148 && i < 156) ? (uint64_t) ' ' : (uint64_t) h[i];
        if (sum != stored)
            return fail ("header checksum mismatch at byte " + std::to_string (pos));

        uint64_t sz = 0, mode = 0;
        if (! detail::octal (h + 124, 12, sz))
            return fail ("bad size field at byte " + std::to_string (pos));
        detail::octal (h + 100, 8, mode);
        const size_t dataPos = pos + 512;
        const size_t padded = (size_t) ((sz + 511) / 512 * 512);
        if (sz > size || dataPos + padded > size + 0 || dataPos + sz > size)
            return fail ("entry runs past the end of the archive");
        const char type = (char) h[156];

        if (type == 'L')           // GNU long name for the next entry
        {
            pendingLong = detail::cstr (data + dataPos, (size_t) sz);
            pos = dataPos + padded;
            continue;
        }
        if (type == 'x')           // pax header for the next entry
        {
            if (! detail::paxPath (data + dataPos, (size_t) sz, pendingPax))
                return fail ("malformed pax header");
            pos = dataPos + padded;
            continue;
        }
        if (type == 'g')           // pax global header: nothing we use
        {
            pos = dataPos + padded;
            continue;
        }

        std::string name;
        if (! pendingPax.empty())
            name = pendingPax;
        else if (! pendingLong.empty())
            name = pendingLong;
        else
        {
            name = detail::cstr (h, 100);
            if (std::memcmp (h + 257, "ustar", 5) == 0)
            {
                const auto prefix = detail::cstr (h + 345, 155);
                if (! prefix.empty())
                    name = prefix + "/" + name;
            }
        }
        pendingLong.clear();
        pendingPax.clear();

        const bool isDir = type == '5' || ((type == 0 || type == '0') && ! name.empty() && name.back() == '/' && sz == 0);
        const bool isFile = (type == '0' || type == 0 || type == '7') && ! isDir;
        if (! isDir && ! isFile)
            return fail ("unsupported entry type '" + std::string (1, type ? type : '0') + "' for " + name
                         + " (links, devices and FIFOs are not allowed)");
        const auto clean = detail::safePath (name);
        if (clean.empty())
        {
            if (isDir && name.find_first_not_of ("./") == std::string::npos && name.front() == '.')   // "./" itself
            {
                pos = dataPos + padded;
                continue;
            }
            return fail ("unsafe path in archive: " + name);
        }
        Entry e;
        e.path = clean;
        e.directory = isDir;
        e.offset = dataPos;
        e.size = isDir ? 0 : (size_t) sz;
        e.mode = (unsigned) (mode & 0777);
        r.entries.push_back (std::move (e));
        pos = dataPos + padded;
    }
    if (! ended)
        return fail ("archive is truncated (no end marker)");
    return r;
}
} // namespace dojo::tar
