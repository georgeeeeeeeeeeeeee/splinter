#pragma once
#include "dsp/Engine.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

namespace fault::test
{
using Signal = std::vector<float>;
constexpr double kSr = 48000.0;

inline Signal sine (size_t n, double hz, float amp = 0.5f)
{
    Signal s (n);
    for (size_t i = 0; i < n; ++i) s[i] = amp * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / kSr);
    return s;
}

inline Signal noise (size_t n, float amp = 0.3f, uint32_t seed = 1)
{
    Signal s (n);
    for (auto& v : s) { seed = seed * 1664525u + 1013904223u; v = amp * ((float) (seed >> 8) / 8388608.0f - 1.0f); }
    return s;
}

struct Rendered
{
    Signal l, r;
    std::vector<dsp::TickEvent> ticks;
};

// Runs the engine like a host: block sizes from `blockFor`, transport at `bpm` (playing), params set per
// block by `perBlock` (optional).
inline Rendered render (const Signal& in, dsp::EngineParams p, std::function<int (size_t)> blockFor, double bpm = 120.0, bool playing = true,
                        std::function<void (dsp::EngineParams&, size_t)> perBlock = {})
{
    dsp::Engine e;
    e.setParams (p);
    e.prepare (kSr, 4096);
    Rendered out { Signal (in.size()), Signal (in.size()), {} };
    uint64_t seen = 0;
    for (size_t pos = 0; pos < in.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) std::max (1, blockFor (pos)), in.size() - pos);
        if (perBlock) { perBlock (p, pos); e.setParams (p); }
        e.setTransport ({ true, playing, bpm, (double) pos / kSr * bpm / 60.0 });
        e.process (in.data() + pos, in.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
        for (; seen < e.ticksWritten(); ++seen) out.ticks.push_back (e.tick (seen));
        pos += (size_t) n;
    }
    return out;
}

inline std::function<int (size_t)> fixedBlocks (int n) { return [n] (size_t) { return n; }; }
inline std::function<int (size_t)> randomBlocks (uint32_t seed = 7)
{
    return [seed] (size_t) mutable { seed = seed * 1664525u + 1013904223u; return 1 + (int) ((seed >> 8) % 1024u); };
}
} // namespace fault::test
