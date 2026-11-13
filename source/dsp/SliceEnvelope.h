#pragma once
// Glitch windowing (brief §3.5): a raised-cosine fade at both ends of every slice, each fade
// windowing × half the slice. 0 = no envelope (hard, clicky edges, intended); 100% = each slice a full
// raised-cosine window. Measured in playback time (progress since the slice started), so it follows
// the slice whatever the speed or direction.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fault::dsp
{
struct SliceEnvelope
{
    static float rise (double x) noexcept { return x >= 1.0 ? 1.0f : x <= 0.0 ? 0.0f : (float) (0.5 - 0.5 * std::cos (std::numbers::pi * x)); }

    // Fades are at least kMinFadeSamples (1 ms at 48 kHz; or half the slice, if shorter) whenever
    // windowing is on: on short slices 2% alone is a few samples, which is a click (SPEC_NOTES N4).
    static constexpr double kMinFadeSeconds = 0.001;
    static double fadeSamples (double duration, float windowing, double sampleRate) noexcept
    {
        if (windowing <= 0.0f || duration <= 0.0)
            return 0.0;
        return std::min (0.5 * duration, std::max (0.5 * (double) windowing * duration, kMinFadeSeconds * sampleRate));
    }
    // progress and duration in samples of playback; windowing 0..1.
    static float gain (double progress, double duration, float windowing, double sampleRate) noexcept
    {
        const double fade = fadeSamples (duration, windowing, sampleRate);
        if (fade < 1.0)
            return 1.0f;
        return std::min (rise (progress / fade), rise ((duration - progress) / fade));
    }
};
} // namespace fault::dsp
