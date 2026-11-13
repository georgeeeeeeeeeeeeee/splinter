#pragma once
// Seeded PCG32 (O'Neill's pcg32: XSH-RR output on a 64-bit LCG), one per decision stream (brief §6).
// Plain integer maths: identical results on every compiler and CPU, so a fixed seed renders bit-identical.

#include <cstdint>

namespace fault::dsp
{
class Pcg32
{
public:
    Pcg32() noexcept { seed (0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL); }
    Pcg32 (uint64_t state, uint64_t stream) noexcept { seed (state, stream); }

    void seed (uint64_t state, uint64_t stream) noexcept
    {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += state;
        next();
    }

    uint32_t next() noexcept
    {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const auto rot = (uint32_t) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // [0, 1), 32 bits of resolution.
    double uniform() noexcept { return (double) next() * (1.0 / 4294967296.0); }
    double uniform (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }
    bool chance (double p) noexcept { return uniform() < p; }
    // [0, n), unbiased.
    int below (int n) noexcept
    {
        if (n <= 1) return 0;
        const auto bound = (uint32_t) n, threshold = (uint32_t) (-bound) % bound;
        for (;;)
            if (const uint32_t r = next(); r >= threshold)
                return (int) (r % bound);
    }

private:
    uint64_t state_ = 0, inc_ = 1;
};

// SplitMix64 finaliser: turns (seed, tick index) into a well-mixed PCG state.
inline uint64_t mix64 (uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
} // namespace fault::dsp
