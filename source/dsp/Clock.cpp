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
    origin_ = 0.0;
    now_ = 0;
    lastPulse_ = -1;
    interval_ = 0.0;
    lastTick_ = 0.0;
    pulseCount_ = subLeft_ = 0;
    noteFree_ = false;
}

void Clock::addTick (int offset, int64_t index) noexcept
{
    if (numTicks_ < kMaxTicks && (numTicks_ == 0 || offset > ticks_[(size_t) numTicks_ - 1]))
    {
        ticks_[(size_t) numTicks_] = offset;
        index_[(size_t) numTicks_++] = index;
    }
}

void Clock::noteBlock (const HostTransport& t, int n, bool pulse) noexcept
{
    // Rate as ticks per pulse: x1..x8 multiply, /2../16 divide.
    const double rate = kRates[(size_t) std::max (0, step_)];
    const int mult = rate >= 1.0 ? (int) std::lround (rate) : 1, div = rate < 1.0 ? (int) std::lround (1.0 / rate) : 1;
    if (interval_ <= 0.0)   // no pulse yet: assume one a beat at the host tempo, for the window length
        interval_ = sr_ * 60.0 / (t.valid && t.bpm > 1.0 ? t.bpm : 120.0);
    period_ = interval_ * (double) div / (double) mult;
    if (resync_)
    {
        resync_ = false;
        pulseCount_ = 0;   // the next pulse ticks
    }
    for (int i = 0; i < n; ++i)
    {
        const int64_t s = now_ + i;
        if (i == 0 && pulse)
        {
            if (lastPulse_ >= 0)
                interval_ = std::max (16.0, (double) (s - lastPulse_));
            lastPulse_ = s;
            period_ = interval_ * (double) div / (double) mult;
            noteFree_ = false;
            if (pulseCount_ % div == 0)
            {
                addTick (i, freeCount_++);
                lastTick_ = (double) s;
                subLeft_ = mult - 1;
            }
            pulseCount_ = (pulseCount_ + 1) % div;
            continue;
        }
        if (lastPulse_ < 0)
            continue;
        if (subLeft_ > 0 && (double) s >= lastTick_ + period_ - 1.0e-9)
        {
            // The multiplied ticks between pulses.
            lastTick_ += period_;
            --subLeft_;
            addTick (i, freeCount_++);
            continue;
        }
        if (! noteFree_ && (double) (s - lastPulse_) > 4.0 * interval_)
        {
            // No clock for four intervals: carry on at the last rate, on the same grid.
            noteFree_ = true;
            while (lastTick_ + period_ < (double) s - 1.0e-9) lastTick_ += period_;
        }
        if (noteFree_ && (double) s >= lastTick_ + period_ - 1.0e-9)
        {
            lastTick_ += period_;
            addTick (i, freeCount_++);
        }
    }
}

void Clock::beginBlock (const HostTransport& t, int n, bool pulse) noexcept
{
    numTicks_ = 0;
    if (source_ == ClockSource::note)
    {
        const int s = hostStep (time_);
        if (step_ >= 0 && s != step_)
            ++stepChanges_;
        step_ = s;
        noteBlock (t, n, pulse);
        now_ += n;
        wasPlaying_ = false;
        return;
    }
    now_ += n;
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
        // Reset while playing realigns the division to the next beat (a start or loop simply lands on
        // the grid; a transport start puts it back on the bar).
        if (resync_)
            origin_ = std::ceil (t.ppq - 1.0e-7);
        const double rel = t.ppq - origin_;
        for (auto k = (int64_t) std::ceil (rel * rate - 1.0e-7);; ++k)
        {
            const int offset = (int) std::ceil (((double) k / rate - rel) / bps - 1.0e-6);
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
