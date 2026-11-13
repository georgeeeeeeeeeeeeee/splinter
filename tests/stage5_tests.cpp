// Stage 5: modulation (modulators, envelope follower, CC lanes, routes, attenuators), gate notes with
// latching / momentary behaviour, the Note clock, Reset, Corrupt-as-reset.
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace fault;
using namespace fault::test;
using dsp::ModDest;
using dsp::ModSource;

namespace
{
constexpr int kFreeze = 36, kBend = 37, kBreak = 38, kCorrupt = 39, kReset = 40, kClock = 41;

struct Ev { int64_t at; int note; int value; bool on; bool cc = false; };
struct Run
{
    Signal l, r;
    std::vector<dsp::TickEvent> ticks;
    std::vector<int64_t> blockEnds;          // sample after each block
    std::vector<float> speed, modulated[dsp::kNumModDests], sources[dsp::kNumModSources];
    std::vector<int> corruptType;
    std::vector<bool> frozen, bendOn, breakOn;
};

// A host: blocks from `blocks`, transport playing from `playFrom` (samples; < 0 never), events at
// absolute samples, landing in whichever block holds them.
Run run (const Signal& in, dsp::EngineParams p, std::function<int (size_t)> blocks, std::vector<Ev> ev = {}, int64_t playFrom = 0,
         std::function<void (dsp::EngineParams&, size_t)> perBlock = {})
{
    dsp::Engine e;
    e.setSessionSeed (7);
    e.setParams (p);
    e.prepare (kSr, 4096);
    Run o { Signal (in.size()), Signal (in.size()) };
    size_t next = 0;
    uint64_t seen = 0;
    for (size_t pos = 0; pos < in.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) std::max (1, blocks (pos)), in.size() - pos);
        if (perBlock) { perBlock (p, pos); e.setParams (p); }
        const bool playing = playFrom >= 0 && (int64_t) pos >= playFrom;
        e.setTransport ({ true, playing, 120.0, playing ? (double) ((int64_t) pos - playFrom) / kSr * 2.0 : 0.0 });
        while (next < ev.size() && ev[next].at < (int64_t) (pos + (size_t) n))
        {
            const auto& x = ev[next++];
            e.queueNote ({ (int) std::max<int64_t> (0, x.at - (int64_t) pos), x.note, x.value, x.on, x.cc });
        }
        e.process (in.data() + pos, in.data() + pos, o.l.data() + pos, o.r.data() + pos, n);
        for (; seen < e.ticksWritten(); ++seen) o.ticks.push_back (e.tick (seen));
        pos += (size_t) n;
        const auto& st = e.status();
        o.blockEnds.push_back ((int64_t) pos);
        o.speed.push_back (st.speed.load());
        for (int k = 0; k < dsp::kNumModDests; ++k) o.modulated[k].push_back (st.modulated[(size_t) k].load());
        for (int k = 0; k < dsp::kNumModSources; ++k) o.sources[k].push_back (st.sources[(size_t) k].load());
        o.corruptType.push_back (st.corruptType.load());
        o.frozen.push_back (st.frozen.load());
        o.bendOn.push_back (st.bendOn.load());
        o.breakOn.push_back (st.breakOn.load());
    }
    return o;
}

Ev on (int64_t at, int note) { return { at, note, 100, true }; }
Ev off (int64_t at, int note) { return { at, note, 0, false }; }
Ev cc (int64_t at, int number, int value) { return { at, number, value, true, true }; }

dsp::EngineParams internalSlow()
{
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::internal;
    p.time = 0.0f;   // 16 s: no ticks of its own in these tests
    p.mix = 1.0f;
    return p;
}

std::vector<int64_t> tickSamples (const Run& r)
{
    std::vector<int64_t> s;
    for (const auto& t : r.ticks) s.push_back (t.sample);
    return s;
}

// Value of a per-block series at the block holding sample `at`.
template <typename T> T atSample (const Run& r, const std::vector<T>& v, int64_t at)
{
    for (size_t i = 0; i < r.blockEnds.size(); ++i)
        if (r.blockEnds[i] > at) return v[i];
    return v.back();
}
} // namespace

TEST_CASE ("Reset and Note clock gates tick on their exact sample, at any block size", "[stage5][gates]")
{
    const auto in = noise ((size_t) (2.0 * kSr));
    for (auto blocks : { fixedBlocks (1), fixedBlocks (512), fixedBlocks (333), randomBlocks (5) })
    {
        const auto r = run (in, internalSlow(), blocks, { on (12345, kReset), off (12400, kReset) }, -1);
        REQUIRE (r.ticks.size() == 1);
        CHECK (r.ticks[0].sample == 12345);

        auto p = internalSlow();
        p.clock = dsp::ClockSource::note;
        p.time = 0.5f;   // x1: a tick per pulse
        const auto c = run (in, p, blocks, { on (20011, kClock), off (20100, kClock), on (44017, kClock) }, -1);
        CHECK (tickSamples (c) == std::vector<int64_t> { 20011, 44017 });
    }
}

TEST_CASE ("A gate's effect starts on its sample: output identical before, different from it", "[stage5][gates]")
{
    const auto in = noise ((size_t) (1.0 * kSr));
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = 0.5f;
    p.mix = 1.0f;
    // Micro, Break at 90%: the Break gate flips Traverse to Silence (81% of each slice silent). Sample
    // 31007 is 7007 into a 24000-sample slice, deep in the silent part, so the output drops on that sample.
    p.mode = dsp::Mode::micro;
    p.brk = 0.9f;
    for (auto blocks : { fixedBlocks (1), fixedBlocks (512), randomBlocks (9) })
    {
        const auto plain = run (in, p, blocks);
        const auto gated = run (in, p, blocks, { on (31007, kBreak) });
        size_t first = 0;
        while (first < in.size() && plain.l[first] == gated.l[first]) ++first;
        CHECK (first == 31007);
    }
}

TEST_CASE ("Latching gates flip on each note-on; momentary gates hold while the note is down", "[stage5][gates]")
{
    const auto in = noise ((size_t) (1.0 * kSr));
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.mix = 1.0f;
    p.mode = dsp::Mode::micro;
    const std::vector<Ev> ev { on (4800, kBend), off (9600, kBend), on (19200, kBend), off (24000, kBend), on (28800, kBreak) };
    const auto latch = run (in, p, fixedBlocks (480), ev);
    CHECK_FALSE (atSample (latch, latch.bendOn, 4000));
    CHECK (atSample (latch, latch.bendOn, 6000));
    CHECK (atSample (latch, latch.bendOn, 12000));    // still on after note-off
    CHECK_FALSE (atSample (latch, latch.bendOn, 20000));
    CHECK_FALSE (atSample (latch, latch.bendOn, 26000));
    CHECK (atSample (latch, latch.speed, 6000) < 0.0f);   // Micro: Bend on = reverse
    CHECK (atSample (latch, latch.breakOn, 30000));

    p.gateMode = dsp::GateMode::momentary;
    const auto mom = run (in, p, fixedBlocks (480), ev);
    CHECK (atSample (mom, mom.bendOn, 6000));
    CHECK_FALSE (atSample (mom, mom.bendOn, 12000));   // back on release
    CHECK (atSample (mom, mom.bendOn, 20000));
    CHECK_FALSE (atSample (mom, mom.bendOn, 26000));

    // The gate flips the current mode's toggle only: back in Macro, Macro's Bend is untouched.
    const auto modes = run (in, p, fixedBlocks (480), { on (4800, kBend) }, 0,
                            [] (dsp::EngineParams& q, size_t pos) { q.mode = pos < 9600 ? dsp::Mode::micro : dsp::Mode::macro; });
    CHECK (atSample (modes, modes.bendOn, 6000));
    CHECK_FALSE (atSample (modes, modes.bendOn, 12000));
}

TEST_CASE ("Freeze gate: latching freeze engages on the next tick, momentary at once", "[stage5][gates][freeze]")
{
    const auto in = noise ((size_t) (2.0 * kSr));
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = 0.5f;   // a tick every beat (24000 samples)
    p.mix = 1.0f;
    const auto latch = run (in, p, fixedBlocks (480), { on (30000, kFreeze) });
    CHECK_FALSE (atSample (latch, latch.frozen, 40000));
    CHECK (atSample (latch, latch.frozen, 48500));
    p.freezeMode = dsp::FreezeMode::momentary;
    const auto now = run (in, p, fixedBlocks (480), { on (30000, kFreeze) });
    CHECK (atSample (now, now.frozen, 30500));
}

TEST_CASE ("Corrupt gate steps through the set; Corrupt-as-reset makes it a Reset", "[stage5][gates][corrupt]")
{
    const auto in = noise ((size_t) (1.0 * kSr));
    auto p = internalSlow();
    p.corrupt = 0.5f;
    p.corruptSet = dsp::CorruptSet::original3;
    const auto r = run (in, p, fixedBlocks (480), { on (4800, kCorrupt), off (5000, kCorrupt), on (9600, kCorrupt), off (9700, kCorrupt), on (14400, kCorrupt) }, -1);
    CHECK (atSample (r, r.corruptType, 4000) == 0);
    CHECK (atSample (r, r.corruptType, 6000) == 1);
    CHECK (atSample (r, r.corruptType, 11000) == 2);
    CHECK (atSample (r, r.corruptType, 16000) == 0);   // Original 3 wraps after Destroy
    CHECK (r.ticks.empty());

    p.gateMode = dsp::GateMode::momentary;
    const auto m = run (in, p, fixedBlocks (480), { on (4800, kCorrupt), off (9600, kCorrupt) }, -1);
    CHECK (atSample (m, m.corruptType, 6000) == 1);
    CHECK (atSample (m, m.corruptType, 11000) == 0);

    p.gateMode = dsp::GateMode::latching;
    p.corruptAsReset = true;
    const auto asReset = run (in, p, fixedBlocks (480), { on (7777, kCorrupt) }, -1);
    CHECK (atSample (asReset, asReset.corruptType, 9000) == 0);
    REQUIRE (asReset.ticks.size() == 1);
    CHECK (asReset.ticks[0].sample == 7777);
}

TEST_CASE ("Note clock: x2 ticks between pulses, /2 every other pulse, and it carries on without pulses", "[stage5][clock]")
{
    const auto in = noise ((size_t) (6.0 * kSr));
    std::vector<Ev> pulses;
    for (int k = 0; k < 6; ++k) { pulses.push_back (on (12000 + k * 24000, kClock)); pulses.push_back (off (12000 + k * 24000 + 100, kClock)); }
    auto p = internalSlow();
    p.clock = dsp::ClockSource::note;

    p.time = 0.5f;   // x1
    const auto x1 = run (in, p, randomBlocks (2), pulses, -1);
    REQUIRE (x1.ticks.size() >= 6);
    for (int k = 0; k < 6; ++k) CHECK (x1.ticks[(size_t) k].sample == 12000 + k * 24000);
    CHECK (x1.ticks[3].windowLength == 24000.0);   // the window follows the measured interval

    p.time = 0.625f;   // x2
    const auto x2 = run (in, p, randomBlocks (2), pulses, -1);
    const auto s2 = tickSamples (x2);
    REQUIRE (s2.size() >= 11);
    // The first pulse can't know the interval yet; from the second on, a tick halfway between pulses.
    CHECK (std::find (s2.begin(), s2.end(), 36000 + 12000) != s2.end());
    CHECK (std::find (s2.begin(), s2.end(), 60000 + 12000) != s2.end());

    p.time = 0.375f;   // /2
    const auto d2 = run (in, p, randomBlocks (2), pulses, -1);
    const auto sd = tickSamples (d2);
    REQUIRE (sd.size() >= 3);
    CHECK (sd[0] == 12000);
    CHECK (sd[1] == 60000);
    CHECK (sd[2] == 108000);

    // After the last pulse (132000), nothing for four intervals, then ticks carry on, on the same grid.
    std::vector<int64_t> after;
    for (auto s : tickSamples (x1)) if (s > 132000) after.push_back (s);
    REQUIRE (after.size() >= 2);
    CHECK (after[0] > 132000 + 4 * 24000);
    CHECK ((after[0] - 132000) % 24000 == 0);
    CHECK (after[1] - after[0] == 24000);
}

TEST_CASE ("Reset on the host clock realigns divisions to the next beat; a transport start is a Reset", "[stage5][clock]")
{
    const auto in = noise ((size_t) (10.0 * kSr));
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = 0.25f;   // /4: a tick every 4 beats (96000 samples)
    p.mix = 1.0f;
    const auto r = run (in, p, fixedBlocks (512), { on (30000, kReset) });   // beat 1.25: the next beat is 2 (48000)
    const auto s = tickSamples (r);
    CHECK (s == std::vector<int64_t> { 0, 48000, 48000 + 96000, 48000 + 2 * 96000, 48000 + 3 * 96000, 48000 + 4 * 96000 });

    // Internal clock, transport starting at sample 51200: a new window right there.
    auto q = internalSlow();
    const auto t = run (in, q, fixedBlocks (512), {}, 51200);
    REQUIRE (! t.ticks.empty());
    CHECK (t.ticks[0].sample == 51200);
}

TEST_CASE ("Modulator shapes, rates and polarity", "[stage5][mod]")
{
    using dsp::ModShape;
    const auto trace = [] (dsp::ModulatorParams p, double seconds, dsp::ModClock c = {}) {
        dsp::Modulator m;
        m.reseed (3);
        std::vector<float> v;
        for (int i = 0; i < (int) (seconds * kSr / dsp::kControlSamples); ++i) v.push_back (m.next (p, c, kSr, dsp::kControlSamples));
        return v;
    };
    // A free 2 Hz sine crosses zero upwards every 0.5 s (750 control steps).
    dsp::ModulatorParams sine { ModShape::sine, 2.0f, -1, 0.0f, true };
    const auto v = trace (sine, 3.0);
    std::vector<int> ups;
    for (size_t i = 1; i < v.size(); ++i) if (v[i - 1] < 0.0f && v[i] >= 0.0f) ups.push_back ((int) i);
    REQUIRE (ups.size() >= 4);
    CHECK (std::abs ((ups[3] - ups[2]) - 750) <= 1);
    // Ranges: bipolar -1..1, unipolar 0..1; every shape.
    for (int s = 0; s < dsp::kNumModShapes; ++s)
        for (bool bi : { true, false })
        {
            const auto t = trace ({ (ModShape) s, 5.0f, -1, 0.3f, bi }, 2.0);
            const auto [lo, hi] = std::minmax_element (t.begin(), t.end());
            CHECK (*lo >= (bi ? -1.0f : 0.0f));
            CHECK (*hi <= 1.0f);
            CHECK (*hi - *lo > 0.5f);
        }
    // S&H: stepped (constant within a cycle) at the rate.
    const auto sh = trace ({ ModShape::sampleHold, 4.0f, -1, 0.0f, true }, 2.0);
    int changes = 0;
    for (size_t i = 1; i < sh.size(); ++i) changes += sh[i] != sh[i - 1];
    CHECK (changes >= 7);
    CHECK (changes <= 8);
}

TEST_CASE ("Synced modulators lock to the bar: same value at the same beat, wherever playback started", "[stage5][mod]")
{
    for (int shape = 0; shape < dsp::kNumModShapes; ++shape)
    {
        const dsp::ModulatorParams p { (dsp::ModShape) shape, 1.0f, 5 /* 1/8 */, 0.25f, true };
        dsp::Modulator a, b;
        a.reseed (11);
        b.reseed (11);
        float va = 0.0f, vb = 0.0f;
        const double perStep = dsp::kControlSamples * 2.0 / kSr;   // beats per control step at 120 BPM
        for (double ppq = 0.0; ppq < 7.0; ppq += perStep) va = a.next (p, { true, 120.0, ppq }, kSr, dsp::kControlSamples);
        for (double ppq = 3.0; ppq < 7.0; ppq += perStep) vb = b.next (p, { true, 120.0, ppq }, kSr, dsp::kControlSamples);
        INFO ("shape " << shape);
        CHECK (va == vb);
    }
}

TEST_CASE ("A synced S&H on Repeats: a new slice count on every 1/16 tick, the same each time through", "[stage5][mod][stutter]")
{
    const auto in = noise ((size_t) (8.0 * kSr));
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = 0.875f;   // x4: a tick every 1/16
    p.repeats = 4;
    p.mix = 1.0f;
    p.seed = 5;
    p.mod.mods[0] = { dsp::ModShape::sampleHold, 1.0f, 2 /* 1/16 */, 0.0f, true };
    p.mod.routes[0] = { ModSource::mod1, ModDest::repeats, 0.6f };
    const auto r = run (in, p, randomBlocks (4));
    std::set<int> counts;
    int changes = 0;
    for (size_t i = 1; i < r.ticks.size(); ++i)
    {
        counts.insert (r.ticks[i].repeats);
        changes += r.ticks[i].repeats != r.ticks[i - 1].repeats;
    }
    CHECK (counts.size() >= 5);
    CHECK (changes > (int) r.ticks.size() / 2);
    // Each tick takes the S&H step of its own 1/16, not the one before: compare with an S&H seeded the
    // way the engine seeds Mod 1 (seed 5) and read at the tick's beat.
    dsp::Modulator ref;
    ref.reseed (dsp::mix64 (dsp::mix64 ((uint64_t) 5u * 0x9e3779b97f4a7c15ULL ^ 0x30d) + 0));
    for (const auto& t : r.ticks)
    {
        const float v = ref.at (p.mod.mods[0], { true, 120.0, (double) t.sample / kSr * 2.0 });
        const float x = std::clamp (std::log (4.0f) / std::log (32.0f) + 0.6f * v, 0.0f, 1.0f);
        INFO ("tick at " << t.sample);
        CHECK (t.repeats == std::clamp ((int) std::lround (std::pow (32.0f, x)), 1, 32));
    }
    // The same each time through: other block sizes give the same slice count on every tick.
    const auto again = run (in, p, fixedBlocks (512));
    REQUIRE (again.ticks.size() == r.ticks.size());
    for (size_t i = 0; i < r.ticks.size(); ++i) CHECK (again.ticks[i].repeats == r.ticks[i].repeats);
}

TEST_CASE ("Envelope follower, CC lanes and routes move their destinations", "[stage5][mod]")
{
    // Silence, then a loud burst: env -> Mix pulls Mix up while it's loud.
    Signal in ((size_t) (2.0 * kSr), 0.0f);
    const auto burst = sine ((size_t) (0.5 * kSr), 200.0, 0.8f);
    std::copy (burst.begin(), burst.end(), in.begin() + (size_t) kSr);
    dsp::EngineParams p;
    p.mix = 0.2f;
    p.mod.env = { 1.0f, 100.0f, 0.0f };
    p.mod.routes[0] = { ModSource::env, ModDest::mix, 0.5f };
    // CC lanes: lane 2 listens to CC 16, bipolar, into Corrupt; lane 1 (CC 1) unipolar into Time.
    p.mod.ccNumbers = { 1, 16, 17, 18 };
    p.mod.ccBipolar = { false, true, false, false };
    p.mod.routes[1] = { ModSource::cc2, ModDest::corrupt, 1.0f };
    p.mod.routes[2] = { ModSource::cc1, ModDest::time, -0.5f };
    p.corrupt = 0.5f;
    p.time = 0.6f;
    const auto r = run (in, p, fixedBlocks (480), { cc (24000, 16, 127), cc (24000, 1, 127), cc (30000, 99, 0) });
    CHECK (atSample (r, r.modulated[(int) ModDest::mix], 20000) < 0.21f);
    CHECK (atSample (r, r.modulated[(int) ModDest::mix], 60000) > 0.5f);
    CHECK (atSample (r, r.modulated[(int) ModDest::mix], 95000) < 0.25f);   // released
    CHECK (atSample (r, r.sources[(int) ModSource::cc2], 20000) == -1.0f);   // bipolar lane at CC 0
    CHECK (atSample (r, r.modulated[(int) ModDest::corrupt], 20000) == 0.0f);   // 0.5 - 1, clamped
    CHECK (atSample (r, r.modulated[(int) ModDest::corrupt], 36000) == 1.0f);   // smoothed up to +1 (CC 99 isn't a lane)
    CHECK (std::abs (atSample (r, r.modulated[(int) ModDest::time], 36000) - 0.1f) < 1.0e-4f);
}

TEST_CASE ("Attenuators scale Bend, Break and Corrupt modulation; the Bend one not in Micro", "[stage5][mod]")
{
    const auto in = noise ((size_t) (0.5 * kSr));
    dsp::EngineParams p;
    p.bend = 0.5f;
    p.brk = 0.2f;
    p.corrupt = 0.2f;
    p.mod.ccBipolar = { false, false, false, false };
    for (auto d : { ModDest::bend, ModDest::brk, ModDest::corrupt }) p.mod.routes[(size_t) d] = { ModSource::cc1, d, 0.4f };
    p.mod.bendAtten = 0.0f;
    p.mod.breakAtten = 0.5f;
    p.mod.corruptAtten = 0.25f;
    const std::vector<Ev> full { cc (0, 1, 127) };
    const auto macro = run (in, p, fixedBlocks (480), full);
    CHECK (macro.modulated[(int) ModDest::bend].back() == 0.5f);
    CHECK (std::abs (macro.modulated[(int) ModDest::brk].back() - 0.4f) < 1.0e-4f);
    CHECK (std::abs (macro.modulated[(int) ModDest::corrupt].back() - 0.3f) < 1.0e-4f);
    p.mode = dsp::Mode::micro;
    const auto micro = run (in, p, fixedBlocks (480), full);
    CHECK (std::abs (micro.modulated[(int) ModDest::bend].back() - 0.9f) < 1.0e-4f);
}

TEST_CASE ("Everything modulated, gated and CC-driven is still bit-identical at any block size", "[stage5][seed]")
{
    const auto in = noise ((size_t) (4.0 * kSr), 0.4f);
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = 0.6f;
    p.repeats = 3;
    p.mix = 0.9f;
    p.seed = 9;
    p.bendOnMacro = p.breakOnMacro = true;
    p.bend = 0.6f;
    p.brk = 0.6f;
    p.corrupt = 0.4f;
    p.corruptType = dsp::CorruptType::vinyl;
    p.mod.mods[0] = { dsp::ModShape::sampleHold, 1.0f, 2, 0.0f, true };
    p.mod.mods[1] = { dsp::ModShape::sine, 3.3f, -1, 0.1f, true };
    p.mod.mods[2] = { dsp::ModShape::smoothRandom, 0.7f, -1, 0.0f, false };
    p.mod.routes[0] = { ModSource::mod1, ModDest::repeats, 0.5f };
    p.mod.routes[1] = { ModSource::mod2, ModDest::time, 0.1f };
    p.mod.routes[2] = { ModSource::mod3, ModDest::bend, 0.3f };
    p.mod.routes[3] = { ModSource::env, ModDest::corrupt, 0.4f };
    p.mod.routes[4] = { ModSource::cc1, ModDest::brk, -0.3f };
    const std::vector<Ev> ev { on (20000, kFreeze), cc (30011, 1, 90), on (50000, kBreak), on (70001, kCorrupt), on (90000, kFreeze),
                               cc (100000, 1, 10), on (120000, kReset), on (130000, kBend), off (150000, kBend) };
    const auto a = run (in, p, fixedBlocks (1), ev);
    const auto b = run (in, p, fixedBlocks (512), ev);
    const auto c = run (in, p, randomBlocks (21), ev);
    CHECK (a.l == b.l);
    CHECK (a.r == b.r);
    CHECK (a.l == c.l);
    CHECK (a.r == c.r);
    for (size_t i = 0; i < a.l.size(); ++i) REQUIRE (std::isfinite (a.l[i]));
}

TEST_CASE ("Momentary gates can't stick: doubled notes, stray note-offs, mode switches, All Notes Off", "[stage7][gates]")
{
    const auto in = noise ((size_t) (1.0 * kSr));
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.mix = 1.0f;
    p.gateMode = dsp::GateMode::momentary;

    // Two note-ons of the Corrupt gate, two note-offs, then a stray one: one step, back on release.
    const auto dup = run (in, p, fixedBlocks (480), { on (4800, kCorrupt), on (7200, kCorrupt), off (9600, kCorrupt), off (14400, kCorrupt), off (19200, kCorrupt) });
    CHECK (atSample (dup, dup.corruptType, 8000) == 1);
    CHECK (atSample (dup, dup.corruptType, 12000) == 1);   // one key still down
    CHECK (atSample (dup, dup.corruptType, 16000) == 0);
    CHECK (atSample (dup, dup.corruptType, 24000) == 0);   // the stray note-off doesn't step back again

    // Bend held in Micro, released in Macro: Micro's Bend is back off when we return.
    p.mode = dsp::Mode::micro;
    const auto modes = run (in, p, fixedBlocks (480), { on (4800, kBend), off (14400, kBend) }, 0, [] (dsp::EngineParams& q, size_t pos) {
        q.mode = (pos >= 9600 && pos < 19200) ? dsp::Mode::macro : dsp::Mode::micro;
    });
    CHECK (atSample (modes, modes.bendOn, 6000));
    CHECK_FALSE (atSample (modes, modes.bendOn, 12000));   // Macro's Bend untouched
    CHECK_FALSE (atSample (modes, modes.bendOn, 24000));   // Micro's let go

    // All Notes Off (CC 123) releases a held gate.
    const auto panic = run (in, p, fixedBlocks (480), { on (4800, kBreak), cc (9600, 123, 0) });
    CHECK (atSample (panic, panic.breakOn, 6000));
    CHECK_FALSE (atSample (panic, panic.breakOn, 12000));
}
