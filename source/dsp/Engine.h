#pragma once
// Fault's engine: owns the buffer, clock, window, playheads and output stage, and processes a block.
// Pure C++ (no JUCE), plus a small C API at the bottom for the WASM route.
//
// Per sample: write the input into the RecordBuffer (unless frozen) -> clock ticks capture a new window
// (the most recent `period` samples) -> the playhead loops the current slice (Repeats) -> SliceEnvelope
// -> Mix (equal-power) with Width (M/S) on the wet.
// A jump (new window, slice wrap, reset) that lands mid-slice hands the old head to a short fade-out
// tail, so only windowing 0 cuts hard.

#include "dsp/Clock.h"
#include "dsp/Playhead.h"
#include "dsp/RecordBuffer.h"
#include "dsp/Window.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace fault::dsp
{
enum class FreezeMode { latching = 0, momentary = 1 };
enum class Mode { macro = 0, micro = 1 };

struct EngineParams
{
    float time = 0.5f;        // 0..1
    int repeats = 1;          // 1..32
    float mix = 0.5f;         // 0..1
    bool freeze = false;
    FreezeMode freezeMode = FreezeMode::latching;
    ClockSource clock = ClockSource::internal;
    float windowing = 0.02f;  // 0..1, default 2%
    float width = 0.5f;       // 0..1: 0 mono, 0.5 as is, 1 side x2
    // Stage 2: Micro mode. The Bend / Break toggles are stored per mode (brief §3.7).
    Mode mode = Mode::macro;
    float bend = 0.5f;        // Micro: speed -3 .. +3 octaves (0.5 = 1x)
    float brk = 0.0f;         // Micro: Traverse slice index, or Silence duty 0 .. 90%
    bool bendOnMacro = false, bendOnMicro = false;     // Micro: reverse
    bool breakOnMacro = false, breakOnMicro = false;   // Micro: false = Traverse, true = Silence
    bool noteToSpeed = false; // MIDI note pitch sets the speed (C3 = 60 = 1x), on top of Bend
};

// A MIDI note for the engine, at a sample offset in the next process() block.
struct NoteEvent
{
    int offset = 0, note = 60, velocity = 100;
    bool on = true;
};

// One clock tick, for the CLI's tick log and the UI.
struct TickEvent
{
    int64_t sample = 0;        // absolute output sample
    double windowStart = 0.0, windowLength = 0.0, period = 0.0;
    int repeats = 1;
    bool frozen = false;
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);   // allocates: message thread only
    void reset() noexcept;
    void setParams (const EngineParams& p) noexcept;
    void setTransport (const HostTransport& t) noexcept { transport_ = t; }   // once per block
    void triggerReset() noexcept { resetRequested_ = true; }
    // Notes for the next process() call, offsets ascending (up to kMaxNotes; extra ones are dropped).
    static constexpr int kMaxNotes = 512;
    void queueNote (const NoteEvent& e) noexcept { if (numNotes_ < kMaxNotes) notes_[(size_t) numNotes_++] = e; }
    // Gate notes (Freeze, Bend, Break, Corrupt, Reset, Note clock: brief §3.10) never set the pitch.
    void setGateNotes (const std::array<int, 6>& n) noexcept { gateNotes_ = n; }

    // in and out may alias. Any n.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

    // Tick log: a ring the CLI / UI reads (single consumer). Returns events since `from`.
    static constexpr int kTickRing = 1024;
    uint64_t ticksWritten() const noexcept { return tickCount_.load (std::memory_order_acquire); }
    const TickEvent& tick (uint64_t index) const noexcept { return ticks_[(size_t) (index % kTickRing)]; }

    const Window& window() const noexcept { return win_; }
    // UI state (read on the UI thread; plain values written once per block).
    struct Status
    {
        std::atomic<float> speed { 1.0f };          // signed playback ratio
        std::atomic<bool> onOctave { true };        // Micro Bend sits on an exact octave (±5 cents)
        std::atomic<int> slice { 0 };
        std::atomic<uint32_t> sliceChanges { 0 };   // Traverse moved to another slice
        std::atomic<float> silenceDuty { 0.0f };
    };
    const Status& status() const noexcept { return status_; }
    const RecordBuffer& buffer() const noexcept { return buf_; }
    const Clock& clock() const noexcept { return clock_; }
    double sampleRate() const noexcept { return sr_; }
    int64_t samplesProcessed() const noexcept { return processed_; }

private:
    void onTick() noexcept;
    void jump (const Window& next) noexcept;   // switch the main head, fading out the old one
    float mainGain() const noexcept;
    void readHead (const Window& w, const Playhead& h, float& l, float& r) const noexcept;
    void spawnTail (double fadeDuration) noexcept;        // hand the main head to the fade-out tail
    void fadeInHead() noexcept;                          // after a mid-slice jump
    void handleNote (const NoteEvent& e) noexcept;
    void updateSpeed() noexcept;                         // per sample: Bend / note / reverse -> head speed
    int traverseSlice (int repeats) const noexcept;
    float silenceGain() const noexcept;

    double sr_ = 48000.0;
    EngineParams p_;
    HostTransport transport_;
    RecordBuffer buf_;
    Clock clock_;

    Window win_;            // the main head's window / slice
    Playhead head_;
    Window tailWin_;        // the head being faded out after a jump
    Playhead tailHead_;
    double tailLeft_ = 0.0, tailLen_ = 0.0, jumpFadeLen_ = 1.0;
    float tailGain0_ = 1.0f;
    int pendingRepeats_ = 1;

    bool freezeWanted_ = false, forceWet_ = false, resetRequested_ = false;
    float mix_ = 0.5f, width_ = 0.5f, mixStep_ = 0.0f, widthStep_ = 0.0f;
    int smoothLeft_ = 0, smoothLen_ = 960;
    float mixTarget_ = 0.5f, widthTarget_ = 0.5f;
    int64_t processed_ = 0, curSample_ = 0;

    // Stage 2.
    std::array<NoteEvent, kMaxNotes> notes_ {};
    int numNotes_ = 0;
    std::array<int, 6> gateNotes_ { 36, 37, 38, 39, 40, 41 };   // C1 .. F1
    int pitchNote_ = 60;
    bool hasPitch_ = false;
    double octSmoothed_ = 0.0, octCoef_ = 0.001;
    Mode lastMode_ = Mode::macro;
    Status status_;
    uint32_t sliceChanges_ = 0;

    std::array<TickEvent, kTickRing> ticks_ {};
    std::atomic<uint64_t> tickCount_ { 0 };
};
} // namespace fault::dsp

// ---- C API (WASM / other hosts) ---------------------------------------------
extern "C"
{
typedef struct fault_engine fault_engine;
fault_engine* fault_create (double sampleRate, int maxBlock);
void fault_destroy (fault_engine*);
void fault_set_params (fault_engine*, float time, int repeats, float mix, int freeze, int clockSource, float windowing, float width);
void fault_set_transport (fault_engine*, int playing, double bpm, double ppq);
void fault_process (fault_engine*, const float* inL, const float* inR, float* outL, float* outR, int n);
}
