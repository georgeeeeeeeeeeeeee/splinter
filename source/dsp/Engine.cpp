#include "dsp/Engine.h"
#include "dsp/SafetyClip.h"

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
constexpr double kStopFadeSpeed = 0.04;   // tape stop: fade out below 4% speed
constexpr int kMaxPieces = 32 * 1024;     // Repeats x Macro Break cuts
float sanitise (float x) noexcept { return std::isfinite (x) ? std::clamp (x, -kOutputCeiling, kOutputCeiling) : 0.0f; }
} // namespace

Engine::Engine()
{
    tables_ = MacroTables::defaults();
    reseed();
}

void Engine::prepare (double sampleRate, int)
{
    sr_ = sampleRate;
    buf_.prepare (sampleRate);
    overview_.prepare (buf_.capacity());
    clock_.prepare (sampleRate);
    corrupt_.prepare (sampleRate);
    env_.prepare (sampleRate);
    ccCoef_ = 1.0 - std::exp (-(double) kControlSamples / (0.01 * sampleRate));   // CC lanes: 10 ms smoothing
    smoothLen_ = std::max (1, (int) (0.02 * sampleRate));
    octCoef_ = 1.0 - std::exp (-1.0 / (0.02 * sampleRate));   // Bend knob glide, 20 ms
    outCoef_ = 1.0 - std::exp (-1.0 / (0.02 * sampleRate));   // Output trim, 20 ms
    reset();
}

void Engine::reset() noexcept
{
    buf_.reset();
    overview_.reset();
    jumps_ = { 0, 0 };
    clock_.reset();
    clock_.setSource (p_.clock);
    clock_.setTime (p_.time);
    pendingRepeats_ = std::clamp (p_.repeats, 1, 32);
    for (auto& v : voices_)
    {
        v = Voice {};
        v.baseRepeats = pendingRepeats_;
    }
    freezeWanted_ = p_.freeze;
    forceWet_ = false;
    resetRequested_ = false;
    mix_ = mixTarget_ = std::clamp (p_.mix, 0.0f, 1.0f);
    width_ = widthTarget_ = std::clamp (p_.width, 0.0f, 1.0f);
    smoothLeft_ = 0;
    outGain_ = std::pow (10.0, std::clamp (p_.outputDb, -24.0f, 12.0f) / 20.0);
    processed_ = 0;
    numNotes_ = 0;
    hasPitch_ = false;
    pitchNote_ = 60;
    lastMode_ = p_.mode;
    octSmoothed_ = p_.mode == Mode::micro ? -3.0 + 6.0 * (double) std::clamp (p_.bend, 0.0f, 1.0f) : 0.0;
    lastSeed_ = p_.seed;
    reseed();
    corrupt_.setParams (p_.corrupt, p_.corruptType, p_.corruptSet);
    corrupt_.reset();
    for (auto& m : mods_) m.reset();
    env_.reset();
    env_.setParams (base_.mod.env);
    ccTarget_.fill (0.0f);
    ccValue_.fill (0.0f);
    sources_.fill (0.0f);
    offsets_ = {};
    clearGateState();
    lastPlaying_ = -1;
    tickCount_.store (0, std::memory_order_release);
}

void Engine::reseed() noexcept
{
    for (int s = 0; s < kNumStreams; ++s)
        rngs_[(size_t) s].seed (mix64 (sessionSeed_ + (uint64_t) s * 0x632be59bd9b4e019ULL), (uint64_t) s + 1);
    // Corrupt's randomness (Dropout, Vinyl, Decimate's jitter) follows the seed too.
    corrupt_.reseed (p_.seed != 0 ? mix64 ((uint64_t) (uint32_t) p_.seed * 0x9e3779b97f4a7c15ULL) : mix64 (sessionSeed_ ^ 0xc0ffee));
    // The random shapes too: a fixed seed gives the same values at the same bar positions.
    const uint64_t ms = p_.seed != 0 ? mix64 ((uint64_t) (uint32_t) p_.seed * 0x9e3779b97f4a7c15ULL ^ 0x30d) : mix64 (sessionSeed_ ^ 0x30d);
    for (int m = 0; m < kNumModulators; ++m)
        mods_[(size_t) m].reseed (mix64 (ms + (uint64_t) m));
}

void Engine::clearGateState() noexcept
{
    freezeFlip_ = false;
    bendFlip_ = { false, false };
    breakFlip_ = { false, false };
    corruptSteps_ = 0;
    gateHeld_ = {};
    corruptStepped_ = false;
    heldMode_ = { 0, 0 };
}

void Engine::setGateNotes (const std::array<int, kNumGates>& n) noexcept
{
    for (int g = 0; g < kNumGates; ++g)
        if (n[(size_t) g] != gateNotes_[(size_t) g] && gateHeld_[(size_t) g] > 0)
        {
            gateHeld_[(size_t) g] = 0;   // its note-off would no longer match
            releaseGate (g);
        }
    gateNotes_ = n;
}

void Engine::releaseGate (int gate) noexcept
{
    if (base_.gateMode != GateMode::momentary)
        return;
    switch (gate)
    {
        case gateFreeze: freezeFlip_ = false; break;
        case gateBend: bendFlip_[(size_t) heldMode_[0]] = false; break;
        case gateBreak: breakFlip_[(size_t) heldMode_[1]] = false; break;
        case gateCorrupt:
            if (corruptStepped_) --corruptSteps_;
            corruptStepped_ = false;
            break;
        default: break;
    }
    applyEffective();
}

Pcg32 Engine::tickRng (int stream, int64_t tickIndex) noexcept
{
    // Fixed seed: each tick's dice depend only on (seed, stream, tick index), so with the host clock a
    // given bar always glitches the same way, wherever playback starts.
    const uint64_t s = mix64 ((uint64_t) (uint32_t) p_.seed * 0x9e3779b97f4a7c15ULL ^ mix64 ((uint64_t) tickIndex));
    return Pcg32 (s, (uint64_t) stream + 1);
}

bool Engine::setTables (const MacroTables& t) noexcept
{
    if (tablesPending_.load (std::memory_order_acquire))
        return false;
    pendingTables_ = t;
    tablesPending_.store (true, std::memory_order_release);
    return true;
}

void Engine::setParams (const EngineParams& p) noexcept
{
    base_ = p;
    if (p.gateMode != lastGateMode_)
    {
        lastGateMode_ = p.gateMode;
        clearGateState();   // a held momentary gate must not stick when the mode changes
    }
    env_.setParams (p.mod.env);
    applyEffective();
}

void Engine::applyEffective() noexcept
{
    // Knobs + modulation offsets (clamped), then the gates' flips. Never written back to base_.
    const auto& o = offsets_.d;
    const auto unit = [] (float x) { return std::isfinite (x) ? std::clamp (x, 0.0f, 1.0f) : 0.0f; };
    EngineParams e = base_;
    e.time = unit (base_.time + o[(size_t) ModDest::time]);
    if (o[(size_t) ModDest::repeats] != 0.0f)
    {
        // Repeats on its exponential knob: 32^x.
        const float x = unit (std::log ((float) std::clamp (base_.repeats, 1, 32)) / std::log (32.0f) + o[(size_t) ModDest::repeats]);
        e.repeats = std::clamp ((int) std::lround (std::pow (32.0f, x)), 1, 32);
    }
    e.mix = unit (base_.mix + o[(size_t) ModDest::mix]);
    e.bend = unit (base_.bend + o[(size_t) ModDest::bend]);
    e.brk = unit (base_.brk + o[(size_t) ModDest::brk]);
    e.corrupt = unit (base_.corrupt + o[(size_t) ModDest::corrupt]);
    e.freeze = base_.freeze != freezeFlip_;
    e.bendOnMacro = base_.bendOnMacro != bendFlip_[0];
    e.bendOnMicro = base_.bendOnMicro != bendFlip_[1];
    e.breakOnMacro = base_.breakOnMacro != breakFlip_[0];
    e.breakOnMicro = base_.breakOnMicro != breakFlip_[1];
    const int setSize = base_.corruptSet == CorruptSet::original3 ? 3 : kNumCorruptTypes;
    for (int k = ((corruptSteps_ % setSize) + setSize) % setSize; k > 0; --k)
        e.corruptType = Corrupt::nextType (e.corruptType, e.corruptSet);
    p_ = e;

    clock_.setSource (p_.clock);
    clock_.setTime (p_.time);
    pendingRepeats_ = std::clamp (p_.repeats, 1, 32);
    freezeWanted_ = p_.freeze;
    if (p_.seed != lastSeed_)
    {
        lastSeed_ = p_.seed;
        reseed();
    }
    corrupt_.setParams (p_.corrupt, p_.corruptType, p_.corruptSet);
    if (p_.freezeMode == FreezeMode::momentary && buf_.frozen() != p_.freeze)
    {
        buf_.setFrozen (p_.freeze);
        forceWet_ = p_.freeze && base_.mix < 0.001f;
    }
    const float mt = p_.mix, wt = std::clamp (p_.width, 0.0f, 1.0f);
    if (mt != mixTarget_ || wt != widthTarget_)
    {
        mixTarget_ = mt;
        widthTarget_ = wt;
        mixStep_ = (mixTarget_ - mix_) / (float) smoothLen_;
        widthStep_ = (widthTarget_ - width_) / (float) smoothLen_;
        smoothLeft_ = smoothLen_;
    }
}

void Engine::updateModulation (int offsetInBlock) noexcept
{
    const bool playing = transport_.valid && transport_.playing;
    const double bpm = transport_.valid && transport_.bpm > 1.0 ? transport_.bpm : 120.0;
    const ModClock mc { playing, bpm, transport_.ppq + (double) offsetInBlock * bpm / 60.0 / sr_ };
    for (int m = 0; m < kNumModulators; ++m)
        sources_[(size_t) ModSource::mod1 + (size_t) m] = mods_[(size_t) m].next (base_.mod.mods[(size_t) m], mc, sr_, kControlSamples);
    sources_[(size_t) ModSource::env] = env_.value();
    for (int c = 0; c < kNumCcLanes; ++c)
    {
        auto& v = ccValue_[(size_t) c];
        v += (ccTarget_[(size_t) c] - v) * (float) ccCoef_;
        if (std::abs (ccTarget_[(size_t) c] - v) < 1.0e-5f) v = ccTarget_[(size_t) c];
        sources_[(size_t) ModSource::cc1 + (size_t) c] = base_.mod.ccBipolar[(size_t) c] ? 2.0f * v - 1.0f : v;
    }
    offsets_ = computeOffsets (base_.mod, sources_, base_.mode == Mode::micro);
}

float Engine::mainGain (const Voice& v) const noexcept
{
    // The slice-edge envelope follows the head's position in the slice (in playback direction), so it
    // stays right when the speed changes mid-slice; a mid-slice jump adds its own fade-in.
    const double len = v.win.sliceLength(), dur = v.head.duration (len);
    float g = SliceEnvelope::gain (v.head.sinceStart (len), dur, p_.windowing, sr_);
    if (v.head.fadeIn < v.jumpFadeLen)
        g *= SliceEnvelope::rise (v.head.fadeIn / v.jumpFadeLen);
    // A tape stop fades out over the last few percent of speed, so the standstill is silent, not DC.
    if (v.stopLen > 0.0)
        g *= SliceEnvelope::rise (std::abs (v.head.speed) / kStopFadeSpeed);
    return g;
}

void Engine::spawnTail (Voice& v, double fadeDuration) noexcept
{
    // Mid-slice (the old head still audible), with windowing on: let the old head fade out rather than
    // cut. At a slice end its envelope is already down, and at windowing 0 cuts are meant to be hard.
    const float g = v.win.valid() ? mainGain (v) * silenceGain (v) : 0.0f;
    if (p_.windowing > 0.0f && g > 1.0e-3f)
    {
        v.tailWin = v.win;
        v.tailHead = v.head;
        const double fade = SliceEnvelope::fadeSamples (fadeDuration, p_.windowing, sr_);
        v.tailLen = std::clamp (fade, 32.0, kMaxTailSeconds * sr_);
        v.tailLeft = v.tailLen;
        v.tailGain0 = g;
    }
}

void Engine::fadeInHead (Voice& v) noexcept
{
    // After a jump into the middle of a slice the new position fades in over the same time the old one
    // fades out (nothing at windowing 0: hard, as intended).
    if (p_.windowing <= 0.0f)
        return;
    v.jumpFadeLen = std::clamp (SliceEnvelope::fadeSamples (v.head.duration (v.win.sliceLength()), p_.windowing, sr_), 32.0, kMaxTailSeconds * sr_);
    v.head.fadeIn = 0.0;
}

void Engine::jump (Voice& v, const Window& next) noexcept
{
    spawnTail (v, v.head.duration (next.sliceLength()));
    v.win = next;
    // Forward play starts at the slice's start, reverse at its end.
    v.head.offset = v.head.speed < 0.0 ? std::nextafter (next.sliceLength(), 0.0) : 0.0;
    v.head.fadeIn = 1.0e30;   // starts at the slice edge: the edge envelope fades it in
}

bool Engine::handleNote (const NoteEvent& e) noexcept
{
    if (e.cc)
    {
        if (e.note == 120 || e.note == 123)   // All Sound Off / All Notes Off: every held gate lets go
        {
            for (int g = 0; g < kNumGates; ++g)
                if (gateHeld_[(size_t) g] > 0) { gateHeld_[(size_t) g] = 0; releaseGate (g); }
        }
        for (int c = 0; c < kNumCcLanes; ++c)
            if (base_.mod.ccNumbers[(size_t) c] == e.note)
                ccTarget_[(size_t) c] = (float) std::clamp (e.velocity, 0, 127) / 127.0f;
        return false;
    }
    const bool on = e.on && e.velocity > 0;
    const auto it = std::find (gateNotes_.begin(), gateNotes_.end(), e.note);
    if (it == gateNotes_.end())
    {
        if (on)
        {
            pitchNote_ = e.note;   // last note wins; the pitch holds after note-off, like a CV
            hasPitch_ = true;
        }
        return false;
    }
    // A gate (never a pitch). Latching: each note-on flips the state. Momentary: flipped while any of
    // its note-ons is still held; a note-off with none held is ignored, so a stray or doubled note-off
    // can't leave a gate stuck the wrong way.
    const int g = (int) (it - gateNotes_.begin());
    auto& held = gateHeld_[(size_t) g];
    if (! on)
    {
        if (held == 0) return false;
        if (--held == 0) releaseGate (g);
        return false;
    }
    const bool first = held++ == 0;
    const bool momentary = base_.gateMode == GateMode::momentary;
    const size_t modeIdx = base_.mode == Mode::micro ? 1 : 0;
    bool pulse = false;
    switch (g)
    {
        // Bend / Break flip the current mode's toggle. Momentary remembers which, so a mode switch
        // mid-hold still lets the right one go on release.
        case gateFreeze: freezeFlip_ = momentary ? true : ! freezeFlip_; break;
        case gateBend:
            if (! momentary) bendFlip_[modeIdx] = ! bendFlip_[modeIdx];
            else if (first) { heldMode_[0] = (int) modeIdx; bendFlip_[modeIdx] = true; }
            break;
        case gateBreak:
            if (! momentary) breakFlip_[modeIdx] = ! breakFlip_[modeIdx];
            else if (first) { heldMode_[1] = (int) modeIdx; breakFlip_[modeIdx] = true; }
            break;
        case gateCorrupt:
            if (base_.corruptAsReset) clock_.resync();
            else if (! momentary) ++corruptSteps_;                    // the next type in the set
            else if (first) { ++corruptSteps_; corruptStepped_ = true; }   // momentary: back on release
            break;
        case gateReset: clock_.resync(); break;
        case gateClock: pulse = p_.clock == ClockSource::note; break;
        default: break;
    }
    applyEffective();
    return pulse;
}

int Engine::traverseSlice (int repeats) const noexcept
{
    const int n = std::max (1, repeats);
    return n <= 1 ? 0 : std::clamp ((int) std::floor (std::clamp (p_.brk, 0.0f, 1.0f) * (float) n), 0, n - 1);
}

float Engine::silenceGain (const Voice& v) const noexcept
{
    // Silence: the last `duty` of each slice's playback is silent, faded in and out like an edge.
    // Micro: the Break knob (Silence toggle on). Macro: the tick's dice.
    double duty = 0.0;
    if (p_.mode == Mode::micro)
        duty = p_.breakOnMicro ? 0.9 * (double) std::clamp (p_.brk, 0.0f, 1.0f) : 0.0;
    else
        duty = v.duty;
    if (duty <= 0.0)
        return 1.0f;
    const double len = v.win.sliceLength(), dur = v.head.duration (len), audible = (1.0 - duty) * dur;
    const double fade = std::max (1.0, SliceEnvelope::fadeSamples (dur, std::max (p_.windowing, 0.0001f), sr_));
    return SliceEnvelope::rise ((audible - v.head.sinceStart (len)) / fade);
}

void Engine::resetMacroVoice (Voice& v) noexcept
{
    v.oct = v.target = 0.0;
    v.glideCoef = 1.0;
    v.dir = 1.0;
    v.stopLen = v.stopPos = 0.0;
    v.duty = 0.0;
    v.clickLen = 0.0;
    v.pieces = 1;
    v.win.repeats = v.baseRepeats;
    v.win.slice = std::min (v.win.slice, v.win.repeats - 1);
}

double Engine::macroSpeed (Voice& v) noexcept
{
    if (v.stopLen > 0.0)
    {
        const double t = std::min (1.0, v.stopPos / v.stopLen);
        v.stopPos += 1.0;
        return v.dir * std::pow (1.0 - t, (double) std::max (0.1f, tables_.tapeStopCurve));
    }
    if (v.glideCoef >= 1.0 || std::abs (v.target - v.oct) < 1.0e-7)
        v.oct = v.target;
    else
        v.oct += (v.target - v.oct) * v.glideCoef;
    return v.dir * std::exp2 (v.oct);
}

void Engine::updateSpeed() noexcept
{
    // Micro: speed = 2^(Bend octaves + note octaves), reversed by the Micro Bend toggle, the same for both
    // voices. Macro: each voice follows its tick's Bend decision. Switching mode starts the new mode from
    // its own state, so nothing leaks across.
    const bool micro = p_.mode == Mode::micro;
    const double knobOct = micro ? -3.0 + 6.0 * (double) std::clamp (p_.bend, 0.0f, 1.0f) : 0.0;
    if (p_.mode != lastMode_)
    {
        lastMode_ = p_.mode;
        octSmoothed_ = knobOct;
        for (auto& v : voices_)
            resetMacroVoice (v);
    }
    if (! micro)
    {
        for (auto& v : voices_)
            v.head.speed = macroSpeed (v);
        return;
    }
    octSmoothed_ += (knobOct - octSmoothed_) * octCoef_;
    if (std::abs (knobOct - octSmoothed_) < 1.0e-6)
        octSmoothed_ = knobOct;
    const double noteOct = p_.noteToSpeed && hasPitch_ ? (pitchNote_ - 60) / 12.0 : 0.0;
    const double dir = p_.bendOnMicro ? -1.0 : 1.0;
    const double speed = dir * std::exp2 (octSmoothed_ + noteOct);
    for (auto& v : voices_)
    {
        if ((speed < 0.0) != (v.head.speed < 0.0) && v.win.valid())
        {
            spawnTail (v, v.head.duration (v.win.sliceLength()));   // a direction change fades like a jump
            v.head.speed = speed;
            fadeInHead (v);
        }
        v.head.speed = speed;
    }
}

void Engine::onTick (int64_t tickIndex) noexcept
{
    // Synced modulators are re-read at the tick's own beat position, so a synced S&H on Repeats changes
    // exactly with the tick (the control grid alone could be up to 31 samples late).
    if (transport_.valid && transport_.playing)
    {
        bool any = false;
        for (const auto& m : base_.mod.mods) any = any || m.sync >= 0;
        if (any)
        {
            const double bpm = transport_.bpm > 1.0 ? transport_.bpm : 120.0;
            const ModClock mc { true, bpm, transport_.ppq + (double) (curSample_ - processed_) * bpm / 60.0 / sr_ };
            for (int m = 0; m < kNumModulators; ++m)
                sources_[(size_t) ModSource::mod1 + (size_t) m] = mods_[(size_t) m].at (base_.mod.mods[(size_t) m], mc);
            offsets_ = computeOffsets (base_.mod, sources_, base_.mode == Mode::micro);
            applyEffective();
        }
    }
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
    const bool macro = p_.mode == Mode::macro;
    if (! macro && ! p_.breakOnMicro)
        next.slice = traverseSlice (next.repeats);

    // Macro: roll the dice. Shared stereo: one roll for both voices; Unique: one per voice.
    std::array<BendDecision, 2> bend {};
    std::array<BreakDecision, 2> brk {};
    if (macro && next.valid())
    {
        const TickContext ctx { sr_, clock_.periodSamples(), next.sliceLength(), next.repeats, p_.clock != ClockSource::internal };
        const bool fixedSeed = p_.seed != 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            if (ch == 1 && p_.stereo == StereoMode::shared)
            {
                bend[1] = bend[0];
                brk[1] = brk[0];
                break;
            }
            if (p_.bendOnMacro)
            {
                Pcg32 local = fixedSeed ? tickRng (kBendStream + ch, tickIndex) : Pcg32 {};
                bend[(size_t) ch] = decideBend (tables_, p_.bend, fixedSeed ? local : rngs_[(size_t) (kBendStream + ch)], ctx);
            }
            if (p_.breakOnMacro)
            {
                Pcg32 local = fixedSeed ? tickRng (kBreakStream + ch, tickIndex) : Pcg32 {};
                brk[(size_t) ch] = decideBreak (tables_, p_.brk, fixedSeed ? local : rngs_[(size_t) (kBreakStream + ch)], ctx);
            }
        }
    }

    const uint64_t k = tickCount_.load (std::memory_order_relaxed);
    auto& e = ticks_[(size_t) (k % kTickRing)];
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& v = voices_[(size_t) ch];
        Window w = next;
        v.baseRepeats = next.repeats;
        if (macro)
        {
            const auto& b = bend[(size_t) ch];
            const double oldDir = v.head.speed < 0.0 ? -1.0 : 1.0;
            v.dir = b.reverse ? -1.0 : 1.0;
            v.stopLen = b.stopLength;
            v.stopPos = 0.0;
            if (b.glide > 0.0)
            {
                v.oct = std::log2 (std::clamp (std::abs (v.head.speed), 1.0 / 64.0, 64.0));   // glide from where it is
                v.target = b.octaves;
                v.glideCoef = 1.0 - std::exp (-1.0 / b.glide);
            }
            else
            {
                v.oct = v.target = b.octaves;
                v.glideCoef = 1.0;
            }
            v.head.speed = v.stopLen > 0.0 ? v.dir : v.dir * std::exp2 (v.oct);
            if (b.click != 0.0f && v.dir != oldDir)
            {
                v.clickGain = b.click;
                v.clickLen = std::max (1.0, (double) tables_.clickMs * 0.001 * sr_);
                v.clickPos = 0.0;
            }
            const auto& c = brk[(size_t) ch];
            v.pieces = std::max (1, c.pieces);
            w.repeats = std::min (kMaxPieces, next.repeats * v.pieces);
            w.slice = std::clamp (c.slice, 0, w.repeats - 1);
            v.duty = c.duty;
            if ((c.mask & (1u << breakJump)) != 0)
                ++jumps_[(size_t) ch];
        }
        if (w.valid())
            jump (v, w);
        else
            v.win = w;
        e.speed[(size_t) ch] = (float) v.head.speed;
        e.octaves[(size_t) ch] = (float) v.target;
        e.pieces[(size_t) ch] = w.repeats;
        e.slice[(size_t) ch] = w.slice;
        e.duty[(size_t) ch] = (float) v.duty;
        e.bendMask[(size_t) ch] = (uint8_t) bend[(size_t) ch].mask();
        e.breakMask[(size_t) ch] = (uint8_t) brk[(size_t) ch].mask;
    }

    e.sample = curSample_;
    e.index = tickIndex;
    e.windowStart = next.start;
    e.windowLength = next.length;
    e.period = clock_.periodSamples();
    e.repeats = next.repeats;
    e.frozen = buf_.frozen();
    tickCount_.store (k + 1, std::memory_order_release);
}

void Engine::renderSegment (const float* inL, const float* inR, float* outL, float* outR, int from, int to) noexcept
{
    // One stretch with no events inside and one clock block (tick offsets relative to `from`).
    int nextTick = 0;
    const int numTicks = clock_.numTicks();
    const double clickW = 2.0 * std::numbers::pi * 2800.0 / sr_;
    for (int i = from; i < to; ++i)
    {
        const float xl = std::isfinite (inL[i]) ? inL[i] : 0.0f;
        const float xr = std::isfinite (inR[i]) ? inR[i] : 0.0f;
        // A tick captures the most recent `period` samples up to (not including) this one, then this
        // sample is recorded: the playhead's first read is exactly one period back.
        curSample_ = processed_ + i;
        env_.process (xl, xr);
        updateSpeed();
        while (nextTick < numTicks && clock_.tickAt (nextTick) + from == i)
        {
            onTick (clock_.tickIndex (nextTick));
            ++nextTick;
        }
        if (! buf_.frozen())
            overview_.push (xl, xr);
        buf_.write (xl, xr);

        std::array<float, 2> wet { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& v = voices_[(size_t) ch];
            // Micro Traverse: the knob picks the slice; a change skips there at once (keeping the position
            // in the slice, like a CD skipping), faded like any jump.
            if (v.win.valid() && p_.mode == Mode::micro && ! p_.breakOnMicro)
            {
                const int sl = traverseSlice (v.win.repeats);
                if (sl != v.win.slice)
                {
                    spawnTail (v, v.head.duration (v.win.sliceLength()));
                    v.win.slice = sl;
                    fadeInHead (v);
                    ++jumps_[(size_t) ch];
                    if (ch == 0)
                        ++sliceChanges_;
                }
            }

            float w = 0.0f;
            if (v.win.valid())
            {
                w = buf_.read (ch, v.win.sliceStart() + v.head.offset) * (mainGain (v) * silenceGain (v));
                if (v.head.advance (v.win.sliceLength()) && pendingRepeats_ != v.baseRepeats)
                {
                    // Repeats changes take effect at a slice boundary.
                    v.baseRepeats = pendingRepeats_;
                    v.win.repeats = std::min (kMaxPieces, v.baseRepeats * v.pieces);
                    v.win.slice = std::min (v.win.slice, v.win.repeats - 1);
                    v.head.offset = 0.0;
                }
            }
            if (v.tailLeft > 0.0)
            {
                const float g = v.tailGain0 * SliceEnvelope::rise (v.tailLeft / v.tailLen);
                w += buf_.read (ch, v.tailWin.sliceStart() + v.tailHead.offset) * g;
                v.tailHead.advance (v.tailWin.sliceLength());
                v.tailLeft -= 1.0;
            }
            if (v.clickPos < v.clickLen)
            {
                // A vinyl-style click: a short damped ring.
                w += v.clickGain * (float) (std::exp (-5.0 * v.clickPos / v.clickLen) * std::cos (clickW * v.clickPos));
                v.clickPos += 1.0;
            }
            wet[(size_t) ch] = w;
        }
        float wl = wet[0], wr = wet[1];
        corrupt_.process (wl, wr);

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
        const float wetG = m <= 0.0f ? 0.0f : m >= 1.0f ? 1.0f : std::sin (m * 0.5f * (float) std::numbers::pi);
        outL[i] = sanitise (xl * dry + wl * wetG);
        outR[i] = sanitise (xr * dry + wr * wetG);
    }
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    if (tablesPending_.load (std::memory_order_acquire))
    {
        tables_ = pendingTables_;
        tablesPending_.store (false, std::memory_order_release);
    }
    if (clearGates_.exchange (false, std::memory_order_acq_rel))
    {
        clearGateState();
        applyEffective();
    }
    if (resetRequested_)
    {
        resetRequested_ = false;
        clock_.resync();
    }
    // A transport start (from a known stop) is a Reset too: back on the bar when host-synced, a new
    // window at once on the internal clock, the division counter realigned on the note clock.
    const bool playing = transport_.valid && transport_.playing;
    if (lastPlaying_ == 0 && playing)
    {
        clock_.transportStarted();
        if (p_.clock != ClockSource::host)
            clock_.resync();
    }
    lastPlaying_ = playing ? 1 : 0;

    // Segments: split at the control grid (absolute time, so block sizes don't matter) and at every
    // MIDI event, so gates, resets and clock pulses land on their own sample.
    const double bps = (transport_.valid && transport_.bpm > 1.0 ? transport_.bpm : 120.0) / 60.0 / sr_;
    int nextNote = 0;
    for (int i = 0; i < n;)
    {
        bool pulse = false;
        while (nextNote < numNotes_ && notes_[(size_t) nextNote].offset <= i)
            pulse = handleNote (notes_[(size_t) nextNote++]) || pulse;
        const int64_t abs = processed_ + i;
        const int intoGrid = (int) (abs % kControlSamples);
        if (intoGrid == 0)
        {
            updateModulation (i);
            applyEffective();
        }
        int end = std::min (n, i + kControlSamples - intoGrid);
        if (nextNote < numNotes_)
            end = std::min (end, std::max (i + 1, notes_[(size_t) nextNote].offset));
        HostTransport t = transport_;
        if (playing)
            t.ppq += (double) i * bps;
        clock_.beginBlock (t, end - i, pulse);
        renderSegment (inL, inR, outL, outR, i, end);
        i = end;
    }
    {
        // Output trim. At 0 dB with nothing to glide, the multiply is skipped: output stays bit-exact.
        const double target = std::pow (10.0, std::clamp (p_.outputDb, -24.0f, 12.0f) / 20.0);
        if (target != 1.0 || outGain_ != 1.0)
            for (int k = 0; k < n; ++k)
            {
                outGain_ += (target - outGain_) * outCoef_;
                if (std::abs (outGain_ - target) < 1.0e-7) outGain_ = target;
                outL[k] = (float) (outL[k] * outGain_);
                if (outR != outL) outR[k] = (float) (outR[k] * outGain_);
            }
    }
    if (p_.safetyClip)
    {
        bool hit = SafetyClip::process (outL, n);
        if (outR != outL) hit = SafetyClip::process (outR, n) || hit;
        if (hit) status_.clips.store (status_.clips.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
    processed_ += n;
    numNotes_ = 0;
    const double s = voices_[0].head.speed;
    status_.speed.store ((float) s, std::memory_order_relaxed);
    status_.speedR.store ((float) voices_[1].head.speed, std::memory_order_relaxed);
    const double oct = std::log2 (std::max (1.0e-9, std::abs (s)));
    status_.onOctave.store (std::abs (oct - std::round (oct)) < 5.0 / 1200.0, std::memory_order_relaxed);
    status_.slice.store (voices_[0].win.slice, std::memory_order_relaxed);
    status_.sliceChanges.store (sliceChanges_, std::memory_order_relaxed);
    status_.silenceDuty.store (p_.mode == Mode::micro ? (p_.breakOnMicro ? 0.9f * std::clamp (p_.brk, 0.0f, 1.0f) : 0.0f) : (float) voices_[0].duty,
                               std::memory_order_relaxed);
    status_.corrupt.store (corrupt_.playing(), std::memory_order_relaxed);
    status_.modulated[(size_t) ModDest::time].store (p_.time, std::memory_order_relaxed);
    status_.modulated[(size_t) ModDest::repeats].store (std::log ((float) p_.repeats) / std::log (32.0f), std::memory_order_relaxed);
    status_.modulated[(size_t) ModDest::mix].store (p_.mix, std::memory_order_relaxed);
    status_.modulated[(size_t) ModDest::bend].store (p_.bend, std::memory_order_relaxed);
    status_.modulated[(size_t) ModDest::brk].store (p_.brk, std::memory_order_relaxed);
    status_.modulated[(size_t) ModDest::corrupt].store (p_.corrupt, std::memory_order_relaxed);
    for (int k = 0; k < kNumModSources; ++k)
        status_.sources[(size_t) k].store (sources_[(size_t) k], std::memory_order_relaxed);
    status_.frozen.store (buf_.frozen(), std::memory_order_relaxed);
    status_.bendOn.store (p_.mode == Mode::micro ? p_.bendOnMicro : p_.bendOnMacro, std::memory_order_relaxed);
    status_.breakOn.store (p_.mode == Mode::micro ? p_.breakOnMicro : p_.breakOnMacro, std::memory_order_relaxed);
    status_.corruptType.store ((int) p_.corruptType, std::memory_order_relaxed);
    status_.windowStart.store (voices_[0].win.start, std::memory_order_relaxed);
    status_.windowLength.store (voices_[0].win.length, std::memory_order_relaxed);
    status_.period.store (clock_.periodSamples(), std::memory_order_relaxed);
    status_.clockStep.store (clock_.step(), std::memory_order_relaxed);
    status_.written.store (buf_.written(), std::memory_order_relaxed);
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const auto& v = voices_[ch];
        status_.head[ch].store (v.win.sliceStart() + v.head.offset, std::memory_order_relaxed);
        status_.headSpeed[ch].store ((float) v.head.speed, std::memory_order_relaxed);
        status_.pieces[ch].store (v.win.repeats, std::memory_order_relaxed);
        status_.sliceIndex[ch].store (v.win.slice, std::memory_order_relaxed);
        status_.duty[ch].store (p_.mode == Mode::micro ? (p_.breakOnMicro ? 0.9f * p_.brk : 0.0f) : (float) v.duty, std::memory_order_relaxed);
        status_.jumps[ch].store (jumps_[ch], std::memory_order_relaxed);
    }
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
void fault_set_macro (fault_engine* e, int mode, float bend, float brk, int bendOn, int breakOn, int stereoShared, int seed)
{
    auto& p = e->params;
    p.mode = mode != 0 ? fault::dsp::Mode::micro : fault::dsp::Mode::macro;
    p.bend = bend;
    p.brk = brk;
    (mode != 0 ? p.bendOnMicro : p.bendOnMacro) = bendOn != 0;
    (mode != 0 ? p.breakOnMicro : p.breakOnMacro) = breakOn != 0;
    p.stereo = stereoShared != 0 ? fault::dsp::StereoMode::shared : fault::dsp::StereoMode::unique;
    p.seed = seed;
    e->engine.setParams (p);
}
void fault_set_corrupt (fault_engine* e, float amount, int type, int originalThreeOnly)
{
    auto& p = e->params;
    p.corrupt = amount;
    p.corruptType = (fault::dsp::CorruptType) std::clamp (type, 0, fault::dsp::kNumCorruptTypes - 1);
    p.corruptSet = originalThreeOnly != 0 ? fault::dsp::CorruptSet::original3 : fault::dsp::CorruptSet::all;
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
