// Break this bar: a fill armed by hand (or by a gate note) waits for its place before a bar line, plays
// glitched, and lets go exactly on the downbeat; the auto mode breaks the end of every Nth bar by chance.
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

using namespace fault;
using namespace fault::test;

namespace
{
constexpr double kBpm = 120.0;   // 2 beats a second: a 4/4 bar is 2 s
constexpr int kFillNote = 42;

struct Run
{
    Signal l;
    uint32_t fills = 0;
    std::vector<int> states;   // status().fill after each block
};

// The engine like a host, with the fill gate on kFillNote. `fillAt` (seconds): note-ons on the fill gate.
// `jumpAt`: from that time on, the transport is moved back by `jumpBy` beats (a loop).
Run run (const Signal& in, dsp::EngineParams p, std::vector<double> fillAt, std::function<int (size_t)> blockFor, bool playing = true,
         uint64_t sessionSeed = 1, double jumpAt = -1.0, double jumpBy = 0.0)
{
    dsp::Engine e;
    e.setParams (p);
    e.prepare (kSr, 4096);
    e.setSessionSeed (sessionSeed);
    std::array<int, dsp::kNumGates> gates { 36, 37, 38, 39, 40, 41, kFillNote };
    e.setGateNotes (gates);
    Run out { Signal (in.size()) };
    size_t next = 0;
    for (size_t pos = 0; pos < in.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) std::max (1, blockFor (pos)), in.size() - pos);
        double ppq = (double) pos / kSr * kBpm / 60.0;
        if (jumpAt >= 0.0 && (double) pos / kSr >= jumpAt) ppq -= jumpBy;
        e.setTransport ({ true, playing, kBpm, ppq });
        while (next < fillAt.size() && (size_t) std::llround (fillAt[next] * kSr) < pos + (size_t) n)
        {
            const auto at = (size_t) std::llround (fillAt[next] * kSr);
            e.queueNote ({ (int) (at > pos ? at - pos : 0), kFillNote, 100, true });
            ++next;
        }
        e.process (in.data() + pos, in.data() + pos, out.l.data() + pos, out.l.data() + pos, n);
        out.states.push_back (e.status().fill.load());
        pos += (size_t) n;
    }
    out.fills = e.status().fills.load();
    return out;
}

dsp::EngineParams dryParams()
{
    dsp::EngineParams p;
    p.mix = 0.0f;   // dry until a fill: anything wet is the fill
    p.time = 0.5f;
    p.clock = dsp::ClockSource::internal;
    p.seed = 7;
    return p;
}

// First and last sample where the output isn't the input.
std::pair<long, long> wetSpan (const Run& r, const Signal& in)
{
    long first = -1, last = -1;
    for (size_t i = 0; i < in.size(); ++i)
        if (std::abs (r.l[i] - in[i]) > 1.0e-6f)
        {
            if (first < 0) first = (long) i;
            last = (long) i;
        }
    return { first, last };
}

long sec (double s) { return (long) std::llround (s * kSr); }
constexpr long kMixFade = 960;   // the Mix smoothing (20 ms): the wet fades out over it after the downbeat
} // namespace

TEST_CASE ("Break this bar waits for the next bar line, breaks that bar, and lets go on the downbeat", "[stage8][fill]")
{
    const auto in = noise ((size_t) (7.0 * kSr), 0.3f, 3);
    auto p = dryParams();
    for (int block : { 512, 64, 1000 })
    {
        // Pressed at beat 2.6 of bar 1: the fill is bar 2 (2 s .. 4 s).
        const auto r = run (in, p, { 1.3 }, [block] (size_t) { return block; });
        const auto [first, last] = wetSpan (r, in);
        INFO ("block " << block << ": wet from " << first << " to " << last);
        CHECK (first == sec (2.0));
        CHECK (last >= sec (4.0) - 1);
        CHECK (last < sec (4.0) + kMixFade);
        CHECK (r.fills == 1);
    }
}

TEST_CASE ("Break this bar lands on the same samples whatever the block sizes", "[stage8][fill]")
{
    const auto in = noise ((size_t) (5.0 * kSr), 0.3f, 4);
    const auto p = dryParams();
    const auto a = run (in, p, { 0.7 }, [] (size_t) { return 512; });
    uint32_t s = 99;
    const auto b = run (in, p, { 0.7 }, [&s] (size_t) { s = s * 1664525u + 1013904223u; return 1 + (int) ((s >> 8) % 1500u); });
    REQUIRE (a.l.size() == b.l.size());
    size_t diffs = 0;
    for (size_t i = 0; i < a.l.size(); ++i) diffs += std::abs (a.l[i] - b.l[i]) > 1.0e-5f;
    CHECK (diffs == 0);
}

TEST_CASE ("Pressed just after the downbeat, the fill starts at once and still ends on the bar", "[stage8][fill]")
{
    const auto in = noise ((size_t) (7.0 * kSr), 0.3f, 5);
    // Beat 4.1 (bar 2's first 1/16): late, but inside the grace.
    const auto r = run (in, dryParams(), { 2.05 }, [] (size_t) { return 256; });
    const auto [first, last] = wetSpan (r, in);
    CHECK (first == sec (2.05));
    CHECK (last >= sec (4.0) - 1);
    CHECK (last < sec (4.0) + kMixFade);
}

TEST_CASE ("Shorter fills break the end of the bar, leading into the downbeat", "[stage8][fill]")
{
    const auto in = noise ((size_t) (5.0 * kSr), 0.3f, 6);
    for (auto [len, start] : { std::pair { dsp::FillLength::beat, 1.5 }, std::pair { dsp::FillLength::twoBeats, 1.0 } })
    {
        auto p = dryParams();
        p.fillLength = len;
        const auto r = run (in, p, { 0.25 }, [] (size_t) { return 512; });
        const auto [first, last] = wetSpan (r, in);
        INFO ("length " << (int) len);
        CHECK (first == sec (start));
        CHECK (last >= sec (2.0) - 1);
        CHECK (last < sec (2.0) + kMixFade);
    }
    // Two bars: from the next bar line, for 4 s.
    auto p = dryParams();
    p.fillLength = dsp::FillLength::twoBars;
    const auto r = run (noise ((size_t) (8.0 * kSr), 0.3f, 6), p, { 0.25 }, [] (size_t) { return 512; });
    const auto [first, last] = wetSpan (r, noise ((size_t) (8.0 * kSr), 0.3f, 6));
    CHECK (first == sec (2.0));
    CHECK (last < sec (6.0) + kMixFade);
    CHECK (last >= sec (6.0) - 1);
}

TEST_CASE ("The fill actually breaks: wet, on a fast grid, with the dice", "[stage8][fill]")
{
    const auto in = noise ((size_t) (5.0 * kSr), 0.3f, 7);
    auto p = dryParams();
    p.fillAmount = 1.0f;
    dsp::Engine e;
    e.setParams (p);
    e.prepare (kSr, 4096);
    e.triggerFill();
    Signal out (in.size());
    std::vector<dsp::TickEvent> ticks;
    uint64_t seen = 0;
    for (size_t pos = 0; pos < in.size(); pos += 512)
    {
        const int n = (int) std::min<size_t> (512, in.size() - pos);
        e.setTransport ({ true, true, kBpm, (double) pos / kSr * kBpm / 60.0 });
        e.process (in.data() + pos, in.data() + pos, out.data() + pos, out.data() + pos, n);
        for (; seen < e.ticksWritten(); ++seen) ticks.push_back (e.tick (seen));
    }
    // Armed on the very first downbeat, so bar 1 (0..2 s) breaks: ticks every 1/16 (6000 samples at
    // 120 bpm), the first on the bar line.
    std::vector<int64_t> inFill;
    int broken = 0;
    for (const auto& t : ticks)
        if (t.sample >= 0 && t.sample < sec (2.0))
        {
            inFill.push_back (t.sample);
            broken += (t.breakMask[0] | t.bendMask[0]) != 0;
        }
    REQUIRE (inFill.size() == 16);
    CHECK (inFill.front() == 0);
    for (size_t k = 1; k < inFill.size(); ++k) CHECK (inFill[k] - inFill[k - 1] == 6000);
    CHECK (broken >= 4);   // a full-strength fill: plenty of the 16 ticks rolled something
    // And a tick exactly on the downbeat after it: the clean window starts there.
    bool downbeat = false;
    for (const auto& t : ticks) downbeat = downbeat || t.sample == sec (2.0);
    CHECK (downbeat);
}

TEST_CASE ("Auto: the end of every Nth bar breaks, by its chance", "[stage8][fill][auto]")
{
    const auto in = noise ((size_t) (17.0 * kSr), 0.3f, 8);
    auto p = dryParams();
    p.fillEvery = 4;
    p.fillLength = dsp::FillLength::twoBeats;
    p.fillChance = 1.0f;
    const auto always = run (in, p, {}, [] (size_t) { return 512; });
    CHECK (always.fills == 2);   // bars 4 and 8 (beats 14-16 and 30-32): 7-8 s and 15-16 s
    const auto [first, last] = wetSpan (always, in);
    CHECK (first == sec (7.0));
    CHECK (last >= sec (16.0) - 1);
    CHECK (last < sec (16.0) + kMixFade);
    for (long i = sec (8.0) + kMixFade; i < sec (15.0); ++i)
        if (std::abs (always.l[(size_t) i] - in[(size_t) i]) > 1.0e-6f) { FAIL ("wet between the fills at " << i); break; }

    p.fillChance = 0.0f;
    CHECK (run (in, p, {}, [] (size_t) { return 512; }).fills == 0);

    // A fixed seed: the same bars break whatever the session.
    p.fillChance = 0.5f;
    p.fillEvery = 2;
    const auto in2 = noise ((size_t) (40.0 * kSr), 0.3f, 9);
    const auto a = run (in2, p, {}, [] (size_t) { return 512; }, true, 1);
    const auto b = run (in2, p, {}, [] (size_t) { return 333; }, true, 12345);
    CHECK (a.fills == b.fills);
    CHECK (a.fills > 0);
    CHECK (a.fills < 10);   // 10 chances at 50%
    CHECK (wetSpan (a, in2) == wetSpan (b, in2));
}

TEST_CASE ("Stopped transport: the fill starts at once and lasts its length at the tempo", "[stage8][fill]")
{
    const auto in = noise ((size_t) (4.0 * kSr), 0.3f, 10);
    const auto r = run (in, dryParams(), { 0.5 }, [] (size_t) { return 512; }, false);
    const auto [first, last] = wetSpan (r, in);
    CHECK (first == sec (0.5));
    CHECK (last >= sec (2.5) - 1);   // one bar of 4 beats at 120 bpm
    CHECK (last < sec (2.5) + kMixFade);
}

TEST_CASE ("A loop or locate drops the fill and goes back to clean", "[stage8][fill]")
{
    const auto in = noise ((size_t) (6.0 * kSr), 0.3f, 11);
    // Fill armed for bar 2 (2..4 s); at 3 s the transport jumps back 4 beats: the fill stops there.
    const auto r = run (in, dryParams(), { 1.0 }, [] (size_t) { return 512; }, true, 1, 3.0, 4.0);
    const auto [first, last] = wetSpan (r, in);
    CHECK (first == sec (2.0));
    CHECK (last < sec (3.0) + 512 + kMixFade);
    CHECK (r.states.back() == 0);
}

TEST_CASE ("A second press while a fill plays is ignored; the status shows armed, then playing", "[stage8][fill]")
{
    const auto in = noise ((size_t) (5.0 * kSr), 0.3f, 12);
    const auto r = run (in, dryParams(), { 1.0, 2.5, 3.0 }, [] (size_t) { return 4800; });   // 0.1 s blocks
    CHECK (r.fills == 1);
    CHECK (r.states[10] == 1);    // 1.0-1.1 s: armed
    CHECK (r.states[25] == 2);    // 2.5 s: playing
    CHECK (r.states[45] == 0);    // 4.5 s: done
}

TEST_CASE ("Auto every 8 bars keeps going after a press by hand", "[stage8][fill][auto][press]")
{
    // George, 21 Nov: Every 8 bars at 100%, press Break this bar: it broke once, then never again.
    // (The press took over the auto fill already armed for bar 8, and bar 8 counted as decided.)
    const auto in = noise ((size_t) (70.0 * kSr), 0.3f, 10);   // 35 bars at 120 (a bar is 2 s)
    auto p = dryParams();
    p.fillEvery = 8;
    p.fillChance = 1.0f;
    for (auto len : { dsp::FillLength::beat, dsp::FillLength::twoBeats, dsp::FillLength::bar, dsp::FillLength::twoBars })
        for (double press : { 0.5, 3.0, 9.9, 13.9, 15.0, 21.0, 33.0 })
        {
            p.fillLength = len;
            const auto r = run (in, p, { press }, [] (size_t) { return 512; });
            // The last beat of bars 8, 16, 24 and 32 breaks, whenever the press was.
            for (int bar : { 8, 16, 24, 32 })
            {
                bool wet = false;
                for (long i = sec (2.0 * bar - 0.25); i < sec (2.0 * bar - 0.05) && ! wet; ++i)
                    wet = std::abs (r.l[(size_t) i] - in[(size_t) i]) > 1.0e-6f;
                INFO ("length " << (int) len << ", pressed at " << press << " s, bar " << bar);
                CHECK (wet);
            }
        }
}
