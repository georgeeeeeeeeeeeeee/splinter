#pragma once
// Splinter's engine: owns the buffer, clock, playheads and output stage, and processes a block.
// Pure C++ (no JUCE), plus a small C API at the bottom for the WASM route.
//
// Per sample: write the input into the RecordBuffer (unless frozen) -> clock ticks capture a new window
// (the most recent `period` samples) -> each channel's voice loops its slice (Repeats) -> SliceEnvelope
// and silence -> Corrupt -> Width (M/S) on the wet -> Mix (equal-power).
// Two voices, one per channel. In Micro mode, and in Macro with Shared stereo, they get the same
// decisions; in Macro with Unique stereo each rolls its own dice (brief §3.6).
// A jump (new window, slice change, reset) that lands mid-slice hands the old head to a short fade-out
// tail, so only windowing 0 cuts hard.

#include "dsp/Clock.h"
#include "dsp/Corrupt/Corrupt.h"
#include "dsp/MacroBreak.h"
#include "dsp/Modulation.h"
#include "dsp/Overview.h"
#include "dsp/Playhead.h"
#include "dsp/RecordBuffer.h"
#include "dsp/Rng.h"
#include "dsp/Window.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace fault::dsp
{
enum class FreezeMode { latching = 0, momentary = 1 };
enum class Mode { macro = 0, micro = 1 };
enum class StereoMode { unique = 0, shared = 1 };
enum class GateMode { latching = 0, momentary = 1 };
// Gate notes (brief §3.10), in the order of Engine::setGateNotes.
enum Gate { gateFreeze = 0, gateBend, gateBreak, gateCorrupt, gateReset, gateClock, gateFill, kNumGates };
// Break this bar: how long the fill is (beats or bars), in the order of the fillLength choice.
enum class FillLength { beat = 0, twoBeats, bar, twoBars };

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
    float bend = 0.5f;        // Micro: speed -3 .. +3 octaves (0.5 = 1x). Macro: zones
    float brk = 0.0f;         // Micro: Traverse slice index, or Silence duty 0 .. 90%. Macro: zones
    bool bendOnMacro = false, bendOnMicro = false;     // Macro: Bend on. Micro: reverse
    bool breakOnMacro = false, breakOnMicro = false;   // Macro: Break on. Micro: false = Traverse, true = Silence
    bool noteToSpeed = false; // MIDI note pitch sets the speed (C3 = 60 = 1x), on top of Bend
    // Stage 3: Macro mode.
    StereoMode stereo = StereoMode::unique;
    int seed = 0;             // 0: the session seed, dice never repeat. Otherwise reproducible (keyed on tick index)
    // Stage 4: Corrupt, at the end of the wet chain (before Width).
    float corrupt = 0.0f;     // 0..1. 0 = off (DJ Filter: the centre is off)
    CorruptType corruptType = CorruptType::decimate;
    CorruptSet corruptSet = CorruptSet::all;
    // Stage 5: modulation and gates.
    ModParams mod {};
    GateMode gateMode = GateMode::latching;   // latching: each note-on flips; momentary: flipped while held
    bool corruptAsReset = false;              // the Corrupt gate acts as Reset
    // Stage 7: the output safety clipper (transparent to -1 dBFS, never past 0 dBFS).
    bool safetyClip = true;
    float outputDb = 0.0f;   // output trim, -24..+12 dB, smoothed over 20 ms
    // Break this bar: a fill armed by triggerFill() (or the Break bar gate) waits for its place before
    // a bar line, plays glitched (wet, Macro dice, a fast host-synced clock) and lets go on the downbeat.
    // Auto: every N bars, the last part of the Nth bar breaks with a chance.
    FillLength fillLength = FillLength::bar;
    float fillAmount = 0.6f;   // 0..1: how hard the fill breaks
    int fillEvery = 0;         // 0 = by hand only; otherwise every 2, 4, 8 or 16 bars
    float fillChance = 0.5f;   // 0..1: the chance each auto fill plays
};

// A MIDI note (or CC) for the engine, at a sample offset in the next process() block.
struct NoteEvent
{
    int offset = 0, note = 60, velocity = 100;   // a CC: note = controller number, velocity = value
    bool on = true;
    bool cc = false;
};

// One clock tick, for the CLI's tick log and the UI.
struct TickEvent
{
    int64_t sample = 0;        // absolute output sample
    int64_t index = 0;         // the clock's tick index (grid position when host-synced)
    double windowStart = 0.0, windowLength = 0.0, period = 0.0;
    int repeats = 1;           // the Repeats knob
    bool frozen = false;
    // Per voice (L, R): what the dice decided.
    std::array<float, 2> speed { 1.0f, 1.0f };      // starting speed (signed)
    std::array<float, 2> octaves { 0.0f, 0.0f };    // target, for slews
    std::array<int, 2> pieces { 1, 1 };             // window cut into this many slices
    std::array<int, 2> slice { 0, 0 };
    std::array<float, 2> duty { 0.0f, 0.0f };
    std::array<uint8_t, 2> bendMask { 0, 0 };       // bit per BendOption
    std::array<uint8_t, 2> breakMask { 0, 0 };      // bit per BreakOption
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);   // allocates: message thread only
    void reset() noexcept;
    void setParams (const EngineParams& p) noexcept;
    void setTransport (const HostTransport& t) noexcept { transport_ = t; }   // once per block
    void triggerReset() noexcept { resetRequested_ = true; }
    // Break this bar: arm a fill (the button / its parameter's rising edge). Audio thread / offline.
    void triggerFill() noexcept { fillRequested_ = true; }
    // The seed Macro mode uses when the seed parameter is 0 (the plug-in picks one per session).
    void setSessionSeed (uint64_t s) noexcept { sessionSeed_ = s; reseed(); }

    // Macro tables, from any thread: copied in, taken up at the start of the next block. Returns false
    // (and changes nothing) if the previous set hasn't been taken up yet; try again later.
    bool setTables (const MacroTables& t) noexcept;
    const MacroTables& tables() const noexcept { return tables_; }   // audio thread / offline only

    // Notes for the next process() call, offsets ascending (up to kMaxNotes; extra ones are dropped).
    static constexpr int kMaxNotes = 512;
    void queueNote (const NoteEvent& e) noexcept { if (numNotes_ < kMaxNotes) notes_[(size_t) numNotes_++] = e; }
    void queueCC (int offset, int controller, int value) noexcept { queueNote ({ offset, controller, value, true, true }); }
    // Drops every gate's flipped state (Restore defaults). Any thread; taken up at the next block.
    void clearGates() noexcept { clearGates_.store (true, std::memory_order_release); }
    // Gate notes (Freeze, Bend, Break, Corrupt, Reset, Note clock: brief §3.10) never set the pitch.
    // A gate whose note changes while held is released. Audio thread (before process()).
    void setGateNotes (const std::array<int, kNumGates>& n) noexcept;
    // No gate is flipped and Corrupt hasn't been stepped (for the fuzz's stuck-gate check). Audio thread / offline.
    bool gatesIdle() const noexcept { return ! freezeFlip_ && ! bendFlip_[0] && ! bendFlip_[1] && ! breakFlip_[0] && ! breakFlip_[1] && corruptSteps_ == 0; }

    // in and out may alias. Any n.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

    // Tick log: a ring the CLI / UI reads (single consumer). Returns events since `from`.
    static constexpr int kTickRing = 1024;
    uint64_t ticksWritten() const noexcept { return tickCount_.load (std::memory_order_acquire); }
    const TickEvent& tick (uint64_t index) const noexcept { return ticks_[(size_t) (index % kTickRing)]; }

    const Window& window (int channel = 0) const noexcept { return voices_[(size_t) (channel & 1)].win; }
    // UI state (read on the UI thread; plain values written once per block).
    struct Status
    {
        std::atomic<float> speed { 1.0f };          // signed playback ratio (left voice)
        std::atomic<float> speedR { 1.0f };         // right voice
        std::atomic<bool> onOctave { true };        // Micro Bend sits on an exact octave (±5 cents)
        std::atomic<int> slice { 0 };
        std::atomic<uint32_t> sliceChanges { 0 };   // Traverse moved to another slice
        std::atomic<float> silenceDuty { 0.0f };
        std::atomic<int> corrupt { -1 };            // the Corrupt effect playing (-1 = bypassed)
        // Stage 5: knob + modulation, normalised 0..1, per ModDest ([0] unused), for the UI's rings;
        // the sources' values; gate states (freeze, bend, break flipped; corrupt steps).
        std::array<std::atomic<float>, kNumModDests> modulated {};
        std::array<std::atomic<float>, kNumModSources> sources {};
        std::atomic<bool> frozen { false }, bendOn { false }, breakOn { false };
        std::atomic<int> corruptType { 0 };
        // Stage 6, the buffer view: the window, each voice's slice and head, written once per block.
        std::atomic<double> windowStart { 0.0 }, windowLength { 0.0 }, period { 0.0 };
        std::array<std::atomic<double>, 2> head {};          // absolute buffer position
        std::array<std::atomic<float>, 2> headSpeed {}, duty {};
        std::array<std::atomic<int>, 2> pieces {}, sliceIndex {};
        std::array<std::atomic<uint32_t>, 2> jumps {};       // slice jumps (Traverse, Macro jump)
        std::atomic<int64_t> written { 0 };
        std::atomic<int> clockStep { 4 };
        std::atomic<uint32_t> clips { 0 };   // blocks in which the safety clipper acted
        std::atomic<int> fill { 0 };          // Break this bar: 0 idle, 1 armed (waiting for its place), 2 playing
        std::atomic<uint32_t> fills { 0 };    // fills started (auto ones included)
    };
    const Overview& overview() const noexcept { return overview_; }
    const Status& status() const noexcept { return status_; }
    const RecordBuffer& buffer() const noexcept { return buf_; }
    const Clock& clock() const noexcept { return clock_; }
    double sampleRate() const noexcept { return sr_; }
    int64_t samplesProcessed() const noexcept { return processed_; }

    // The Output trim's gain as last applied (it glides): for tests that check headroom before it.
    double outputGain() const noexcept { return outGain_; }

private:
    // One channel's read head(s) and Macro state.
    struct Voice
    {
        Window win;              // the main head's window / slice
        Playhead head;
        Window tailWin;          // the head being faded out after a jump
        Playhead tailHead;
        double tailLeft = 0.0, tailLen = 0.0, jumpFadeLen = 1.0;
        float tailGain0 = 1.0f;
        int baseRepeats = 1;     // the Repeats knob this window was cut with
        int pieces = 1;          // Macro Break: extra cuts per slice
        double duty = 0.0;       // Macro Break silence
        // Macro Bend speed: octaves gliding to a target, a direction, or a tape stop.
        double oct = 0.0, target = 0.0, glideCoef = 1.0, dir = 1.0;
        double stopLen = 0.0, stopPos = 0.0;
        // Vinyl click (Bend's everything zone).
        double clickPos = 0.0, clickLen = 0.0;
        float clickGain = 0.0f;
    };

    void onTick (int64_t tickIndex) noexcept;
    void jump (Voice& v, const Window& next) noexcept;   // switch the main head, fading out the old one
    float mainGain (const Voice& v) const noexcept;
    void spawnTail (Voice& v, double fadeDuration) noexcept;   // hand the main head to the fade-out tail
    void fadeInHead (Voice& v) noexcept;                      // after a mid-slice jump
    bool handleNote (const NoteEvent& e) noexcept;           // true: a Note clock pulse
    void applyEffective() noexcept;                           // p_ = knobs + gates + modulation
    void updateModulation (int offsetInBlock) noexcept;       // on the control grid
    void clearGateState() noexcept;
    void releaseGate (int gate) noexcept;   // momentary: the gate's last note is released
    void renderSegment (const float* inL, const float* inR, float* outL, float* outR, int from, int to) noexcept;
    void updateSpeed() noexcept;                              // per sample: Bend / note / reverse -> head speed
    double macroSpeed (Voice& v) noexcept;
    void resetMacroVoice (Voice& v) noexcept;
    int traverseSlice (int repeats) const noexcept;
    float silenceGain (const Voice& v) const noexcept;
    void reseed() noexcept;
    // Break this bar, at the start of each segment: arm, start and end fills at beat `beat` (the host's
    // ppq while playing, else a free-running count). True if the fill started or ended.
    bool updateFill (double beat, bool playing, double segBeats) noexcept;
    double fillBeats() const noexcept;
    Pcg32 tickRng (int stream, int64_t tickIndex) noexcept;

    double sr_ = 48000.0;
    double outGain_ = 1.0, outCoef_ = 1.0e-3;
    EngineParams base_;   // from the host (setParams)
    EngineParams p_;      // what plays: base_ with gates and modulation applied
    HostTransport transport_;
    RecordBuffer buf_;
    Clock clock_;
    std::array<Voice, 2> voices_ {};
    int pendingRepeats_ = 1;

    bool freezeWanted_ = false, forceWet_ = false, resetRequested_ = false;
    float mix_ = 0.5f, width_ = 0.5f, mixStep_ = 0.0f, widthStep_ = 0.0f;
    int smoothLeft_ = 0, smoothLen_ = 960;
    float mixTarget_ = 0.5f, widthTarget_ = 0.5f;
    int64_t processed_ = 0, curSample_ = 0;

    // Stage 2.
    std::array<NoteEvent, kMaxNotes> notes_ {};
    int numNotes_ = 0;
    std::array<int, kNumGates> gateNotes_ { 36, 37, 38, 39, 40, 41, -1 };   // C1 .. F1; Break bar unassigned
    int pitchNote_ = 60;
    bool hasPitch_ = false;
    double octSmoothed_ = 0.0, octCoef_ = 0.001;
    Mode lastMode_ = Mode::macro;
    Status status_;
    uint32_t sliceChanges_ = 0;

    // Stage 3. Streams: Bend L/R, Break L/R.
    enum { kBendStream = 0, kBreakStream = 2, kNumStreams = 4 };
    MacroTables tables_, pendingTables_;
    std::atomic<bool> tablesPending_ { false };
    std::array<Pcg32, kNumStreams> rngs_ {};
    uint64_t sessionSeed_ = 0x5eed;
    int lastSeed_ = 0;

    // Stage 4.
    Corrupt corrupt_;
    // Stage 6.
    Overview overview_;
    std::array<uint32_t, 2> jumps_ {};

    // Stage 5.
    std::array<Modulator, kNumModulators> mods_ {};
    EnvelopeFollower env_;
    std::array<float, kNumCcLanes> ccTarget_ {}, ccValue_ {};
    std::array<float, kNumModSources> sources_ {};
    ModOffsets offsets_ {};
    double ccCoef_ = 0.1;
    bool freezeFlip_ = false;
    std::array<bool, 2> bendFlip_ {}, breakFlip_ {};   // per mode (Macro, Micro)
    int corruptSteps_ = 0;
    std::array<int, kNumGates> gateHeld_ {};   // note-ons outstanding per gate (momentary: active while > 0)
    bool corruptStepped_ = false;              // the held Corrupt gate stepped the type (so release steps back)
    std::array<int, 2> heldMode_ {};           // the mode a held Bend / Break gate flipped (0 Macro, 1 Micro)
    int lastPlaying_ = -1;   // -1 unknown, so the first block isn't a "transport start"
    std::atomic<bool> clearGates_ { false };
    GateMode lastGateMode_ = GateMode::latching;

    // Break this bar.
    struct FillState
    {
        bool pending = false, active = false;
        double start = 0.0, end = 0.0;   // beats
        double expect = -1.0;            // where this segment's beat should be (a jump cancels the fill)
        double decided = -1.0e300;       // the auto fill whose dice have been rolled (its end, in beats)
        int domain = -1;                 // 1 host beats (playing), 0 free-running, -1 none yet
        bool resume = false;             // a press displaced the armed auto fill: decide it again, due or not
    };
    FillState fill_;
    bool fillRequested_ = false;
    double freeBeat_ = 0.0;
    double barLength_ = 4.0, barPhase_ = 0.0;
    Pcg32 fillRng_ {};
    uint32_t fills_ = 0;

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
void fault_set_macro (fault_engine*, int mode, float bend, float brk, int bendOn, int breakOn, int stereoShared, int seed);
void fault_set_corrupt (fault_engine*, float amount, int type, int originalThreeOnly);
void fault_set_transport (fault_engine*, int playing, double bpm, double ppq);
void fault_process (fault_engine*, const float* inL, const float* inR, float* outL, float* outR, int n);
}
