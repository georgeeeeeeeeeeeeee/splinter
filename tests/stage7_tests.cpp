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
