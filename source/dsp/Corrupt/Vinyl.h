#pragma once
// Vinyl (brief §3.8): the record player's faults, all rising with the knob.
//   - dust and crackle: random impulses (many tiny, a few big) through a 2.5 kHz band-pass, separate
//     per channel, from a few a second to several hundred;
//   - pops: rare, louder, low thumps, the same in both channels;
//   - surface hiss;
//   - an EQ tilt: a low-pass falling from 20 kHz to 3.3 kHz and a high-pass rising to 170 Hz, gentle
//     at first (the curve is a^1.6), a narrow old-gramophone band at full;
//   - wow (0.55 Hz, 33 rpm, speeding to 1.3 Hz) and a touch of flutter (7 Hz): a short modulated delay,
//     at most ~3 ms, up to ±1% pitch.
// Noise goes in before the EQ, as if it all came off the stylus. At 100% it's a hundred-year-old
// turntable.

#include "dsp/Rng.h"
#include "dsp/Svf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace fault::dsp
{
class Vinyl
{
public:
    static constexpr int kRing = 4096;   // ~21 ms at 192 kHz: room for the deepest wow

    void prepare (double sampleRate) noexcept { sr_ = sampleRate; }
    void reseed (uint64_t s) noexcept { rng_.seed (s, 13); }
    void reset() noexcept
    {
        for (auto& ch : ring_) ch.fill (0.0f);
        w_ = 0;
        wowPhase_ = flutterPhase_ = 0.0;
        for (auto& f : crackleBp_) f.reset();
        for (auto& f : lp_) f.reset();
        for (auto& f : hp_) f.reset();
        popLp_.reset();
        crackleLeft_ = { 0.0, 0.0 };
        popLeft_ = 0.0;
        lastA_ = -1.0f;
        popPan_ = 0.5f;
    }

    void process (float& l, float& r, float amount) noexcept
    {
        const double a = std::clamp ((double) amount, 0.0, 1.0);
        if (std::abs (a - lastA_) > 1.0e-4)
            retune (a);

        // Wow and flutter: a delay of 0 .. D samples, D sized for the wanted pitch deviation
        // (deviation = pi f D / sr for d = D/2 (1 - cos)).
        const double wowHz = 0.55 + 0.75 * a;
        const double wowDev = 0.0005 + 0.0095 * a * a, flutterDev = 0.0012 * a;
        const double flutterHz = 7.0;
        const double dWow = wowDev * sr_ / (std::numbers::pi * wowHz), dFl = flutterDev * sr_ / (std::numbers::pi * flutterHz);
        const double delay = 0.5 * dWow * (1.0 - std::cos (wowPhase_)) + 0.5 * dFl * (1.0 - std::cos (flutterPhase_));
        wowPhase_ += 2.0 * std::numbers::pi * wowHz / sr_;
        flutterPhase_ += 2.0 * std::numbers::pi * flutterHz / sr_;
        if (wowPhase_ > 2.0 * std::numbers::pi) wowPhase_ -= 2.0 * std::numbers::pi;
        if (flutterPhase_ > 2.0 * std::numbers::pi) flutterPhase_ -= 2.0 * std::numbers::pi;
        ring_[0][(size_t) w_] = l;
        ring_[1][(size_t) w_] = r;
        const double pos = (double) w_ - std::min (delay, (double) kRing - 4.0);
        w_ = (w_ + 1) & (kRing - 1);
        const auto readAt = [&] (int ch) {
            const double fl = std::floor (pos);
            const float frac = (float) (pos - fl);
            const int i0 = ((int) fl) & (kRing - 1), i1 = (i0 + 1) & (kRing - 1);
            return ring_[(size_t) ch][(size_t) i0] + (ring_[(size_t) ch][(size_t) i1] - ring_[(size_t) ch][(size_t) i0]) * frac;
        };
        std::array<float, 2> x { readAt (0), readAt (1) };

        // Pops: the same thump in both channels, panned a little.
        popLeft_ -= 1.0;
        float popIn = 0.0f;
        if (popLeft_ <= 0.0)
        {
            popLeft_ = nextGap (popRate_);
            popIn = popAmp_ * (float) (0.4 + 0.6 * rng_.uniform()) * (rng_.chance (0.5) ? 1.0f : -1.0f);
            popPan_ = (float) rng_.uniform (0.3, 0.7);
        }
        const float pop = popLp_.tick (popIn).lp;
        const float hiss = hiss_ * (float) (rng_.uniform() - rng_.uniform());
        for (int ch = 0; ch < 2; ++ch)
        {
            crackleLeft_[(size_t) ch] -= 1.0;
            float imp = 0.0f;
            if (crackleLeft_[(size_t) ch] <= 0.0)
            {
                crackleLeft_[(size_t) ch] = nextGap (crackleRate_);
                const double u = rng_.uniform();
                imp = crackleAmp_ * (float) (u * u * u) * (rng_.chance (0.5) ? 1.0f : -1.0f);
            }
            const float crackle = crackleBp_[(size_t) ch].tick (imp).bp;
            const float panned = pop * (ch == 0 ? 1.0f - popPan_ : popPan_) * 2.0f;
            const float in = x[(size_t) ch] + crackle + panned + hiss;
            x[(size_t) ch] = hp_[(size_t) ch].tick (lp_[(size_t) ch].tick (in).lp).hp;
        }
        l = x[0];
        r = x[1];
    }

private:
    void retune (double a) noexcept
    {
        lastA_ = (float) a;
        const double e = std::pow (a, 1.6);
        const double lpHz = 20000.0 * std::pow (3300.0 / 20000.0, e), hpHz = 12.0 * std::pow (170.0 / 12.0, e);
        lp_[0].set (lpHz, 0.75, sr_);
        hp_[0].set (hpHz, 0.6, sr_);
        lp_[1].copyCoefficients (lp_[0]);
        hp_[1].copyCoefficients (hp_[0]);
        crackleBp_[0].set (2500.0, 0.9, sr_);
        crackleBp_[1].copyCoefficients (crackleBp_[0]);
        popLp_.set (420.0, 0.8, sr_);
        crackleRate_ = 3.0 * std::pow (200.0, a);   // 3 .. 600 a second
        crackleAmp_ = (float) (0.15 + 0.85 * a);
        popRate_ = 0.04 + 1.3 * a * a;
        popAmp_ = (float) (0.6 + 1.4 * a);
        hiss_ = (float) (0.0008 + 0.006 * a * a);
    }
    double nextGap (double ratePerSecond) noexcept
    {
        return std::max (1.0, -std::log (std::max (1.0e-9, 1.0 - rng_.uniform())) / ratePerSecond * sr_);
    }

    double sr_ = 48000.0;
    std::array<std::array<float, kRing>, 2> ring_ {};
    int w_ = 0;
    double wowPhase_ = 0.0, flutterPhase_ = 0.0;
    Svf crackleBp_[2], lp_[2], hp_[2], popLp_;
    std::array<double, 2> crackleLeft_ {};
    double popLeft_ = 0.0, crackleRate_ = 3.0, popRate_ = 0.04;
    float crackleAmp_ = 0.15f, popAmp_ = 0.6f, hiss_ = 0.001f, lastA_ = -1.0f, popPan_ = 0.5f;
    Pcg32 rng_;
};
} // namespace fault::dsp
