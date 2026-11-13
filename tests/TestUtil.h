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
    std::vector<float> speeds;   // engine speed after each block
};

struct Note { double time; int note; bool on; };

// Runs the engine like a host: block sizes from `blockFor`, transport at `bpm` (playing), params set per
// block by `perBlock` (optional).
inline Rendered render (const Signal& in, dsp::EngineParams p, std::function<int (size_t)> blockFor, double bpm = 120.0, bool playing = true,
                        std::function<void (dsp::EngineParams&, size_t)> perBlock = {}, std::vector<Note> notes = {})
{
    size_t nextNote = 0;
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
        while (nextNote < notes.size() && (size_t) std::llround (notes[nextNote].time * kSr) < pos + (size_t) n)
        {
            const auto at = (size_t) std::llround (notes[nextNote].time * kSr);
            e.queueNote ({ (int) (at > pos ? at - pos : 0), notes[nextNote].note, 100, notes[nextNote].on });
            ++nextNote;
        }
        e.process (in.data() + pos, in.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
        for (; seen < e.ticksWritten(); ++seen) out.ticks.push_back (e.tick (seen));
        out.speeds.push_back (e.status().speed.load());
        pos += (size_t) n;
    }
    return out;
}

// Strongest frequency near hz (±6%), by a fine Hann-windowed DFT search (golden-section refine).
inline double peakHz (const Signal& x, double t0, double dur, double hz)
{
    const auto i0 = (size_t) (t0 * kSr), n = (size_t) (dur * kSr);
    const auto power = [&] (double f) {
        const double w = 2.0 * std::numbers::pi * f / kSr;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n);
            re += x[i0 + i] * win * std::cos (w * (double) i);
            im -= x[i0 + i] * win * std::sin (w * (double) i);
        }
        return re * re + im * im;
    };
    double best = hz, bp = -1;
    for (double f = hz * 0.94; f <= hz * 1.06; f += hz * 0.002)
        if (const double pw = power (f); pw > bp) { bp = pw; best = f; }
    double a = best - hz * 0.002, b = best + hz * 0.002;
    for (int it = 0; it < 40; ++it)
    {
        const double m1 = a + (b - a) * 0.382, m2 = a + (b - a) * 0.618;
        if (power (m1) > power (m2)) b = m2; else a = m1;
    }
    return 0.5 * (a + b);
}

inline std::function<int (size_t)> fixedBlocks (int n) { return [n] (size_t) { return n; }; }
inline std::function<int (size_t)> randomBlocks (uint32_t seed = 7)
{
    return [seed] (size_t) mutable { seed = seed * 1664525u + 1013904223u; return 1 + (int) ((seed >> 8) % 1024u); };
}
} // namespace fault::test
