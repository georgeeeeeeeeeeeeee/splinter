#include "dsp/Engine.h"

#include "dsp/SliceEnvelope.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fault::dsp
{
namespace
{
constexpr float kOutputCeiling = 15.84f;   // +24 dBFS hard safety clamp
constexpr double kMaxTailSeconds = 0.02;
float sanitise (float x) noexcept { return std::isfinite (x) ? std::clamp (x, -kOutputCeiling, kOutputCeiling) : 0.0f; }
} // namespace

void Engine::prepare (double sampleRate, int)
{
    sr_ = sampleRate;
    buf_.prepare (sampleRate);
    clock_.prepare (sampleRate);
    smoothLen_ = std::max (1, (int) (0.02 * sampleRate));
    reset();
}

void Engine::reset() noexcept
{
    buf_.reset();
    clock_.reset();
    clock_.setSource (p_.clock);
    clock_.setTime (p_.time);
    win_ = {};
    tailWin_ = {};
    head_ = {};
    tailHead_ = {};
    tailLeft_ = 0.0;
    pendingRepeats_ = std::clamp (p_.repeats, 1, 32);
    freezeWanted_ = p_.freeze;
    forceWet_ = false;
    resetRequested_ = false;
    mix_ = mixTarget_ = std::clamp (p_.mix, 0.0f, 1.0f);
    width_ = widthTarget_ = std::clamp (p_.width, 0.0f, 1.0f);
    smoothLeft_ = 0;
    processed_ = 0;
    tickCount_.store (0, std::memory_order_release);
}

void Engine::setParams (const EngineParams& p) noexcept
{
    p_ = p;
    clock_.setSource (p.clock);
    clock_.setTime (p.time);
    pendingRepeats_ = std::clamp (p.repeats, 1, 32);
    freezeWanted_ = p.freeze;
    if (p.freezeMode == FreezeMode::momentary && buf_.frozen() != p.freeze)
    {
        buf_.setFrozen (p.freeze);
        forceWet_ = p.freeze && p.mix < 0.001f;
    }
    const float mt = std::clamp (p.mix, 0.0f, 1.0f), wt = std::clamp (p.width, 0.0f, 1.0f);
    if (mt != mixTarget_ || wt != widthTarget_)
    {
        mixTarget_ = mt;
        widthTarget_ = wt;
        mixStep_ = (mixTarget_ - mix_) / (float) smoothLen_;
        widthStep_ = (widthTarget_ - width_) / (float) smoothLen_;
        smoothLeft_ = smoothLen_;
    }
}

float Engine::mainGain() const noexcept
{
    return SliceEnvelope::gain (head_.progress, head_.duration (win_.sliceLength()), p_.windowing, sr_);
}

void Engine::readHead (const Window& w, const Playhead& h, float& l, float& r) const noexcept
{
    buf_.read (w.sliceStart() + h.offset, l, r);
}

void Engine::jump (const Window& next) noexcept
{
    // Mid-slice (the old head still audible), with windowing on: let the old head fade out rather than
    // cut. At a slice end its envelope is already down, and at windowing 0 cuts are meant to be hard.
    const float g = win_.valid() ? mainGain() : 0.0f;
    if (p_.windowing > 0.0f && g > 1.0e-3f)
    {
        tailWin_ = win_;
        tailHead_ = head_;
        const double fade = SliceEnvelope::fadeSamples (head_.duration (next.sliceLength()), p_.windowing, sr_);
        tailLen_ = std::clamp (fade, 32.0, kMaxTailSeconds * sr_);
        tailLeft_ = tailLen_;
        tailGain0_ = g;
    }
    win_ = next;
    head_.offset = 0.0;
    head_.progress = 0.0;
}

void Engine::onTick() noexcept
{
    // Latching freeze engages / releases on the tick (brief §3.10); momentary acts at once in setParams.
    if (p_.freezeMode == FreezeMode::latching && buf_.frozen() != freezeWanted_)
    {
        buf_.setFrozen (freezeWanted_);
        forceWet_ = freezeWanted_ && p_.mix < 0.001f;   // dry when frozen: the mangle takes over (§3.1)
    }
    const double held = (double) (buf_.written() - buf_.oldest());
    Window next;
    next.length = std::min (std::round (clock_.periodSamples()), held);   // whole samples: reads stay on the grid
    next.start = (double) buf_.written() - next.length;
    next.repeats = pendingRepeats_;
    next.slice = 0;
    if (next.valid())
        jump (next);
    else
        win_ = next;

    const uint64_t k = tickCount_.load (std::memory_order_relaxed);
    auto& e = ticks_[(size_t) (k % kTickRing)];
    e.sample = curSample_;
    e.windowStart = next.start;
    e.windowLength = next.length;
    e.period = clock_.periodSamples();
    e.repeats = next.repeats;
    e.frozen = buf_.frozen();
    tickCount_.store (k + 1, std::memory_order_release);
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    if (resetRequested_)
    {
        resetRequested_ = false;
        clock_.resync();
    }
    clock_.beginBlock (transport_, n);
    int nextTick = 0;
    const int numTicks = clock_.numTicks();

    for (int i = 0; i < n; ++i)
    {
        const float xl = std::isfinite (inL[i]) ? inL[i] : 0.0f;
        const float xr = std::isfinite (inR[i]) ? inR[i] : 0.0f;
        // A tick captures the most recent `period` samples up to (not including) this one, then this
        // sample is recorded: the playhead's first read is exactly one period back.
        curSample_ = processed_ + i;
        while (nextTick < numTicks && clock_.tickAt (nextTick) == i)
        {
            onTick();
            ++nextTick;
        }
        buf_.write (xl, xr);

        float wl = 0.0f, wr = 0.0f;
        if (win_.valid())
        {
            float l, r;
            readHead (win_, head_, l, r);
            const float g = mainGain();
            wl = l * g;
            wr = r * g;
            if (head_.advance (win_.sliceLength()) && pendingRepeats_ != win_.repeats)
            {
                // Repeats changes take effect at a slice boundary.
                win_.repeats = pendingRepeats_;
                win_.slice = std::min (win_.slice, win_.repeats - 1);
                head_.offset = 0.0;
            }
        }
        if (tailLeft_ > 0.0)
        {
            float l, r;
            readHead (tailWin_, tailHead_, l, r);
            const float g = tailGain0_ * SliceEnvelope::rise (tailLeft_ / tailLen_);
            wl += l * g;
            wr += r * g;
            tailHead_.advance (tailWin_.sliceLength());
            tailLeft_ -= 1.0;
        }

        if (smoothLeft_ > 0)
        {
            mix_ += mixStep_;
            width_ += widthStep_;
            if (--smoothLeft_ == 0)
            {
                mix_ = mixTarget_;
                width_ = widthTarget_;
            }
        }
        // Width: M/S on the wet, side x0 .. x2 (50% = unchanged).
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * (2.0f * width_);
        wl = mid + side;
        wr = mid - side;
        // Equal-power mix, exact at the ends (fully dry is the input, fully wet has none of it).
        const float m = forceWet_ ? 1.0f : mix_;
        const float dry = m <= 0.0f ? 1.0f : m >= 1.0f ? 0.0f : std::cos (m * 0.5f * (float) std::numbers::pi);
        const float wet = m <= 0.0f ? 0.0f : m >= 1.0f ? 1.0f : std::sin (m * 0.5f * (float) std::numbers::pi);
        outL[i] = sanitise (xl * dry + wl * wet);
        outR[i] = sanitise (xr * dry + wr * wet);
    }
    processed_ += n;
}
} // namespace fault::dsp

// ---- C API -------------------------------------------------------------------
struct fault_engine
{
    fault::dsp::Engine engine;
    fault::dsp::EngineParams params;
};

extern "C"
{
fault_engine* fault_create (double sampleRate, int maxBlock)
{
    auto* e = new fault_engine();
    e->engine.prepare (sampleRate, maxBlock);
    return e;
}
void fault_destroy (fault_engine* e) { delete e; }
void fault_set_params (fault_engine* e, float time, int repeats, float mix, int freeze, int clockSource, float windowing, float width)
{
    auto& p = e->params;
    p.time = time;
    p.repeats = repeats;
    p.mix = mix;
    p.freeze = freeze != 0;
    p.clock = (fault::dsp::ClockSource) clockSource;
    p.windowing = windowing;
    p.width = width;
    e->engine.setParams (p);
}
void fault_set_transport (fault_engine* e, int playing, double bpm, double ppq)
{
    e->engine.setTransport ({ true, playing != 0, bpm, ppq });
}
void fault_process (fault_engine* e, const float* inL, const float* inR, float* outL, float* outR, int n)
{
    e->engine.process (inL, inR, outL, outR, n);
}
}
