// Stage 3: Macro mode. Per-tick dice for Bend and Break, Unique / Shared stereo, MacroTables, seeds.
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace fault;
using namespace fault::test;

namespace
{
dsp::EngineParams macro (float bend, float brk, int step = 3, int repeats = 4)
{
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = (float) step / 8.0f;   // step 3 = /2: a tick every 2 beats (1 s at 120 BPM)
    p.repeats = repeats;
    p.mix = 1.0f;
    p.mode = dsp::Mode::macro;
    p.bend = bend;
    p.brk = brk;
    p.bendOnMacro = p.breakOnMacro = true;
    return p;
}

// Tables where every zone fires every tick and only `option` can be picked.
dsp::MacroTables forcedBend (int option)
{
    auto t = dsp::MacroTables::defaults();
    t.bendChanceLow.fill (1.0f);
    t.bendChanceHigh.fill (1.0f);
    t.bendWeights.fill (0.0f);
    for (int z = 0; z < dsp::kZones; ++z) t.bendWeights[(size_t) (z * dsp::kNumBendOptions + option)] = 1.0f;
    t.bendAlsoReverse.fill (0.0f);
    t.bendRateExponent = 0.0f;
    return t;
}

Rendered renderWith (const Signal& in, dsp::EngineParams p, const dsp::MacroTables& t, std::function<int (size_t)> blocks, double startPpq = 0.0,
                     std::function<void (dsp::EngineParams&, size_t)> perBlock = {}, uint64_t sessionSeed = 0x5eed)
{
    dsp::Engine e;
    e.setTables (t);
    e.setSessionSeed (sessionSeed);
    e.setParams (p);
    e.prepare (kSr, 4096);
    Rendered out { Signal (in.size()), Signal (in.size()), {} };
    uint64_t seen = 0;
    for (size_t pos = 0; pos < in.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) std::max (1, blocks (pos)), in.size() - pos);
        if (perBlock) { perBlock (p, pos); e.setParams (p); }
        e.setTransport ({ true, true, 120.0, startPpq + (double) pos / kSr * 2.0 });
        e.process (in.data() + pos, in.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
        for (; seen < e.ticksWritten(); ++seen) out.ticks.push_back (e.tick (seen));
        out.speeds.push_back (e.status().speed.load());
        pos += (size_t) n;
    }
    return out;
}

bool allFinite (const Rendered& r, float& peak)
{
    peak = 0.0f;
    for (const auto* s : { &r.l, &r.r })
        for (float v : *s)
        {
            if (! std::isfinite (v)) return false;
            peak = std::max (peak, std::abs (v));
        }
    return true;
}
} // namespace

TEST_CASE ("The same seed gives bit-identical output, at any block size", "[stage3][seed]")
{
    const Signal in = noise ((size_t) (24.0 * kSr));
    for (int seed : { 0, 1, 777 })
    {
        auto p = macro (0.95f, 0.95f, 5, 4);   // everything zones, x2 (4 ticks a second)
        p.seed = seed;
        const auto t = dsp::MacroTables::defaults();
        const auto a = renderWith (in, p, t, fixedBlocks (512));
        const auto b = renderWith (in, p, t, randomBlocks (3));
        const auto c = renderWith (in, p, t, fixedBlocks (1));
        INFO ("seed " << seed);
        CHECK (a.l == b.l);
        CHECK (a.r == b.r);
        CHECK (a.l == c.l);
        CHECK (a.r == c.r);
        float peak = 0.0f;
        CHECK (allFinite (a, peak));
        CHECK (peak < 4.0f);
        // And the dice really did something.
        int events = 0;
        for (const auto& k : a.ticks) events += (k.bendMask[0] | k.breakMask[0]) != 0;
        CHECK (events > (int) a.ticks.size() / 2);
    }
}

TEST_CASE ("Different seeds give different output", "[stage3][seed]")
{
    const Signal in = noise ((size_t) (12.0 * kSr));
    auto p = macro (0.95f, 0.95f, 5);
    const auto t = dsp::MacroTables::defaults();
    p.seed = 1;
    const auto a = renderWith (in, p, t, fixedBlocks (512));
    p.seed = 2;
    const auto b = renderWith (in, p, t, fixedBlocks (512));
    CHECK (a.l != b.l);
    p.seed = 0;   // the session seed decides
    const auto c = renderWith (in, p, t, fixedBlocks (512), 0.0, {}, 11);
    const auto d = renderWith (in, p, t, fixedBlocks (512), 0.0, {}, 12);
    CHECK (c.l != d.l);
}

TEST_CASE ("A fixed seed keys the dice on the host bar position", "[stage3][seed]")
{
    // Start one render 8 beats later: every tick at the same grid position decides the same.
    const Signal in = noise ((size_t) (20.0 * kSr));
    auto p = macro (0.9f, 0.9f, 5);
    p.seed = 42;
    const auto t = dsp::MacroTables::defaults();
    const auto a = renderWith (in, p, t, fixedBlocks (256));
    const auto b = renderWith (in, p, t, randomBlocks(), 8.0);
    int compared = 0;
    for (const auto& x : a.ticks)
        for (const auto& y : b.ticks)
            if (x.index == y.index && x.windowLength == y.windowLength)   // (b's first window is still short)
            {
                ++compared;
                CHECK (x.bendMask == y.bendMask);
                CHECK (x.breakMask == y.breakMask);
                CHECK (x.pieces == y.pieces);
                CHECK (x.slice == y.slice);
                CHECK (x.octaves == y.octaves);
            }
    CHECK (compared > 40);
}

TEST_CASE ("Shared stereo rolls once for both channels; Unique rolls per channel", "[stage3][stereo]")
{
    const Signal in = noise ((size_t) (12.0 * kSr));   // mono in: L and R identical going in
    auto p = macro (0.9f, 0.9f, 5);
    p.seed = 5;
    const auto t = dsp::MacroTables::defaults();
    p.stereo = dsp::StereoMode::shared;
    const auto s = renderWith (in, p, t, randomBlocks());
    CHECK (s.l == s.r);
    p.stereo = dsp::StereoMode::unique;
    const auto u = renderWith (in, p, t, randomBlocks());
    CHECK (u.l != u.r);
    int differ = 0;
    for (const auto& k : u.ticks) differ += k.bendMask[0] != k.bendMask[1] || k.breakMask[0] != k.breakMask[1] || k.slice[0] != k.slice[1];
    CHECK (differ > (int) u.ticks.size() / 4);
}

TEST_CASE ("Bend and Break off (toggle or zone 0) play exactly like plain Repeats", "[stage3][off]")
{
    const Signal in = noise ((size_t) (8.0 * kSr));
    auto plain = macro (0.5f, 0.5f);
    plain.bendOnMacro = plain.breakOnMacro = false;
    const auto ref = render (in, plain, fixedBlocks (512));
    const auto t = dsp::MacroTables::defaults();
    auto zone0 = macro (0.01f, 0.01f);   // both toggles on, knobs in the Off zone
    const auto a = renderWith (in, zone0, t, fixedBlocks (512));
    CHECK (a.l == ref.l);
    CHECK (a.r == ref.r);
    auto toggledOff = macro (0.95f, 0.95f);
    toggledOff.bendOnMacro = toggledOff.breakOnMacro = false;
    const auto b = renderWith (in, toggledOff, t, fixedBlocks (512));
    CHECK (b.l == ref.l);
}

TEST_CASE ("Zones are cumulative and position in a zone raises the chance", "[stage3][zones]")
{
    const auto t = dsp::MacroTables::defaults();
    const dsp::TickContext ctx { kSr, kSr, kSr / 4.0, 4, true };   // 1 s ticks, Repeats 4
    for (int z = 1; z < dsp::kZones; ++z)
    {
        const float lo = t.bendZoneStart[(size_t) z], hi = z + 1 < dsp::kZones ? t.bendZoneStart[(size_t) z + 1] : 1.0f;
        for (float pos : { 0.05f, 0.95f })
        {
            dsp::Pcg32 rng (99, (uint64_t) z);
            int events = 0;
            std::set<int> seen;
            const int N = 20000;
            for (int i = 0; i < N; ++i)
                if (const auto d = dsp::decideBend (t, lo + pos * (hi - lo), rng, ctx); d.option >= 0)
                {
                    ++events;
                    seen.insert (d.option);
                }
            const double expected = t.bendChanceLow[(size_t) z] + (t.bendChanceHigh[(size_t) z] - t.bendChanceLow[(size_t) z]) * pos;
            INFO ("bend zone " << z << " pos " << pos);
            CHECK (std::abs ((double) events / N - expected) < 0.02);
            CHECK (*seen.rbegin() <= std::min (z - 1, dsp::kNumBendOptions - 1));   // nothing from later zones
            if (z < dsp::kZones - 1) CHECK (seen.count (z - 1) == 1);               // the zone's own option shows up
        }
    }
    // Faster ticks: fewer events (period 12.5 ms, ref 0.25 s, exponent 0.5 -> x0.224).
    CHECK (std::abs (dsp::rateScale (0.0125, 0.25f, 0.5f) - std::sqrt (0.05)) < 1.0e-9);
}

TEST_CASE ("Break options respect Time, Repeats and their ranges", "[stage3][break]")
{
    auto t = dsp::MacroTables::defaults();
    t.breakChanceLow.fill (1.0f);
    t.breakChanceHigh.fill (1.0f);
    t.breakRateExponent = 0.0f;
    dsp::Pcg32 rng (3, 3);
    const double minSlice = t.breakMinSliceMs * 0.001 * kSr;
    for (int repeats : { 1, 2, 8, 32 })
        for (double period : { 0.05 * kSr, 0.5 * kSr, 4.0 * kSr })
            for (int i = 0; i < 3000; ++i)
            {
                const dsp::TickContext ctx { kSr, period, period / repeats, repeats, (i & 1) != 0 };
                const auto d = dsp::decideBreak (t, 0.99f, rng, ctx);   // everything: all options, stacking
                INFO ("repeats " << repeats << " period " << period / kSr << " s, pieces " << d.pieces << " mask " << d.mask);
                REQUIRE (d.slice >= 0);
                REQUIRE (d.slice < repeats * d.pieces);
                if (repeats == 1 && d.pieces == 1) REQUIRE ((d.mask & (1u << dsp::breakJump)) == 0u);   // nothing to jump to
                const double piece = ctx.sliceLength / d.pieces;
                if (d.mask & (1u << dsp::breakAudioRate))
                {
                    const double hz = kSr / piece;   // rounding to whole pieces moves the rate a little
                    REQUIRE (hz >= t.audioRateHzMin * 0.66);
                    REQUIRE (hz <= t.audioRateHzMax * 1.5);
                }
                else if (d.pieces > 1)
                    REQUIRE (piece >= minSlice - 1e-9);
                if (d.mask & (1u << dsp::breakSilence))
                {
                    REQUIRE (d.duty >= t.silenceMin - 1e-9);
                    REQUIRE (d.duty <= 0.9 + 1e-9);
                }
            }
}

TEST_CASE ("Tape stop winds down to silence, not DC, and restarts on the next tick", "[stage3][tapestop]")
{
    const Signal in = sine ((size_t) (10.0 * kSr), 220.0, 0.5f);
    auto p = macro (0.75f, 0.0f, 3, 1);   // a tick every second, Repeats 1
    p.breakOnMacro = false;
    p.seed = 9;
    const auto r = renderWith (in, p, forcedBend (dsp::bendTapeStop), fixedBlocks (480));
    REQUIRE (r.ticks.size() >= 8);
    const auto t = dsp::MacroTables::defaults();
    for (size_t k = 1; k + 1 < r.ticks.size(); ++k)
    {
        const auto start = (size_t) r.ticks[k].sample, end = (size_t) r.ticks[k + 1].sample;
        // The stop completes within tapeStopMax of the period; after that the output is silent.
        const auto quiet = start + (size_t) ((double) (end - start) * (t.tapeStopMax + 0.02));
        float tailPeak = 0.0f, headPeak = 0.0f;
        for (size_t i = quiet; i < end; ++i) tailPeak = std::max (tailPeak, std::abs (r.l[i]));
        for (size_t i = start + 2000; i < start + 6000; ++i) headPeak = std::max (headPeak, std::abs (r.l[i]));
        INFO ("tick " << k);
        CHECK (tailPeak < 1.0e-4f);
        CHECK (headPeak > 0.2f);   // and it starts again at speed
        CHECK (r.ticks[k].bendMask[0] == (1u << dsp::bendTapeStop));
    }
}

TEST_CASE ("Octave events play at the decided speed; switching to Micro doesn't leak them", "[stage3][bend]")
{
    const Signal in = noise ((size_t) (6.0 * kSr));
    auto p = macro (0.3f, 0.0f, 3, 1);
    p.breakOnMacro = false;
    p.seed = 4;
    const auto r = renderWith (in, p, forcedBend (dsp::bendTwoOctaves), fixedBlocks (480), 0.0, [] (dsp::EngineParams& q, size_t pos) {
        if (pos >= (size_t) (3.5 * kSr)) { q.mode = dsp::Mode::micro; q.bend = 0.5f; q.bendOnMicro = false; }
    });
    bool sawTwoOct = false;
    for (const auto& k : r.ticks)
        if ((size_t) k.sample < (size_t) (3.5 * kSr) && k.bendMask[0] != 0)   // (the first tick has no window yet)
        {
            CHECK (std::abs (std::abs (k.speed[0]) - (k.octaves[0] > 0 ? 4.0f : 0.25f)) < 1.0e-6f);
            sawTwoOct = true;
        }
    CHECK (sawTwoOct);
    // After the switch: exactly 1x forward, at once.
    CHECK (r.speeds.back() == 1.0f);
    CHECK (r.speeds[(size_t) (3.5 * kSr / 480) + 1] == 1.0f);
}

TEST_CASE ("New tables are taken up at the next block, without blocking", "[stage3][tables]")
{
    dsp::Engine e;
    e.prepare (kSr, 512);
    auto t = dsp::MacroTables::defaults();
    t.clickLevel = 0.123f;
    CHECK (e.setTables (t));
    auto t2 = t;
    t2.clickLevel = 0.5f;
    CHECK_FALSE (e.setTables (t2));   // the first set is still waiting
    Signal z (64, 0.0f), o (64);
    e.process (z.data(), z.data(), o.data(), o.data(), 64);
    CHECK (e.tables().clickLevel == 0.123f);
    CHECK (e.setTables (t2));
    e.process (z.data(), z.data(), o.data(), o.data(), 64);
    CHECK (e.tables().clickLevel == 0.5f);
}

TEST_CASE ("Every zone of both knobs renders finite and under the ceiling, both stereo modes", "[stage3][safety]")
{
    const Signal in = noise ((size_t) (10.0 * kSr), 0.9f);
    const auto t = dsp::MacroTables::defaults();
    for (int z = 0; z < dsp::kZones; ++z)
        for (auto stereo : { dsp::StereoMode::unique, dsp::StereoMode::shared })
        {
            const float lo = t.bendZoneStart[(size_t) z], hi = z + 1 < dsp::kZones ? t.bendZoneStart[(size_t) z + 1] : 1.0f;
            auto p = macro (lo + 0.9f * (hi - lo), lo + 0.9f * (hi - lo), 6, 8);   // x3, Repeats 8
            p.stereo = stereo;
            p.seed = 100 + z;
            const auto r = renderWith (in, p, t, randomBlocks ((uint32_t) z + 1));
            float peak = 0.0f;
            INFO ("zone " << z);
            CHECK (allFinite (r, peak));
            CHECK (peak < 2.5f);
        }
}
