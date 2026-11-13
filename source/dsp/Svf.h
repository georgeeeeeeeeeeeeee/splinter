#pragma once
// A state-variable filter in the topology-preserving (trapezoidal) form: low, band and high outputs
// from one structure, unconditionally stable and free of zipper noise when the cutoff moves every
// sample. Used by DJ Filter and Vinyl.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fault::dsp
{
struct Svf
{
    struct Out { float lp, bp, hp; };

    void reset() noexcept { ic1 = ic2 = 0.0; }
    void set (double hz, double q, double sampleRate) noexcept
    {
        const double fc = std::clamp (hz, 5.0, 0.45 * sampleRate);
        g = std::tan (std::numbers::pi * fc / sampleRate);
        k = 1.0 / std::max (0.1, q);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void copyCoefficients (const Svf& o) noexcept { g = o.g; k = o.k; a1 = o.a1; a2 = o.a2; a3 = o.a3; }
    Out tick (float x) noexcept
    {
        const double v0 = x, v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { (float) v2, (float) v1, (float) (v0 - k * v1 - v2) };
    }

    double g = 0.0, k = 1.4, a1 = 1.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;
};
} // namespace fault::dsp
