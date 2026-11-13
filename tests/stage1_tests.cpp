// Stage 1: buffer core. Clock timing, the clean delayed copy, windowing, freeze, mix, block-size invariance.
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

using namespace fault;
using namespace fault::test;

namespace
{
dsp::EngineParams hostParams (int step, int repeats = 1, float windowing = 0.0f)
{
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = (float) step / 8.0f;
    p.repeats = repeats;
    p.mix = 1.0f;
    p.windowing = windowing;
    return p;
}
} // namespace

TEST_CASE ("Host ticks are sample-accurate over 10 minutes at 120 BPM", "[stage1][clock]")
{
    // x1 = one tick per beat = 24000 samples at 48 kHz, from beat 0; random host block sizes.
    const Signal in ((size_t) (600.0 * kSr));
    const auto r = render (in, hostParams (4), randomBlocks());
    REQUIRE (r.ticks.size() == 1200);
    for (size_t k = 0; k < r.ticks.size(); ++k)
    {
        INFO ("tick " << k);
        REQUIRE (r.ticks[k].sample == (int64_t) k * 24000);
    }
}

TEST_CASE ("Every host division / multiplication ticks on its grid", "[stage1][clock]")
{
    const Signal in ((size_t) (40.0 * kSr));
    for (int step = 0; step <= 8; ++step)
    {
        const double rate = dsp::Clock::kRates[(size_t) step];
        const double spacing = 24000.0 / rate;   // samples per tick at 120 BPM
        const auto r = render (in, hostParams (step), randomBlocks (100 + (uint32_t) step));
        INFO (dsp::Clock::kRateNames[(size_t) step]);
        REQUIRE (! r.ticks.empty());
        for (size_t k = 0; k < r.ticks.size(); ++k)
            REQUIRE (r.ticks[k].sample == (int64_t) std::ceil ((double) k * spacing - 1.0e-6));
    }
}

TEST_CASE ("Internal clock: 16 s at Time 0 to 80 Hz at Time 1, without drift", "[stage1][clock]")
{
    CHECK (std::abs (dsp::Clock::internalPeriodSeconds (0.0f) - 16.0) < 1e-9);
    CHECK (std::abs (dsp::Clock::internalPeriodSeconds (1.0f) - 1.0 / 80.0) < 1e-9);
    for (float t : { 0.55f, 0.8f, 1.0f })
    {
        dsp::EngineParams p;
        p.time = t;
        const Signal in ((size_t) (30.0 * kSr));
        const auto r = render (in, p, randomBlocks (3), 120.0, false);
        const double period = dsp::Clock::internalPeriodSeconds (t) * kSr;
        INFO ("time " << t << ", period " << period);
        REQUIRE (r.ticks.size() > 10);
        for (size_t k = 0; k < r.ticks.size(); ++k)
            REQUIRE (std::abs ((double) r.ticks[k].sample - ((double) (k + 1) * period - 1.0)) <= 1.0);
    }
}

TEST_CASE ("Repeats 1, speed 1, Mix 100% is a clean delayed copy of the input", "[stage1][window]")
{
    const Signal in = noise ((size_t) (8.0 * kSr));
    const auto r = render (in, hostParams (4, 1, 0.0f), randomBlocks());
    // After the first tick (at 0, with nothing recorded) the output is the input one period (24000) late.
    for (size_t i = 24000; i < in.size(); ++i)
        REQUIRE (r.l[i] == in[i - 24000]);
}

TEST_CASE ("Windowing 2% removes the slice-edge discontinuities that 0% leaves", "[stage1][envelope]")
{
    const Signal in = sine ((size_t) (6.0 * kSr), 220.0);
    const auto maxStep = [] (const Signal& x, size_t from) {
        float m = 0.0f;
        for (size_t i = from + 1; i < x.size(); ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    };
    const float natural = maxStep (in, 0);   // 0.5 · 2π · 220 / 48000 ≈ 0.0144
    for (int repeats : { 3, 8, 32 })
    {
        const auto hard = render (in, hostParams (4, repeats, 0.0f), fixedBlocks (512));
        const auto soft = render (in, hostParams (4, repeats, 0.02f), fixedBlocks (512));
        INFO ("repeats " << repeats << ": natural " << natural << ", 0% " << maxStep (hard.l, 24000) << ", 2% " << maxStep (soft.l, 24000));
        CHECK (maxStep (hard.l, 24000) > 4.0f * natural);   // hard edges click, as intended
        CHECK (maxStep (soft.l, 24000) < 2.5f * natural);   // 2% fades: no jump beyond a steep sine slope
    }
}

TEST_CASE ("Output doesn't depend on the host's block sizes", "[stage1][blocks]")
{
    const Signal in = noise ((size_t) (5.0 * kSr));
    for (auto p : { hostParams (5, 4, 0.02f), hostParams (2, 7, 0.3f) })
    {
        p.mix = 0.6f;
        const auto a = render (in, p, fixedBlocks (1));
        const auto b = render (in, p, fixedBlocks (512));
        const auto c = render (in, p, randomBlocks (11));
        REQUIRE (a.l == b.l);
        REQUIRE (a.l == c.l);
        REQUIRE (a.r == c.r);
    }
}

TEST_CASE ("Freeze holds the window; growing Time while frozen brings older audio back", "[stage1][freeze]")
{
    // 440 Hz for the first 2 s, then silence. Freeze (latching: on the next tick) at 3 s with a 0.5 s
    // window of silence; then Time /4 (4 beats = 2 s) reaches back to the tone.
    Signal in ((size_t) (8.0 * kSr));
    const auto tone = sine ((size_t) (2.0 * kSr), 440.0);
    std::copy (tone.begin(), tone.end(), in.begin());
    auto p = hostParams (4, 1, 0.02f);
    const auto r = render (in, p, fixedBlocks (256), 120.0, true, [] (dsp::EngineParams& q, size_t pos) {
        q.freeze = pos >= (size_t) (2.9 * kSr);
        q.time = pos >= (size_t) (4.5 * kSr) ? 2.0f / 8.0f : 4.0f / 8.0f;
    });
    const auto rms = [&r] (double t0, double t1) {
        double e = 0;
        for (auto i = (size_t) (t0 * kSr); i < (size_t) (t1 * kSr); ++i) e += r.l[i] * r.l[i];
        return std::sqrt (e / ((t1 - t0) * kSr));
    };
    CHECK (rms (3.1, 4.4) < 1.0e-6);   // frozen on silence
    CHECK (rms (5.1, 7.9) > 0.05);     // the older tone returns
    // The freeze engaged on the tick at 3.0 s, not when asked at 2.9 s.
    bool frozenAt3 = false, frozenBefore = false;
    for (const auto& t : r.ticks)
    {
        if (t.sample == (int64_t) (3.0 * kSr)) frozenAt3 = t.frozen;
        if (t.sample == (int64_t) (2.5 * kSr)) frozenBefore = t.frozen;
    }
    CHECK (frozenAt3);
    CHECK_FALSE (frozenBefore);
}

TEST_CASE ("Frozen output repeats exactly, period after period", "[stage1][freeze]")
{
    const Signal in = noise ((size_t) (6.0 * kSr));
    const auto r = render (in, hostParams (4, 1, 0.0f), fixedBlocks (64), 120.0, true,
                           [] (dsp::EngineParams& q, size_t pos) { q.freeze = pos >= (size_t) (1.0 * kSr); });
    for (size_t i = (size_t) (1.5 * kSr); i + 24000 < in.size(); ++i)
        REQUIRE (r.l[i] == r.l[i + 24000]);
}

TEST_CASE ("Mix: fully dry is the input; freezing at fully dry turns it wet until release", "[stage1][mix]")
{
    const Signal in = noise ((size_t) (6.0 * kSr));
    auto p = hostParams (4, 2, 0.02f);
    p.mix = 0.0f;
    const auto dry = render (in, p, randomBlocks());
    REQUIRE (dry.l == in);
    const auto r = render (in, p, fixedBlocks (128), 120.0, true,
                           [] (dsp::EngineParams& q, size_t pos) { q.freeze = pos >= (size_t) (2.0 * kSr) && pos < (size_t) (4.0 * kSr); });
    double diff = 0;
    for (auto i = (size_t) (2.6 * kSr); i < (size_t) (3.9 * kSr); ++i) diff += std::abs (r.l[i] - in[i]);
    CHECK (diff > 100.0);   // wet while frozen
    for (auto i = (size_t) (4.6 * kSr); i < in.size(); ++i) REQUIRE (r.l[i] == in[i]);   // dry again after release
}
