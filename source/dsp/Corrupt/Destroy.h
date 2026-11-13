#pragma once
// Destroy (brief §3.8). First half of the knob: soft saturation, tanh with drive rising to +30 dB.
// Second half: the curve hardens into clipping (drive on to +42 dB) while a sine wavefolder blends in.
// Level safety, both always on:
//   - compensation: a slow RMS match (150 ms; 6 ms for the first 40 ms after it starts) of the output to
//     the input, linked across channels and only ever turning down, so turning it up doesn't get
//     louder;
//   - a zero-latency safety limiter (instant attack, 80 ms release, -1 dBFS ceiling) and a hard clamp
//     at the ceiling, so even line-level input at full drive can't go over.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fault::dsp
{
class Destroy
{
public:
    static constexpr float kCeiling = 0.891f;   // -1 dBFS

    void prepare (double sampleRate) noexcept
    {
        rmsCoef_ = 1.0 - std::exp (-1.0 / (0.15 * sampleRate));
        fastCoef_ = 1.0 - std::exp (-1.0 / (0.006 * sampleRate));
        warmLen_ = (int) (0.04 * sampleRate);
        relCoef_ = std::exp (-1.0 / (0.08 * sampleRate));
    }
    void reset() noexcept
    {
        msIn_ = msOut_ = 0.0;
        comp_ = 1.0;
        env_ = 0.0;
        warm_ = warmLen_;
    }

    // The waveshaper alone, for knob position a (0..1). Identity at 0.
    static float shape (float x, float a) noexcept
    {
        a = std::clamp (a, 0.0f, 1.0f);
        if (a <= 0.5f)
        {
            const float t = a * 2.0f, g = std::pow (10.0f, 1.5f * t);   // 0 .. +30 dB
            const float w = std::min (1.0f, t / 0.15f);                    // from dry over the first bit
            return x + (std::tanh (g * x) - x) * w;
        }
        const float t = (a - 0.5f) * 2.0f, g = std::pow (10.0f, 1.5f + 0.6f * t);   // +30 .. +42 dB
        const float u = g * x;
        const float clipW = std::min (1.0f, t * 3.0f), foldW = std::clamp ((t - 0.15f) / 0.85f, 0.0f, 1.0f);
        const float base = std::tanh (u) + (std::clamp (u, -1.0f, 1.0f) - std::tanh (u)) * clipW;
        const float folded = std::sin (0.5f * (float) std::numbers::pi * x * (1.5f + 6.5f * foldW));
        return base + (folded - base) * foldW;
    }

    void process (float& l, float& r, float amount) noexcept
    {
        const float yl = shape (l, amount), yr = shape (r, amount);
        // Compensation: match the output's RMS to the input's (linked), only ever turning down.
        // Fresh from a reset (while Corrupt fades it in) it settles fast, so it doesn't come in loud.
        const double c = warm_ > 0 ? fastCoef_ : rmsCoef_;
        warm_ -= warm_ > 0 ? 1 : 0;
        msIn_ += (0.5 * ((double) l * l + (double) r * r) - msIn_) * c;
        msOut_ += (0.5 * ((double) yl * yl + (double) yr * yr) - msOut_) * c;
        const double target = std::clamp (std::sqrt ((msIn_ + 1.0e-9) / (msOut_ + 1.0e-9)), 0.03, 1.0);
        comp_ += (target - comp_) * std::min (1.0, c * 4.0);
        float ol = yl * (float) comp_, orr = yr * (float) comp_;
        // Safety limiter: instant attack, so nothing gets past the ceiling, then a clamp for certainty.
        const double peak = std::max (std::abs (ol), std::abs (orr));
        env_ = peak > env_ ? peak : env_ * relCoef_ + peak * (1.0 - relCoef_);
        if (env_ > kCeiling)
        {
            const float g = kCeiling / (float) env_;
            ol *= g;
            orr *= g;
        }
        l = std::clamp (ol, -kCeiling, kCeiling);
        r = std::clamp (orr, -kCeiling, kCeiling);
    }

private:
    int warm_ = 0, warmLen_ = 1920;
    double rmsCoef_ = 1.0e-4, fastCoef_ = 1.0e-2, relCoef_ = 0.999, msIn_ = 0.0, msOut_ = 0.0, comp_ = 1.0, env_ = 0.0;
};
} // namespace fault::dsp
