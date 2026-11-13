#pragma once
// Decimate (brief §3.8): bit depth + sample-rate reduction, from a fixed table of 12 presets. The knob
// steps through them in a deliberately shuffled order (bits and rate don't move together), trending from
// a faint hiss to blown out. One instance plays one preset; Corrupt crossfades two instances when the
// knob moves to the next preset, so stepping never clicks.
// The aliasing of the sample-and-hold is the sound: there's no anti-alias filter on purpose.

#include "dsp/Rng.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fault::dsp
{
struct DecimatePreset
{
    const char* name;   // for the UI
    float bits;      // quantiser resolution (fractional is fine)
    float rateHz;    // sample-and-hold rate, in Hz (the same at any host rate)
    float driveDb;   // into the quantiser (clipped at full scale), compensated after
    float jitter;    // 0..1: random wobble of the hold length (an unstable clock)
    float noise;     // dither / hiss before the quantiser, as a fraction of full scale
};

// In knob order. Shuffled on purpose: a resolution step, then a rate step, then both.
inline constexpr std::array<DecimatePreset, 12> kDecimatePresets { {
    { "faint hiss", 12.0f, 48000.0f, 0.0f, 0.00f, 0.0015f },
    { "grainy tails", 10.0f, 44100.0f, 0.0f, 0.00f, 0.0f },
    { "early sampler", 14.0f, 11025.0f, 0.0f, 0.00f, 0.0f },
    { "8-bit", 8.0f, 22050.0f, 0.0f, 0.00f, 0.0005f },
    { "muffled, unsteady", 12.0f, 6000.0f, 0.0f, 0.15f, 0.0f },
    { "crunchy", 6.0f, 16000.0f, 3.0f, 0.00f, 0.0f },
    { "telephone ghost", 9.0f, 4000.0f, 0.0f, 0.05f, 0.0f },
    { "broken toy", 5.0f, 8000.0f, 4.0f, 0.30f, 0.002f },
    { "metallic", 7.0f, 2500.0f, 3.0f, 0.00f, 0.0f },
    { "fizz", 4.0f, 12000.0f, 6.0f, 0.10f, 0.0f },
    { "gravel", 3.0f, 3000.0f, 8.0f, 0.50f, 0.0f },
    { "blown out", 2.0f, 1500.0f, 12.0f, 0.60f, 0.004f },
} };

class Decimate
{
public:
    void prepare (double sampleRate) noexcept { sr_ = sampleRate; }
    void reseed (uint64_t s) noexcept { rng_.seed (s, 11); }
    void reset (int preset) noexcept
    {
        p_ = kDecimatePresets[(size_t) std::clamp (preset, 0, (int) kDecimatePresets.size() - 1)];
        steps_ = std::exp2 (p_.bits - 1.0f);
        drive_ = std::pow (10.0f, p_.driveDb / 20.0f);
        makeup_ = std::pow (drive_, -0.75f);   // driven presets end up a little hotter, not much
        phase_ = 1.0;   // take a sample at once
        inc_ = nextIncrement();
        held_ = { 0.0f, 0.0f };
    }

    void process (float& l, float& r) noexcept
    {
        phase_ += inc_;
        if (phase_ >= 1.0)
        {
            phase_ -= std::floor (phase_);
            inc_ = nextIncrement();
            held_[0] = quantise (l);
            held_[1] = quantise (r);
        }
        l = held_[0] * makeup_;
        r = held_[1] * makeup_;
    }

private:
    double nextIncrement() noexcept
    {
        const double base = std::min (1.0, (double) p_.rateHz / sr_);
        return p_.jitter > 0.0f ? base * (1.0 + (double) p_.jitter * (rng_.uniform() - 0.5)) : base;
    }
    float quantise (float x) noexcept
    {
        float v = std::clamp (x * drive_, -1.0f, 1.0f);
        if (p_.noise > 0.0f)
            v += p_.noise * (float) (rng_.uniform() - rng_.uniform());   // TPDF
        return std::clamp (std::round (v * steps_) / steps_, -1.0f, 1.0f);
    }

    double sr_ = 48000.0, phase_ = 1.0, inc_ = 1.0;
    DecimatePreset p_ = kDecimatePresets[0];
    float steps_ = 2048.0f, drive_ = 1.0f, makeup_ = 1.0f;
    std::array<float, 2> held_ {};
    Pcg32 rng_;
};
} // namespace fault::dsp
