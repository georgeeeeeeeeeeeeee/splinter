#pragma once
// Modulation (brief §3.10): the CV replacement. Sources (four modulators, an envelope follower, four MIDI
// CC lanes) feed 8 route slots into six destinations; each destination gets a bipolar offset that's
// added to its knob (normalised 0..1) and clamped. Host automation moves the knob itself; modulation
// is computed on the audio thread and never written back.
//
// Everything runs on a fixed control grid (every kControlSamples of absolute output time), so the
// result never depends on the host's block sizes. The envelope follower runs every sample.

#include "dsp/Rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fault::dsp
{
inline constexpr int kControlSamples = 32;   // 0.67 ms at 48 kHz
inline constexpr int kNumModulators = 4, kNumCcLanes = 4, kNumRoutes = 8;

enum class ModShape { sine = 0, triangle, saw, square, sampleHold, smoothRandom };
inline constexpr int kNumModShapes = 6;

// Synced lengths, shortest first: { name, length in beats } (a bar is 4 beats).
struct SyncDivision { const char* name; double beats; };
inline constexpr std::array<SyncDivision, 17> kSyncDivisions { {
    { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/16D", 0.375 },
    { "1/8", 0.5 }, { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 }, { "1/4", 1.0 }, { "1/2T", 4.0 / 3.0 },
    { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1 bar", 4.0 }, { "2 bars", 8.0 }, { "4 bars", 16.0 }, { "8 bars", 32.0 },
} };

struct ModulatorParams
{
    ModShape shape = ModShape::sine;
    float rateHz = 1.0f;     // free rate, 0.01 .. 40 Hz
    int sync = -1;           // -1 free, else an index into kSyncDivisions
    float phase = 0.0f;      // 0..1 of a cycle
    bool bipolar = true;
};

struct EnvParams
{
    float attackMs = 10.0f, releaseMs = 200.0f, gainDb = 0.0f;
};

// Sources, in the order the route Source choice lists them (0 = none).
enum class ModSource { none = 0, mod1, mod2, mod3, mod4, env, cc1, cc2, cc3, cc4 };
inline constexpr int kNumModSources = 10;
enum class ModDest { none = 0, time, repeats, mix, bend, brk, corrupt };
inline constexpr int kNumModDests = 7;

struct Route
{
    ModSource source = ModSource::none;
    ModDest dest = ModDest::none;
    float depth = 0.0f;   // -1 .. +1
};

struct ModParams
{
    std::array<ModulatorParams, kNumModulators> mods {};
    EnvParams env {};
    std::array<Route, kNumRoutes> routes {};
    std::array<bool, kNumCcLanes> ccBipolar { false, false, false, false };
    std::array<int, kNumCcLanes> ccNumbers { 1, 16, 17, 18 };   // state, not parameters (learnable)
    // Attenuators (the hardware's Shift attenuators): scale all modulation into Bend, Break, Corrupt.
    float bendAtten = 1.0f, breakAtten = 1.0f, corruptAtten = 1.0f;
};

// Where the host is at a control point.
struct ModClock
{
    bool playing = false;
    double bpm = 120.0, ppq = 0.0;
};

class Modulator
{
public:
    void reset() noexcept { cycles_ = 0.0; }
    void reseed (uint64_t s) noexcept { seed_ = s; }
    // Value now (bipolar -1..1 or unipolar 0..1), then advances `samples`. Synced with the host playing,
    // the phase comes straight from the beat position, so it's locked to the bar and never drifts.
    float next (const ModulatorParams& p, const ModClock& c, double sampleRate, int samples) noexcept;
    float value() const noexcept { return value_; }
    // Synced with the host playing, the value is a function of the beat position alone: re-read it at
    // a given position without advancing anything (the engine does this on each clock tick, so a synced
    // S&H and a tick on the same grid line agree). Otherwise the current value.
    float at (const ModulatorParams& p, const ModClock& c) noexcept;

private:
    float shapeAt (const ModulatorParams& p, double cycles) const noexcept;
    double rnd (int64_t cycle) const noexcept;   // a fixed random value per cycle (seeded)
    double cycles_ = 0.0;
    uint64_t seed_ = 1;
    float value_ = 0.0f;
};

class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept { sr_ = sampleRate; }
    void reset() noexcept { env_ = 0.0; }
    void setParams (const EnvParams& p) noexcept;
    void process (float l, float r) noexcept
    {
        const double x = std::max (std::abs ((double) l), std::abs ((double) r));
        env_ += (x - env_) * (x > env_ ? att_ : rel_);
    }
    float value() const noexcept { return (float) std::min (1.0, env_ * gain_); }   // 0..1

private:
    double sr_ = 48000.0, env_ = 0.0, att_ = 0.01, rel_ = 0.001, gain_ = 1.0;
};

// Offsets per destination from source values (kNumModSources, [0] unused).
struct ModOffsets
{
    std::array<float, kNumModDests> d {};   // [0] unused
};
ModOffsets computeOffsets (const ModParams& p, const std::array<float, kNumModSources>& sources, bool micro) noexcept;
} // namespace fault::dsp
