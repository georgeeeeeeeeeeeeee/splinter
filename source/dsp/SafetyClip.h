#pragma once
// The output safety clipper (Stage 7). Untouched up to -1 dBFS; above that a tanh knee that is
// continuous in slope with the straight line and never passes 0 dBFS. Stateless, zero latency.

#include <cmath>

namespace fault::dsp
{
struct SafetyClip
{
    static constexpr float kKnee = 0.8912509f;          // -1 dBFS
    static constexpr float kCeiling = 1.0f;             // 0 dBFS
    static constexpr float kSpan = kCeiling - kKnee;

    static float apply (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kKnee) return x;
        const float y = kKnee + kSpan * std::tanh ((a - kKnee) / kSpan);
        return x < 0.0f ? -y : y;
    }
    // Clips n samples in place; returns true if any went past the knee.
    static bool process (float* x, int n) noexcept
    {
        bool hit = false;
        for (int i = 0; i < n; ++i)
            if (std::abs (x[i]) > kKnee)
            {
                x[i] = apply (x[i]);
                hit = true;
            }
        return hit;
    }
};
} // namespace fault::dsp
