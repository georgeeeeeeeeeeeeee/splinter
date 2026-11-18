// Stage 2: Micro mode. Bend speed and reverse, note pitch as speed, Traverse, Silence, per-mode toggles.
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

using namespace fault;
using namespace fault::test;

namespace
{
dsp::EngineParams micro (int step = 0, int repeats = 1)
{
    dsp::EngineParams p;
    p.clock = dsp::ClockSource::host;
    p.time = (float) step / 8.0f;   // step 0 = /16 = 16 beats = 8 s at 120 BPM
    p.repeats = repeats;
    p.mix = 1.0f;
    p.mode = dsp::Mode::micro;
    return p;
}
double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }
} // namespace

TEST_CASE ("Note pitch plays the buffer chromatically and in tune across +/- 3 octaves", "[stage2][pitch]")
{
    // A steady 440 Hz into an 8 s window; notes C0 .. C6 (MIDI 24 .. 96) every 3 semitones, 0.6 s each,
    // skipping the gate notes C1 .. F1 (36 .. 41), which never set the pitch.
    const Signal in = sine ((size_t) (26.0 * kSr), 440.0, 0.4f);
    auto p = micro();
    p.noteToSpeed = true;
    std::vector<Note> notes;
    double t = 8.2;
    const auto isGate = [] (int n) { return n >= 36 && n <= 41; };
    for (int n = 24; n <= 96; n += 3, t += 0.6)
    {
        if (isGate (n)) continue;
        notes.push_back ({ t, n, true });
        notes.push_back ({ t + 0.55, n, false });
    }
    const auto r = render (in, p, randomBlocks(), 120.0, true, {}, notes);
    double worst = 0.0;
    t = 8.2;
    int measured = 0;
    for (int n = 24; n <= 96; n += 3, t += 0.6)
    {
        if (isGate (n)) continue;
        ++measured;
        const double expected = 440.0 * std::exp2 ((n - 60) / 12.0);
        const double got = peakHz (r.l, t + 0.12, 0.4, expected);
        INFO ("note " << n << ": expected " << expected << " Hz, got " << got << " (" << cents (got, expected) << " cents)");
        CHECK (std::abs (cents (got, expected)) < 2.0);
        worst = std::max (worst, std::abs (cents (got, expected)));
    }
    std::printf ("chromatic tracking: worst error %.3f cents over %d notes, -3 .. +3 octaves\n", worst, measured);
}

TEST_CASE ("Bend spans -3 .. +3 octaves and flags exact octaves", "[stage2][bend]")
{
    const Signal in = noise ((size_t) (1.0 * kSr));
    for (auto [bend, ratio] : { std::pair { 0.0f, 0.125f }, { 0.5f, 1.0f }, { 1.0f, 8.0f }, { 4.0f / 6.0f, 2.0f } })
    {
        auto p = micro();
        p.bend = bend;
        dsp::Engine e;
        e.setParams (p);
        e.prepare (kSr, 512);
        Signal out (512);
        e.process (in.data(), in.data(), out.data(), out.data(), 512);
        INFO ("bend " << bend);
        CHECK (std::abs (e.status().speed.load() - ratio) < 1.0e-4f * ratio);
        CHECK (e.status().onOctave.load());
    }
    auto p = micro();
    p.bend = 0.53f;   // between octaves
    dsp::Engine e;
    e.setParams (p);
    e.prepare (kSr, 512);
    Signal out (512);
    e.process (in.data(), in.data(), out.data(), out.data(), 512);
    CHECK_FALSE (e.status().onOctave.load());
}

TEST_CASE ("Micro Bend toggle reverses playback", "[stage2][reverse]")
{
    // A rising ramp: forward playback rises, reverse falls.
    Signal ramp ((size_t) (20.0 * kSr));
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) ((i % 48000) / 48000.0) - 0.5f;
    auto p = micro (4);   // x1: 0.5 s windows
    p.windowing = 0.0f;
    for (bool reverse : { false, true })
    {
        p.bendOnMicro = reverse;
        const auto r = render (ramp, p, fixedBlocks (256));
        int up = 0, down = 0;
        for (size_t i = (size_t) (2.0 * kSr); i < (size_t) (10.0 * kSr); ++i)
            (r.l[i] > r.l[i - 1] ? up : down)++;
        INFO ((reverse ? "reverse" : "forward") << ": up " << up << ", down " << down);
        CHECK ((reverse ? down > 20 * up : up > 20 * down));
    }
}

TEST_CASE ("Switching mode never leaks pitch or reverse", "[stage2][modes]")
{
    // Micro: reversed at +2 octaves. Switch to Macro: 1x forward. Back to Micro: reversed +2 octaves again.
    const Signal in = noise ((size_t) (3.0 * kSr));
    auto p = micro (4);
    p.bend = 5.0f / 6.0f;
    p.bendOnMicro = true;
    p.noteToSpeed = true;
    const auto r = render (in, p, fixedBlocks (480), 120.0, true, [] (dsp::EngineParams& q, size_t pos) {
        q.mode = pos >= (size_t) kSr && pos < (size_t) (2.0 * kSr) ? dsp::Mode::macro : dsp::Mode::micro;
    }, { { 0.5, 72, true } });   // a note an octave up, too
    const auto at = [&r] (double t) { return r.speeds[(size_t) (t * kSr / 480.0)]; };
    CHECK (std::abs (at (0.9) - -8.0f) < 1.0e-3f);   // Micro: -(2^(2 + 1))
    CHECK (at (1.5) == 1.0f);                        // Macro: exactly 1x forward
    CHECK (std::abs (at (2.5) - -8.0f) < 1.0e-3f);   // Micro state restored, glide-free
}

TEST_CASE ("Per-mode toggles: the Macro Bend toggle doesn't reverse Micro, and vice versa", "[stage2][modes]")
{
    const Signal in = noise ((size_t) (1.0 * kSr));
    auto p = micro();
    p.bendOnMacro = true;    // stored for Macro only
    dsp::Engine e;
    e.setParams (p);
    e.prepare (kSr, 256);
    Signal out (256);
    e.process (in.data(), in.data(), out.data(), out.data(), 256);
    CHECK (e.status().speed.load() > 0.0f);
}

TEST_CASE ("Traverse picks the slice; it does nothing at Repeats 1", "[stage2][traverse]")
{
    const Signal in = noise ((size_t) (8.0 * kSr));
    auto p = micro (4, 8);
    for (int k = 0; k < 8; ++k)
    {
        p.brk = ((float) k + 0.5f) / 8.0f;
        dsp::Engine e;
        e.setParams (p);
        e.prepare (kSr, 512);
        Signal out (512);
        for (int b = 0; b < 200; ++b)
        {
            e.setTransport ({ true, true, 120.0, b * 512 / kSr * 2.0 });
            e.process (in.data() + b * 512, in.data() + b * 512, out.data(), out.data(), 512);
        }
        CHECK (e.status().slice.load() == k);
    }
    auto one = micro (4, 1);
    const auto a = render (in, one, fixedBlocks (512));
    one.brk = 0.9f;
    const auto b = render (in, one, fixedBlocks (512));
    CHECK (a.l == b.l);
}

TEST_CASE ("Silence replaces up to 90% of each slice", "[stage2][silence]")
{
    const Signal in = sine ((size_t) (8.0 * kSr), 330.0, 0.5f);
    for (float brk : { 0.0f, 0.5f, 1.0f })
    {
        auto p = micro (5, 4);   // x2: 0.25 s windows, 4 slices
        p.breakOnMicro = true;
        p.brk = brk;
        const auto r = render (in, p, fixedBlocks (512));
        // Fraction of 2 ms frames that are silent.
        int silent = 0, frames = 0;
        for (size_t f = (size_t) (1.0 * kSr); f + 96 < (size_t) (7.0 * kSr); f += 96, ++frames)
        {
            double e = 0;
            for (size_t i = f; i < f + 96; ++i) e += r.l[i] * r.l[i];
            silent += e < 1.0e-8 ? 1 : 0;
        }
        const double frac = (double) silent / frames;
        INFO ("break " << brk << ": silent " << frac);
        CHECK (std::abs (frac - 0.9 * brk) < 0.06);
    }
}

TEST_CASE ("Gate notes never set the pitch", "[stage2][pitch]")
{
    const Signal in = noise ((size_t) (2.0 * kSr));
    auto p = micro (4);
    p.noteToSpeed = true;
    const auto r = render (in, p, fixedBlocks (480), 120.0, true, {}, { { 0.5, 36, true }, { 0.6, 41, true } });   // C1, F1
    CHECK (r.speeds.back() == 1.0f);
}

TEST_CASE ("Micro output doesn't depend on block sizes, notes included", "[stage2][blocks]")
{
    const Signal in = noise ((size_t) (4.0 * kSr));
    auto p = micro (5, 4);
    p.noteToSpeed = true;
    p.windowing = 0.05f;
    const std::vector<Note> notes { { 0.31, 64, true }, { 1.177, 55, true }, { 2.5, 70, true } };
    const auto a = render (in, p, fixedBlocks (1), 120.0, true, [] (dsp::EngineParams& q, size_t pos) { q.bendOnMicro = pos > (size_t) (2.0 * kSr); }, notes);
    const auto b = render (in, p, randomBlocks (5), 120.0, true, [] (dsp::EngineParams& q, size_t pos) { q.bendOnMicro = pos > (size_t) (2.0 * kSr); }, notes);
    // Parameter changes land per block, so compare away from the toggle; notes are sample-accurate.
    for (size_t i = 0; i < (size_t) (2.0 * kSr); ++i)
        REQUIRE (a.l[i] == b.l[i]);
}
