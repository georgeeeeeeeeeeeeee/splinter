// Stage 7: the output safety clipper.
#include "dsp/Engine.h"
#include "dsp/SafetyClip.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace fault;

TEST_CASE ("Safety clip: untouched to -1 dBFS, never past 0 dBFS, smooth and odd", "[stage7][clip]")
{
    using C = dsp::SafetyClip;
    float last = 0.0f;
    for (int i = 0; i <= 200000; ++i)
    {
        const float x = (float) i * 1.0e-4f;   // 0 .. 20 (+26 dBFS)
        const float y = C::apply (x);
        if (x <= C::kKnee) REQUIRE (y == x);
        REQUIRE (y <= 1.0f);
        REQUIRE (y >= last);                   // monotonic
        REQUIRE (C::apply (-x) == -y);
        last = y;
    }
    // Slope is continuous at the knee (no corner).
    const float h = 1.0e-4f;
    CHECK (std::abs ((C::apply (C::kKnee + h) - C::apply (C::kKnee)) / h - 1.0f) < 0.01f);
}

TEST_CASE ("Engine: the clipper caps a hot input, and off is a straight bypass", "[stage7][clip]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;
    std::vector<float> in (n), outL (n), outR (n);
    for (int i = 0; i < n; ++i) in[(size_t) i] = 4.0f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * i / sr);   // +12 dBFS
    for (bool clip : { true, false })
    {
        dsp::Engine e;
        dsp::EngineParams p;
        p.mix = 0.0f;   // dry only
        p.safetyClip = clip;
        e.setParams (p);
        e.prepare (sr, 512);
        for (int i = 0; i < n; i += 512)
            e.process (in.data() + i, in.data() + i, outL.data() + i, outR.data() + i, std::min (512, n - i));
        float peak = 0.0f, err = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            peak = std::max (peak, std::abs (outL[(size_t) i]));
            err = std::max (err, std::abs (outL[(size_t) i] - in[(size_t) i]));
        }
        if (clip)
        {
            CHECK (peak <= 1.0f);
            CHECK (e.status().clips.load() > 0);
        }
        else
            CHECK (err < 1.0e-5f);
    }
}

TEST_CASE ("Engine: the output trim scales dry and wet, glides, and is exact at 0 dB", "[stage7][output]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;
    std::vector<float> in (n), outL (n), outR (n);
    for (int i = 0; i < n; ++i) in[(size_t) i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * i / sr);
    const auto run = [&] (float db, float newDb = 1.0e9f) {
        dsp::Engine e;
        dsp::EngineParams p;
        p.mix = 0.0f;
        p.outputDb = db;
        e.setParams (p);
        e.prepare (sr, 512);
        bool changed = false;
        for (int i = 0; i < n; i += 512)
        {
            if (! changed && i >= n / 2 && newDb < 1.0e8f) { p.outputDb = newDb; e.setParams (p); changed = true; }
            e.process (in.data() + i, in.data() + i, outL.data() + i, outR.data() + i, std::min (512, n - i));
        }
    };
    run (0.0f);
    for (int i = 0; i < n; ++i) REQUIRE (outL[(size_t) i] == in[(size_t) i]);                 // bit-exact
    run (-6.0f);
    for (int i = 0; i < n; ++i) REQUIRE (std::abs (outL[(size_t) i] - 0.501187f * in[(size_t) i]) < 1.0e-6f);
    run (0.0f, -12.0f);                                                                         // a change glides
    CHECK (std::abs (outL[(size_t) n - 100] - 0.251189f * in[(size_t) n - 100]) < 1.0e-5f);    // settled at -12 dB
    const int at = ((n / 2 + 511) / 512) * 512 + 48;                                              // 1 ms after the change
    CHECK (std::abs (outL[(size_t) at] / in[(size_t) at]) > 0.8f);                              // not a jump
}
