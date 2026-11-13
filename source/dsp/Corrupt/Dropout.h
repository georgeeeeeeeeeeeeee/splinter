#pragma once
// Dropout (brief §3.8): random mutes, like a worn tape losing contact. Left of the knob: few, long
// dropouts (about one every 4 s, up to a second long). Right: many short ones (16 a second, 12 ms).
// Both channels drop together. Every mute fades in and out over 3 ms, so moving the knob only changes
// what the next dropout looks like and never clicks.

#include "dsp/Rng.h"
#include "dsp/SliceEnvelope.h"

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
class Dropout
{
public:
    static constexpr double kFadeSeconds = 0.003;

    void prepare (double sampleRate) noexcept { sr_ = sampleRate; }
    void reseed (uint64_t s) noexcept { rng_.seed (s, 12); }
    void reset() noexcept
    {
        muted_ = false;
        left_ = 0.0;   // pick the first gap on the first sample
        ramp_ = 1.0;
        started_ = false;
    }

    // Events per second and their mean length, for knob position a (0..1).
    static double ratePerSecond (double a) noexcept { return 0.25 * std::pow (64.0, std::clamp (a, 0.0, 1.0)); }
    static double meanLengthSeconds (double a) noexcept { return 1.0 * std::pow (0.012, std::clamp (a, 0.0, 1.0)); }

    void process (float& l, float& r, float amount) noexcept
    {
        if (left_ <= 0.0)
            next ((double) amount);
        left_ -= 1.0;
        const double step = 1.0 / std::max (1.0, kFadeSeconds * sr_);
        ramp_ = muted_ ? std::max (0.0, ramp_ - step) : std::min (1.0, ramp_ + step);
        const float g = SliceEnvelope::rise (ramp_);
        l *= g;
        r *= g;
    }

    bool muted() const noexcept { return muted_; }

private:
    void next (double a) noexcept
    {
        const double len = meanLengthSeconds (a);
        if (muted_ || ! started_)
        {
            // A gap: exponential, so dropouts arrive like a Poisson process, at least one fade long.
            const double mean = std::max (0.5 * len, 1.0 / ratePerSecond (a) - len);
            const double gap = -mean * std::log (std::max (1.0e-9, 1.0 - rng_.uniform()));
            left_ = std::max (2.0 * kFadeSeconds, gap) * sr_;
            muted_ = false;
            started_ = true;
        }
        else
        {
            left_ = std::max (2.0 * kFadeSeconds, len * rng_.uniform (0.4, 1.6)) * sr_;
            muted_ = true;
        }
    }

    double sr_ = 48000.0, left_ = 0.0, ramp_ = 1.0;
    bool muted_ = false, started_ = false;
    Pcg32 rng_;
};
} // namespace fault::dsp
