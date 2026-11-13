#pragma once
// DJ Filter (brief §3.8): one moderately resonant state-variable filter (TPT form, stable under fast
// modulation). The knob's centre is bypass (a small dead zone, no CPU); left of it sweeps a low-pass
// from 20 kHz down to 50 Hz, right of it a high-pass from 20 Hz up to 8 kHz, exponentially, with the
// resonance rising slightly towards the ends. Just outside the dead zone it blends in from dry, so
// leaving the centre is seamless.

#include "dsp/Svf.h"

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
class DjFilter
{
public:
    static constexpr float kDeadZone = 0.02f;   // either side of the centre

    static bool bypassed (float knob) noexcept { return std::abs (knob - 0.5f) < kDeadZone; }

    void prepare (double sampleRate) noexcept { sr_ = sampleRate; }
    void reset() noexcept { for (auto& f : svf_) f.reset(); }

    void process (float& l, float& r, float knob) noexcept
    {
        const float d = knob - 0.5f;
        if (std::abs (d) < kDeadZone)
            return;
        const bool lowPass = d < 0.0f;
        const double t = std::clamp ((std::abs (d) - kDeadZone) / (0.5 - kDeadZone), 0.0, 1.0);
        const double hz = lowPass ? 20000.0 * std::pow (50.0 / 20000.0, t) : 20.0 * std::pow (8000.0 / 20.0, t);
        const double q = 0.9 + 0.8 * t;
        svf_[0].set (hz, q, sr_);
        svf_[1].copyCoefficients (svf_[0]);
        const float w = (float) std::min (1.0, t / 0.08);
        const auto run = [&] (Svf& f, float x) {
            const auto o = f.tick (x);
            return x + ((lowPass ? o.lp : o.hp) - x) * w;
        };
        l = run (svf_[0], l);
        r = run (svf_[1], r);
    }

private:
    double sr_ = 48000.0;
    Svf svf_[2];
};
} // namespace fault::dsp
