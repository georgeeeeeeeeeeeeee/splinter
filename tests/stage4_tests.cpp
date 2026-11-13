// Stage 4: Corrupt. Decimate, Dropout, Destroy, DJ Filter, Vinyl, the set toggle, level safety, and
// knob sweeps that don't click.
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>

using namespace fault;
using namespace fault::test;
using dsp::CorruptType;

namespace
{
constexpr CorruptType kAll[] = { CorruptType::decimate, CorruptType::dropout, CorruptType::destroy, CorruptType::djFilter, CorruptType::vinyl };
const char* name (CorruptType t)
{
    const char* n[] = { "Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl" };
    return n[(int) t];
}

// Runs Corrupt alone, like the end of the wet chain. `knobAt` gives the knob per 48-sample step (the
// render CLI's 1 ms automation steps).
struct Stereo { Signal l, r; };
Stereo runCorrupt (const Signal& in, CorruptType type, std::function<float (size_t)> knobAt, uint64_t seed = 9,
                   dsp::CorruptSet set = dsp::CorruptSet::all, std::function<CorruptType (size_t)> typeAt = {})
{
    dsp::Corrupt c;
    c.prepare (kSr);
    c.reseed (seed);
    c.setParams (knobAt (0), type, set);
    c.reset();
    Stereo o { Signal (in.size()), Signal (in.size()) };
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (i % 48 == 0)
            c.setParams (knobAt (i), typeAt ? typeAt (i) : type, set);
        float l = in[i], r = in[i];
        c.process (l, r);
        o.l[i] = l;
        o.r[i] = r;
    }
    return o;
}

float maxStep (const Signal& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

double rms (const Signal& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}

dsp::EngineParams plain (float corrupt, CorruptType t)
{
    // Repeats 1 at 1x on the host clock: the wet is a clean delayed copy, so what changes is Corrupt.
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = 0.5f;
    p.repeats = 1;
    p.mix = 1.0f;
    p.corrupt = corrupt;
    p.corruptType = t;
    return p;
}

// Drum-ish test material: decaying noise bursts and a low sine.
Signal material (size_t n)
{
    Signal x = sine (n, 110.0, 0.35f);
    const auto nz = noise (n, 0.6f, 4);
    for (size_t i = 0; i < n; ++i)
        x[i] += nz[i] * (float) std::exp (-(double) (i % 12000) / 1500.0);
    return x;
}
} // namespace

TEST_CASE ("Corrupt at 0, or DJ Filter at its centre, leaves the output bit-identical", "[stage4][bypass]")
{
    const auto in = material ((size_t) (3.0 * kSr));
    const auto ref = render (in, plain (0.0f, CorruptType::decimate), fixedBlocks (512));
    for (auto t : kAll)
    {
        const float knob = t == CorruptType::djFilter ? 0.5f : 0.0f;
        INFO (name (t));
        const auto out = render (in, plain (knob, t), randomBlocks());
        CHECK (out.l == ref.l);
        CHECK (out.r == ref.r);
    }
    const auto edge = render (in, plain (0.5f + 0.019f, CorruptType::djFilter), fixedBlocks (512));   // inside the dead zone
    CHECK (edge.l == ref.l);
}

TEST_CASE ("Bypassed Corrupt does no work", "[stage4][bypass]")
{
    dsp::Corrupt c;
    c.prepare (kSr);
    c.setParams (0.6f, CorruptType::destroy, dsp::CorruptSet::all);
    c.reset();
    float l = 0.3f, r = 0.3f;
    c.process (l, r);
    CHECK_FALSE (c.idle());
    c.setParams (0.0f, CorruptType::destroy, dsp::CorruptSet::all);
    for (int i = 0; i < (int) (0.05 * kSr); ++i) c.process (l, r);   // past the 30 ms fade
    CHECK (c.idle());
    CHECK (c.playing() == -1);
}

TEST_CASE ("Every effect at 25/50/75/100% is finite, bounded and audible", "[stage4][safety]")
{
    const auto in = material ((size_t) (4.0 * kSr));
    for (auto t : kAll)
        for (float k : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            INFO (name (t) << " at " << k);
            const auto o = runCorrupt (in, t, [k] (size_t) { return k; });
            float peak = 0.0f;
            double diff = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                REQUIRE (std::isfinite (o.l[i]));
                REQUIRE (std::isfinite (o.r[i]));
                peak = std::max ({ peak, std::abs (o.l[i]), std::abs (o.r[i]) });
                diff += (double) (o.l[i] - in[i]) * (o.l[i] - in[i]);
            }
            CHECK (peak < 2.0f);
            if (! (t == CorruptType::djFilter && k == 0.5f))   // DJ Filter's centre is bypass
                CHECK (std::sqrt (diff / (double) in.size()) > 0.003);
        }
}

TEST_CASE ("Destroy: line-level input at full drive stays under -1 dBFS and near its input level", "[stage4][destroy][safety]")
{
    // Full-scale sine plus noise: as hot as line level gets.
    auto in = sine ((size_t) (3.0 * kSr), 220.0, 0.9f);
    const auto nz = noise (in.size(), 0.1f);
    for (size_t i = 0; i < in.size(); ++i) in[i] += nz[i];
    const double inRms = rms (in, (size_t) kSr, in.size());
    for (float k = 0.05f; k <= 1.0001f; k += 0.05f)
    {
        INFO ("knob " << k);
        const auto o = runCorrupt (in, CorruptType::destroy, [k] (size_t) { return k; });
        float peak = 0.0f;
        for (size_t i = 0; i < in.size(); ++i) peak = std::max ({ peak, std::abs (o.l[i]), std::abs (o.r[i]) });
        CHECK (peak <= dsp::Destroy::kCeiling);
        const double ratioDb = 20.0 * std::log10 (rms (o.l, (size_t) kSr, in.size()) / inRms);
        CHECK (ratioDb > -9.0);
        CHECK (ratioDb < 0.5);
    }
    // A quiet input isn't pumped up either.
    const auto quiet = sine ((size_t) (2.0 * kSr), 220.0, 0.05f);
    const auto oq = runCorrupt (quiet, CorruptType::destroy, [] (size_t) { return 0.5f; });
    CHECK (rms (oq.l, (size_t) kSr, quiet.size()) < 1.06 * rms (quiet, (size_t) kSr, quiet.size()));
}

TEST_CASE ("Sweeping the knob 0 -> 100% -> 0 doesn't click", "[stage4][sweep]")
{
    // A click is a step bigger than the effect itself makes when held still. So: hold the knob at 41
    // positions and note the largest sample-to-sample step at each, then sweep over 6 s and check that
    // every step is within what the held knob makes nearby (+25% and a small floor).
    const auto in = sine ((size_t) (6.0 * kSr), 110.0, 0.5f);
    const size_t n = in.size();
    for (auto t : kAll)
    {
        INFO (name (t));
        std::vector<float> held;
        for (int i = 0; i <= 40; ++i)
        {
            const float k = (float) i / 40.0f;
            const auto o = runCorrupt (Signal (in.begin(), in.begin() + (size_t) (0.6 * kSr)), t, [k] (size_t) { return k; });
            held.push_back (std::max (maxStep (o.l, (size_t) (0.1 * kSr), o.l.size()), maxStep (o.r, (size_t) (0.1 * kSr), o.r.size())));
        }
        const auto knob = [n] (size_t i) { const double x = (double) i / (double) n; return (float) (x < 0.5 ? 2.0 * x : 2.0 - 2.0 * x); };
        const auto o = runCorrupt (in, t, knob);
        int clicks = 0;
        for (size_t i = 1; i < n; ++i)
        {
            const int c = (int) std::lround (knob (i) * 40.0f);
            const float allowed = 1.25f * *std::max_element (held.begin() + std::max (0, c - 2), held.begin() + std::min (41, c + 3)) + 0.01f;
            clicks += std::abs (o.l[i] - o.l[i - 1]) > allowed || std::abs (o.r[i] - o.r[i - 1]) > allowed;
        }
        CHECK (clicks == 0);
    }
}

TEST_CASE ("Switching type while playing crossfades instead of clicking", "[stage4][sweep]")
{
    const auto in = sine ((size_t) (3.0 * kSr), 110.0, 0.5f);
    const auto typeAt = [] (size_t i) { return kAll[(i / 6000) % 5]; };   // a new type every 125 ms
    for (float k : { 0.3f, 0.8f })
    {
        INFO ("knob " << k);
        float worstHeld = 0.0f;
        for (auto t : kAll)
        {
            const auto h = runCorrupt (Signal (in.begin(), in.begin() + (size_t) (0.6 * kSr)), t, [k] (size_t) { return k; });
            worstHeld = std::max (worstHeld, maxStep (h.l, (size_t) (0.1 * kSr), h.l.size()));
        }
        const auto o = runCorrupt (in, CorruptType::decimate, [k] (size_t) { return k; }, 9, dsp::CorruptSet::all, typeAt);
        CHECK (maxStep (o.l, 1, o.l.size()) <= 1.25f * worstHeld + 0.01f);
    }
}

TEST_CASE ("Decimate: 12 presets, one per twelfth of the knob, trending from faint to blown out", "[stage4][decimate]")
{
    CHECK (dsp::Corrupt::decimatePreset (0.001f) == 0);
    CHECK (dsp::Corrupt::decimatePreset (1.0f / 12.0f + 0.001f) == 1);
    CHECK (dsp::Corrupt::decimatePreset (1.0f) == 11);
    auto in = sine ((size_t) (1.5 * kSr), 220.0, 0.4f);   // tonal, like a held chord
    const auto hi = sine (in.size(), 1700.0, 0.15f);
    for (size_t i = 0; i < in.size(); ++i) in[i] += hi[i];
    std::vector<double> err;
    for (int p = 0; p < 12; ++p)
    {
        const float k = ((float) p + 0.5f) / 12.0f;
        const auto o = runCorrupt (in, CorruptType::decimate, [k] (size_t) { return k; });
        // Distortion regardless of level: what's left after the best-fitting gain, relative to the output.
        double xo = 0.0, xx = 0.0, oo = 0.0;
        for (size_t i = (size_t) (0.1 * kSr); i < in.size(); ++i) { xo += (double) in[i] * o.l[i]; xx += (double) in[i] * in[i]; oo += (double) o.l[i] * o.l[i]; }
        err.push_back (1.0 - xo * xo / (xx * oo));
    }
    for (double e : err) UNSCOPED_INFO ("distortion " << e);
    CHECK (err.back() > 10.0 * err.front());
    // Shuffled, not monotonic, but trending: the later half distorts more than the first half.
    double first = 0.0, second = 0.0;
    for (int p = 0; p < 6; ++p) { first += err[(size_t) p]; second += err[(size_t) p + 6]; }
    CHECK (second > 3.0 * first);
}

TEST_CASE ("Dropout: left is few long dropouts, right is many short ones; both channels together", "[stage4][dropout]")
{
    const Signal in ((size_t) (60.0 * kSr), 0.5f);
    const auto stats = [&] (float k) {
        const auto o = runCorrupt (in, CorruptType::dropout, [k] (size_t) { return k; });
        int events = 0;
        size_t mutedSamples = 0;
        bool muted = false;
        for (size_t i = 0; i < in.size(); ++i)
        {
            REQUIRE (o.l[i] == o.r[i]);
            const bool m = o.l[i] < 0.25f;
            events += m && ! muted;
            mutedSamples += m;
            muted = m;
        }
        return std::pair<int, double> { events, events > 0 ? (double) mutedSamples / events / kSr : 0.0 };
    };
    const auto [fewN, fewLen] = stats (0.15f);
    const auto [manyN, manyLen] = stats (0.9f);
    INFO ("left " << fewN << " x " << fewLen << " s, right " << manyN << " x " << manyLen << " s");
    CHECK (fewN >= 10);
    CHECK (fewN < 60);
    CHECK (fewLen > 0.3);
    CHECK (manyN > 300);
    CHECK (manyLen < 0.05);
}

TEST_CASE ("DJ Filter: left of centre low-passes, right high-passes, further is stronger", "[stage4][djfilter]")
{
    const auto in = noise ((size_t) (1.0 * kSr), 0.3f);
    const auto lowAndHigh = [&] (float k) {
        const auto o = runCorrupt (in, CorruptType::djFilter, [k] (size_t) { return k; });
        double lo = 0.0, hi = 0.0, s = 0.0;   // a crude split: a 1-pole low band and the difference signal
        for (size_t i = (size_t) (0.1 * kSr); i < in.size(); ++i)
        {
            s += (o.l[i] - s) * 0.02;
            lo += s * s;
            hi += (double) (o.l[i] - o.l[i - 1]) * (o.l[i] - o.l[i - 1]);
        }
        return std::pair<double, double> { lo, hi };
    };
    const auto [lo0, hi0] = lowAndHigh (0.5f);
    const auto [loLp, hiLp] = lowAndHigh (0.2f);
    const auto [loLp2, hiLp2] = lowAndHigh (0.05f);
    const auto [loHp, hiHp] = lowAndHigh (0.8f);
    const auto [loHp2, hiHp2] = lowAndHigh (0.95f);
    CHECK (hiLp < 0.2 * hi0);
    CHECK (hiLp2 < hiLp);
    CHECK (loHp < 0.5 * lo0);
    CHECK (loHp2 < loHp);
}

TEST_CASE ("Vinyl: crackle on silence, a narrow band and audible wow at full", "[stage4][vinyl]")
{
    const Signal silence ((size_t) (4.0 * kSr), 0.0f);
    const auto quiet = runCorrupt (silence, CorruptType::vinyl, [] (size_t) { return 0.25f; });
    const auto loud = runCorrupt (silence, CorruptType::vinyl, [] (size_t) { return 1.0f; });
    CHECK (rms (quiet.l, 0, silence.size()) > 1.0e-4);
    CHECK (rms (loud.l, 0, silence.size()) > 3.0 * rms (quiet.l, 0, silence.size()));
    CHECK (quiet.l != quiet.r);   // crackle is separate per channel

    // Band: 60 Hz and 9 kHz lose far more than 1 kHz at full.
    const auto level = [] (double hz) {
        const auto in = sine ((size_t) (1.0 * kSr), hz, 0.5f);
        const auto o = runCorrupt (in, CorruptType::vinyl, [] (size_t) { return 1.0f; });
        return rms (o.l, (size_t) (0.3 * kSr), in.size()) / rms (in, (size_t) (0.3 * kSr), in.size());
    };
    const double mid = level (1000.0);
    CHECK (level (60.0) < 0.5 * mid);
    CHECK (level (9000.0) < 0.2 * mid);

    // Wow: a 1 kHz tone's pitch wanders by more than ±0.3% at full, under 0.1% at 10%.
    const auto wander = [] (float k) {
        const auto in = sine ((size_t) (4.0 * kSr), 1000.0, 0.5f);
        const auto o = runCorrupt (in, CorruptType::vinyl, [k] (size_t) { return k; });
        double lo = 1e9, hi = 0.0;
        for (double t = 0.5; t + 0.1 <= 3.9; t += 0.1)
        {
            const double f = peakHz (o.l, t, 0.1, 1000.0);
            lo = std::min (lo, f);
            hi = std::max (hi, f);
        }
        return (hi - lo) / 1000.0;
    };
    CHECK (wander (1.0f) > 0.006);
    CHECK (wander (0.1f) < 0.003);
}

TEST_CASE ("Corrupt Set: Original 3 plays only Decimate, Dropout, Destroy", "[stage4][set]")
{
    using S = dsp::CorruptSet;
    CHECK (dsp::Corrupt::effectiveType (CorruptType::djFilter, S::original3) == CorruptType::decimate);
    CHECK (dsp::Corrupt::effectiveType (CorruptType::vinyl, S::original3) == CorruptType::decimate);
    CHECK (dsp::Corrupt::effectiveType (CorruptType::destroy, S::original3) == CorruptType::destroy);
    CHECK (dsp::Corrupt::effectiveType (CorruptType::vinyl, S::all) == CorruptType::vinyl);
    CorruptType t = CorruptType::decimate;
    std::vector<int> seen;
    for (int i = 0; i < 6; ++i) { t = dsp::Corrupt::nextType (t, S::original3); seen.push_back ((int) t); }
    CHECK (seen == std::vector<int> { 1, 2, 0, 1, 2, 0 });
    seen.clear();
    for (int i = 0; i < 5; ++i) { t = dsp::Corrupt::nextType (t, S::all); seen.push_back ((int) t); }
    CHECK (seen == std::vector<int> { 1, 2, 3, 4, 0 });

    const auto in = material ((size_t) (1.0 * kSr));
    const auto vinyl3 = render (in, [] { auto p = plain (0.6f, CorruptType::vinyl); p.corruptSet = S::original3; return p; }(), fixedBlocks (512));
    const auto dec = render (in, plain (0.6f, CorruptType::decimate), fixedBlocks (512));
    CHECK (vinyl3.l == dec.l);
}

TEST_CASE ("With Corrupt on, the same seed is still bit-identical at any block size", "[stage4][seed]")
{
    const auto in = material ((size_t) (3.0 * kSr));
    for (auto t : { CorruptType::dropout, CorruptType::vinyl, CorruptType::decimate })
    {
        INFO (name (t));
        auto p = plain (0.9f, t);
        p.seed = 42;
        // Knob automation mid-way (on a block boundary of both), so preset changes and fades are covered.
        const auto move = [] (dsp::EngineParams& q, size_t pos) { q.corrupt = pos < 144 * 512 ? 0.9f : 0.35f; };
        const auto a = render (in, p, fixedBlocks (1), 120.0, true, move);
        const auto b = render (in, p, fixedBlocks (512), 120.0, true, move);
        CHECK (a.l == b.l);
        CHECK (a.r == b.r);
        const auto c = render (in, p, fixedBlocks (1));
        const auto d = render (in, p, randomBlocks (3));
        CHECK (c.l == d.l);
        CHECK (c.r == d.r);
        p.seed = 43;
        if (t != CorruptType::decimate)
            CHECK (render (in, p, fixedBlocks (512), 120.0, true, move).l != a.l);
    }
}

TEST_CASE ("Corrupt is finite and bounded at 44.1 to 192 kHz", "[stage4][safety]")
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
        for (auto t : kAll)
            for (float k : { 0.02f, 0.5f, 1.0f })
            {
                INFO (name (t) << " at " << k << ", " << sr << " Hz");
                dsp::Corrupt c;
                c.prepare (sr);
                c.reseed (5);
                c.setParams (k, t, dsp::CorruptSet::all);
                c.reset();
                float peak = 0.0f;
                for (int i = 0; i < (int) sr; ++i)
                {
                    float l = 0.8f * (float) std::sin (2.0 * std::numbers::pi * 220.0 * i / sr), r = (i % 97) / 97.0f - 0.5f;
                    c.process (l, r);
                    REQUIRE (std::isfinite (l));
                    REQUIRE (std::isfinite (r));
                    peak = std::max ({ peak, std::abs (l), std::abs (r) });
                }
                CHECK (peak < 2.0f);
            }
}
