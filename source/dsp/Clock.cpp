#include "dsp/Clock.h"

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
double Clock::internalPeriodSeconds (float time) noexcept
{
    const double t = std::clamp ((double) time, 0.0, 1.0);
    return 16.0 * std::pow (1.0 / (16.0 * 80.0), t);   // 16 s .. 1/80 s
}

int Clock::hostStep (float time) noexcept
{
    return std::clamp ((int) std::lround (std::clamp (time, 0.0f, 1.0f) * 8.0f), 0, 8);
}

void Clock::prepare (double sampleRate) noexcept
{
    sr_ = sampleRate;
    reset();
}

void Clock::reset() noexcept
{
    phase_ = 0.0;
    wasPlaying_ = false;
    lastPpqEnd_ = -1.0;
    resync_ = false;
    numTicks_ = 0;
    freeCount_ = 0;
}

void Clock::addTick (int offset, int64_t index) noexcept
{
    if (numTicks_ < kMaxTicks && (numTicks_ == 0 || offset > ticks_[(size_t) numTicks_ - 1]))
    {
        ticks_[(size_t) numTicks_] = offset;
        index_[(size_t) numTicks_++] = index;
    }
}

void Clock::beginBlock (const HostTransport& t, int n) noexcept
{
    numTicks_ = 0;
    const bool synced = source_ != ClockSource::internal;
    if (synced)
    {
        const int s = hostStep (time_);
        if (step_ >= 0 && s != step_)
            ++stepChanges_;
        step_ = s;
    }

    const double bpm = t.valid && t.bpm > 1.0 ? t.bpm : 120.0;
    const double rate = kRates[(size_t) std::max (0, step_)];
    period_ = synced ? sr_ * 60.0 / bpm / rate : internalPeriodSeconds (time_) * sr_;

    if (synced && t.valid && t.playing)
    {
        // Ticks on the host's beat grid: tick k sits at beat k / rate. The first sample of the block is
        // included when it lands on the grid; a crossing exactly at the next block's first sample is
        // left to that block, so nothing ticks twice.
        const double bps = bpm / 60.0 / sr_;
        const bool jumped = ! wasPlaying_ || resync_ || t.ppq < lastPpqEnd_ - 1.0e-6 || t.ppq > lastPpqEnd_ + 1.0e-3;
        (void) jumped;   // grid ticks need no state: a start, loop or reset simply lands on the grid
        for (auto k = (int64_t) std::ceil (t.ppq * rate - 1.0e-7);; ++k)
        {
            const int offset = (int) std::ceil (((double) k / rate - t.ppq) / bps - 1.0e-6);
            if (offset >= n)
                break;
            addTick (std::max (0, offset), k);
        }
        lastPpqEnd_ = t.ppq + n * bps;
        wasPlaying_ = true;
        resync_ = false;
        phase_ = 0.0;
        return;
    }

    // Internal, or synced with the transport stopped: free-run (at the host tempo when synced).
    wasPlaying_ = false;
    if (resync_)
    {
        resync_ = false;
        phase_ = 0.0;
        freeCount_ = 0;
        addTick (0, freeCount_++);   // Reset in internal mode: a new window right away
    }
    const double inc = 1.0 / std::max (1.0, period_);
    for (int i = 0; i < n; ++i)
    {
        phase_ += inc;
        if (phase_ >= 1.0)
        {
            phase_ -= 1.0;
            addTick (i, freeCount_++);
        }
    }
}
} // namespace fault::dsp
