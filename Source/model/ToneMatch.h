#pragma once
// Axell Signature Dojo - Reference Match: the tonal difference between a take and a reference vocal, as a handful of
// Penta EQ bells (docs/DESIGN.md, D16). Framework independent.
//
// Both voices are compared by their voiced long-term spectra (VoiceAnalysis' 1/3-octave curve), each normalised to its
// own 500 Hz-4 kHz level, so only the *shape* is matched, never the loudness. The difference (reference minus take)
// is smoothed to 1/3 octave, limited to +-`maxDb`, scaled by `amount`, and fitted greedily with peaking bells: the
// largest remaining deviation gets a bell whose Q follows the deviation's width at half height, the bell's own curve
// (RBJ peaking, evaluated analytically) is subtracted, and so on, up to `maxBands`. The plan is then rendered through
// the installed Penta EQ and the remaining difference is measured (ProfileEngine), so the claim is checked, not assumed.
#include "VoiceAnalysis.h"

#include <cmath>
#include <vector>

namespace dojo::tone
{
struct Bell
{
    float hz = 1000.0f, gainDb = 0.0f, q = 1.0f;
};

struct Match
{
    std::vector<float> hz, targetDb;   // the difference to remove (reference - take, shaped), on the take's grid
    std::vector<Bell> bells;
    float residualRmsDb = 0.0f;        // what the bells leave of the target, 120 Hz-10 kHz
    float targetRmsDb = 0.0f;
};

namespace detail
{
// the magnitude (dB) of an RBJ peaking bell at f, for a sample rate far above f (analog shape)
inline double bellDb (double f, const Bell& b)
{
    const double w = f / b.hz, g = std::pow (10.0, b.gainDb / 40.0);   // A = sqrt (linear gain)
    const double q2 = 1.0 / ((double) b.q * b.q);
    const double num = std::pow (1.0 - w * w, 2.0) + w * w * q2 * g * g;
    const double den = std::pow (1.0 - w * w, 2.0) + w * w * q2 / (g * g);
    return 10.0 * std::log10 (num / den);
}

inline float bandMean (const voice::Curve& c, float lo, float hi)
{
    double s = 0.0;
    int n = 0;
    for (size_t i = 0; i < c.hz.size(); ++i)
        if (c.hz[i] >= lo && c.hz[i] <= hi && std::isfinite (c.db[i]))
        {
            s += c.db[i];
            ++n;
        }
    return n > 0 ? (float) (s / n) : 0.0f;
}
} // namespace detail

inline float residualRms (const std::vector<float>& hz, const std::vector<float>& target, const std::vector<Bell>& bells)
{
    double s = 0.0;
    int n = 0;
    for (size_t i = 0; i < hz.size(); ++i)
    {
        if (hz[i] < 120.0f || hz[i] > 10000.0f)
            continue;
        double e = target[i];
        for (const auto& b : bells)
            e -= detail::bellDb (hz[i], b);
        s += e * e;
        ++n;
    }
    return n > 0 ? (float) std::sqrt (s / n) : 0.0f;
}

// take and reference: the voiced long-term spectra from voice::analyse (same grid: same sample-rate family not needed,
// the grids are 40 Hz-16 kHz at 1/12 octave either way)
inline Match match (const voice::Curve& take, const voice::Curve& reference, float amount = 1.0f, int maxBands = 6, float maxDb = 6.0f)
{
    Match m;
    if (take.hz.empty() || reference.hz.empty())
        return m;
    const float tRef = detail::bandMean (take, 500.0f, 4000.0f), rRef = detail::bandMean (reference, 500.0f, 4000.0f);
    // raw difference on the take's grid (the reference interpolated in log frequency)
    std::vector<float> raw;
    for (size_t i = 0; i < take.hz.size(); ++i)
    {
        const float f = take.hz[i];
        size_t k = 0;
        while (k + 1 < reference.hz.size() && reference.hz[k + 1] < f)
            ++k;
        float r = reference.db[k];
        if (k + 1 < reference.hz.size() && reference.hz[k + 1] > reference.hz[k])
        {
            const float t = std::clamp ((float) (std::log (f / reference.hz[k]) / std::log (reference.hz[k + 1] / reference.hz[k])), 0.0f, 1.0f);
            r = reference.db[k] + t * (reference.db[k + 1] - reference.db[k]);
        }
        raw.push_back ((r - rRef) - (take.db[i] - tRef));
    }
    // 1/3-octave smoothing (+-2 points on the 1/12-octave grid), range limit, amount; nothing below 80 Hz or above
    // 12 kHz is matched (rumble and air are not tone)
    for (size_t i = 0; i < raw.size(); ++i)
    {
        double s = 0.0;
        int n = 0;
        for (size_t j = (i >= 2 ? i - 2 : 0); j <= std::min (raw.size() - 1, i + 2); ++j, ++n)
            s += raw[j];
        float v = (float) (s / n);
        const float f = take.hz[i];
        if (f < 80.0f || f > 12000.0f)
            v = 0.0f;
        m.hz.push_back (f);
        m.targetDb.push_back (std::clamp (v, -maxDb, maxDb) * amount);
    }
    m.targetRmsDb = residualRms (m.hz, m.targetDb, {});
    // greedy bells
    std::vector<float> rest = m.targetDb;
    for (int b = 0; b < maxBands; ++b)
    {
        int best = -1;
        for (size_t i = 0; i < rest.size(); ++i)
            if (m.hz[i] >= 100.0f && m.hz[i] <= 10000.0f && (best < 0 || std::abs (rest[i]) > std::abs (rest[(size_t) best])))
                best = (int) i;
        if (best < 0 || std::abs (rest[(size_t) best]) < 0.75f)
            break;   // what is left is below what anyone hears as tone
        const float peak = rest[(size_t) best];
        int a = best, z = best;
        while (a > 0 && std::abs (rest[(size_t) a - 1]) > 0.5f * std::abs (peak) && (rest[(size_t) a - 1] > 0) == (peak > 0))
            --a;
        while (z + 1 < (int) rest.size() && std::abs (rest[(size_t) z + 1]) > 0.5f * std::abs (peak) && (rest[(size_t) z + 1] > 0) == (peak > 0))
            ++z;
        const double bwOct = std::max (1.0 / 6.0, std::log2 ((double) m.hz[(size_t) z] / m.hz[(size_t) a]) + 1.0 / 12.0);
        const double q = std::sqrt (std::pow (2.0, bwOct)) / (std::pow (2.0, bwOct) - 1.0);
        Bell bell { m.hz[(size_t) best], peak, (float) std::clamp (q, 0.4, 4.0) };
        m.bells.push_back (bell);
        for (size_t i = 0; i < rest.size(); ++i)
            rest[i] -= (float) detail::bellDb (m.hz[i], bell);
    }
    m.residualRmsDb = residualRms (m.hz, m.targetDb, m.bells);
    return m;
}
} // namespace dojo::tone
