#pragma once
// Axell Signature Dojo - release versions ("2.0.1", "v2.1.0"). Framework independent.
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>

namespace dojo
{
struct Version
{
    int major = 0, minor = 0, patch = 0;
    bool valid = false;

    // accepts "1.2.3", "v1.2.3", "1.2" (patch 0); anything after the numbers ("-beta") makes it invalid, so a
    // pre-release is never offered as an update
    static Version parse (std::string_view s) noexcept
    {
        Version v;
        if (! s.empty() && (s.front() == 'v' || s.front() == 'V'))
            s.remove_prefix (1);
        int* parts[3] = { &v.major, &v.minor, &v.patch };
        int n = 0;
        size_t i = 0;
        while (n < 3)
        {
            if (i >= s.size() || s[i] < '0' || s[i] > '9')
                return {};
            long value = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                value = value * 10 + (s[i] - '0');
                if (value > 99999)
                    return {};
                ++i;
            }
            *parts[n++] = (int) value;
            if (i == s.size())
                break;
            if (s[i] != '.')
                return {};
            ++i;
            if (i == s.size())
                return {};
        }
        if (i != s.size() || n < 2)
            return {};
        v.valid = true;
        return v;
    }

    std::string toString() const
    {
        char b[48];
        std::snprintf (b, sizeof (b), "%d.%d.%d", major, minor, patch);
        return b;
    }

    friend bool operator< (const Version& a, const Version& b) noexcept
    {
        return std::tie (a.major, a.minor, a.patch) < std::tie (b.major, b.minor, b.patch);
    }
    friend bool operator== (const Version& a, const Version& b) noexcept
    {
        return a.valid == b.valid && std::tie (a.major, a.minor, a.patch) == std::tie (b.major, b.minor, b.patch);
    }
    friend bool operator!= (const Version& a, const Version& b) noexcept { return ! (a == b); }
};
} // namespace dojo
