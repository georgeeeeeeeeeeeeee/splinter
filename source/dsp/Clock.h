#pragma once
// Clock (brief §3.2). Internal: period follows Time exponentially, 16 s at 0 to 80 Hz at 1. Host:
// Time picks a division / multiplication of the beat (/16 … ×8, SPEC_NOTES N2), ticks sample-accurate
// and phase-locked to the host's beat position, re-synced on transport start and loop jumps. With the
// transport stopped it free-runs at the host tempo. Note: each note-on of the Note clock gate note is
// a pulse; the interval between pulses is measured and the same div/mult applied (×N: N ticks per
// pulse, evenly spaced; /N: every Nth pulse). If no pulse arrives for four pulse intervals it carries on
// at the last rate, on the same grid, until pulses return.
// Reset: Internal ticks at once (a new window); Host realigns the division to the next beat; Note
// realigns the division counter so the next pulse ticks.

#include <array>
#include <cstdint>

namespace fault::dsp
{
enum class ClockSource { internal = 0, host = 1, note = 2 };

struct HostTransport
{
    bool valid = false;     // the host gave tempo + position
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;       // beat position of the block's first sample
};

class Clock
{
public:
    // Ticks per beat for each host step, slowest first.
    static constexpr std::array<double, 9> kRates { 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 3.0, 4.0, 8.0 };
    static constexpr std::array<const char*, 9> kRateNames { "/16", "/8", "/4", "/2", "x1", "x2", "x3", "x4", "x8" };
    static double internalPeriodSeconds (float time) noexcept;   // 16 s .. 12.5 ms
    static int hostStep (float time) noexcept;                   // 0 .. 8

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setSource (ClockSource s) noexcept { source_ = s; }
    void setTime (float t) noexcept { time_ = t; }

    // Call once per block before ticks are read: works out every tick offset in [0, n). `pulse`: a Note
    // clock pulse on the block's first sample (the engine splits blocks at note events).
    void beginBlock (const HostTransport& t, int n, bool pulse = false) noexcept;
    // Ticks in this block, ascending sample offsets.
    int numTicks() const noexcept { return numTicks_; }
    int tickAt (int i) const noexcept { return ticks_[(size_t) i]; }
    // The tick's index: its grid position k (tick at beat k / rate) while the host plays, else a count
    // since the last reset / resync. Macro mode keys its dice on this when the seed is fixed.
    int64_t tickIndex (int i) const noexcept { return index_[(size_t) i]; }

    double periodSamples() const noexcept { return period_; }   // current period
    int step() const noexcept { return step_; }
    uint32_t stepChanges() const noexcept { return stepChanges_; }   // UI flag: div/mult step changed
    void resync() noexcept { resync_ = true; }                       // Reset: next pulse / now (internal)
    void transportStarted() noexcept { origin_ = 0.0; }              // host grid back on the bar

private:
    void addTick (int offset, int64_t index) noexcept;
    void noteBlock (const HostTransport& t, int n, bool pulse) noexcept;

    double sr_ = 48000.0, phase_ = 0.0, period_ = 48000.0, lastPpqEnd_ = -1.0;
    float time_ = 0.5f;
    ClockSource source_ = ClockSource::internal;
    bool wasPlaying_ = false, resync_ = false;
    int step_ = -1;
    uint32_t stepChanges_ = 0;
    static constexpr int kMaxTicks = 4096;
    std::array<int, kMaxTicks> ticks_ {};
    std::array<int64_t, kMaxTicks> index_ {};
    int64_t freeCount_ = 0;
    double origin_ = 0.0;                 // host grid origin, in beats (moved by Reset)
    // Note clock.
    int64_t now_ = 0, lastPulse_ = -1;    // samples
    double interval_ = 0.0, lastTick_ = 0.0;
    int pulseCount_ = 0, subLeft_ = 0;
    bool noteFree_ = false;
    int numTicks_ = 0;
};
} // namespace fault::dsp
