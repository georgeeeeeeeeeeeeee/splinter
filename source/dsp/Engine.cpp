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
    octCoef_ = 1.0 - std::exp (-1.0 / (0.02 * sampleRate));   // Bend knob glide, 20 ms
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
    numNotes_ = 0;
    hasPitch_ = false;
    pitchNote_ = 60;
    lastMode_ = p_.mode;
    octSmoothed_ = p_.mode == Mode::micro ? -3.0 + 6.0 * (double) std::clamp (p_.bend, 0.0f, 1.0f) : 0.0;
    head_.speed = 1.0;
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
    // The slice-edge envelope follows the head's position in the slice (in playback direction), so it
    // stays right when the speed changes mid-slice; a mid-slice jump adds its own fade-in.
    const double len = win_.sliceLength(), dur = head_.duration (len);
    float g = SliceEnvelope::gain (head_.sinceStart (len), dur, p_.windowing, sr_);
    if (head_.fadeIn < jumpFadeLen_)
        g *= SliceEnvelope::rise (head_.fadeIn / jumpFadeLen_);
    return g;
}

void Engine::readHead (const Window& w, const Playhead& h, float& l, float& r) const noexcept
{
    buf_.read (w.sliceStart() + h.offset, l, r);
}

void Engine::spawnTail (double fadeDuration) noexcept
{
    // Mid-slice (the old head still audible), with windowing on: let the old head fade out rather than
    // cut. At a slice end its envelope is already down, and at windowing 0 cuts are meant to be hard.
    const float g = win_.valid() ? mainGain() * silenceGain() : 0.0f;
    if (p_.windowing > 0.0f && g > 1.0e-3f)
    {
        tailWin_ = win_;
        tailHead_ = head_;
        const double fade = SliceEnvelope::fadeSamples (fadeDuration, p_.windowing, sr_);
        tailLen_ = std::clamp (fade, 32.0, kMaxTailSeconds * sr_);
        tailLeft_ = tailLen_;
        tailGain0_ = g;
    }
}

void Engine::fadeInHead() noexcept
{
    // After a jump into the middle of a slice the new position fades in over the same time the old one
    // fades out (nothing at windowing 0: hard, as intended).
    if (p_.windowing <= 0.0f)
        return;
    jumpFadeLen_ = std::clamp (SliceEnvelope::fadeSamples (head_.duration (win_.sliceLength()), p_.windowing, sr_), 32.0, kMaxTailSeconds * sr_);
    head_.fadeIn = 0.0;
}

void Engine::jump (const Window& next) noexcept
{
    spawnTail (head_.duration (next.sliceLength()));
    win_ = next;
    // Forward play starts at the slice's start, reverse at its end.
    head_.offset = head_.speed < 0.0 ? std::nextafter (next.sliceLength(), 0.0) : 0.0;
    head_.fadeIn = 1.0e30;   // starts at the slice edge: the edge envelope fades it in
}

void Engine::handleNote (const NoteEvent& e) noexcept
{
    if (std::find (gateNotes_.begin(), gateNotes_.end(), e.note) != gateNotes_.end())
        return;   // gate notes (Stage 5) never set the pitch
    if (e.on && e.velocity > 0)
    {
        pitchNote_ = e.note;   // last note wins; the pitch holds after note-off, like a CV
        hasPitch_ = true;
    }
}

int Engine::traverseSlice (int repeats) const noexcept
{
    const int n = std::max (1, repeats);
    return n <= 1 ? 0 : std::clamp ((int) std::floor (std::clamp (p_.brk, 0.0f, 1.0f) * (float) n), 0, n - 1);
}

float Engine::silenceGain() const noexcept
{
    // Micro Silence: the last `duty` of each slice's playback is silent, faded in and out like an edge.
    if (p_.mode != Mode::micro || ! p_.breakOnMicro)
        return 1.0f;
    const double duty = 0.9 * (double) std::clamp (p_.brk, 0.0f, 1.0f);
    if (duty <= 0.0)
        return 1.0f;
    const double len = win_.sliceLength(), dur = head_.duration (len), audible = (1.0 - duty) * dur;
    const double fade = std::max (1.0, SliceEnvelope::fadeSamples (dur, std::max (p_.windowing, 0.0001f), sr_));
    return SliceEnvelope::rise ((audible - head_.sinceStart (len)) / fade);
}

void Engine::updateSpeed() noexcept
{
    // Micro: speed = 2^(Bend octaves + note octaves), reversed by the Micro Bend toggle. Macro: 1x until
    // Stage 3. Switching mode starts the new mode from its own state, so nothing leaks across.
    const bool micro = p_.mode == Mode::micro;
    const double knobOct = micro ? -3.0 + 6.0 * (double) std::clamp (p_.bend, 0.0f, 1.0f) : 0.0;
    if (p_.mode != lastMode_)
    {
        lastMode_ = p_.mode;
        octSmoothed_ = knobOct;
    }
    octSmoothed_ += (knobOct - octSmoothed_) * octCoef_;
    if (std::abs (knobOct - octSmoothed_) < 1.0e-6)
        octSmoothed_ = knobOct;
    const double noteOct = micro && p_.noteToSpeed && hasPitch_ ? (pitchNote_ - 60) / 12.0 : 0.0;
    const double dir = micro && p_.bendOnMicro ? -1.0 : 1.0;
    const double speed = dir * std::exp2 (octSmoothed_ + noteOct);
    if ((speed < 0.0) != (head_.speed < 0.0) && win_.valid())
    {
        spawnTail (head_.duration (win_.sliceLength()));   // a direction change fades like a jump
        head_.speed = speed;
        fadeInHead();
    }
    head_.speed = speed;
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
    if (p_.mode == Mode::micro && ! p_.breakOnMicro)
        next.slice = traverseSlice (next.repeats);
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
    int nextTick = 0, nextNote = 0;
    const int numTicks = clock_.numTicks();

    for (int i = 0; i < n; ++i)
    {
        const float xl = std::isfinite (inL[i]) ? inL[i] : 0.0f;
        const float xr = std::isfinite (inR[i]) ? inR[i] : 0.0f;
        // A tick captures the most recent `period` samples up to (not including) this one, then this
        // sample is recorded: the playhead's first read is exactly one period back.
        curSample_ = processed_ + i;
        while (nextNote < numNotes_ && notes_[(size_t) nextNote].offset <= i)
            handleNote (notes_[(size_t) nextNote++]);
        updateSpeed();
        while (nextTick < numTicks && clock_.tickAt (nextTick) == i)
        {
            onTick();
            ++nextTick;
        }
        buf_.write (xl, xr);

        // Micro Traverse: the knob picks the slice; a change skips there at once (keeping the position in
        // the slice, like a CD skipping), faded like any jump.
        if (win_.valid() && p_.mode == Mode::micro && ! p_.breakOnMicro)
        {
            const int s = traverseSlice (win_.repeats);
            if (s != win_.slice)
            {
                spawnTail (head_.duration (win_.sliceLength()));
                win_.slice = s;
                fadeInHead();
                ++sliceChanges_;
            }
        }

        float wl = 0.0f, wr = 0.0f;
        if (win_.valid())
        {
            float l, r;
            readHead (win_, head_, l, r);
            const float g = mainGain() * silenceGain();
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
    numNotes_ = 0;
    const double s = head_.speed;
    status_.speed.store ((float) s, std::memory_order_relaxed);
    const double oct = std::log2 (std::abs (s));
    status_.onOctave.store (std::abs (oct - std::round (oct)) < 5.0 / 1200.0, std::memory_order_relaxed);
    status_.slice.store (win_.slice, std::memory_order_relaxed);
    status_.sliceChanges.store (sliceChanges_, std::memory_order_relaxed);
    status_.silenceDuty.store (p_.mode == Mode::micro && p_.breakOnMicro ? 0.9f * std::clamp (p_.brk, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
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
