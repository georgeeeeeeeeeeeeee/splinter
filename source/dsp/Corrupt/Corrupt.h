#pragma once
// Corrupt (brief §3.8): the end of the wet chain, one effect at a time.
//
// Every change of what's playing is a 30 ms crossfade between two "slots": a change of type, Decimate
// stepping to another preset, or the knob reaching or leaving bypass (0, or the centre for DJ Filter).
// The knob itself is smoothed over 20 ms for the continuous effects. A change that arrives mid-fade
// waits for the fade to finish. Bypassed and not fading, process() returns at once (no CPU).

#include "dsp/Corrupt/Decimate.h"
#include "dsp/Corrupt/Destroy.h"
#include "dsp/Corrupt/DjFilter.h"
#include "dsp/Corrupt/Dropout.h"
#include "dsp/Corrupt/Vinyl.h"

#include <cstdint>

namespace fault::dsp
{
enum class CorruptType { decimate = 0, dropout = 1, destroy = 2, djFilter = 3, vinyl = 4 };
enum class CorruptSet { all = 0, original3 = 1 };
inline constexpr int kNumCorruptTypes = 5;

class Corrupt
{
public:
    static constexpr double kFadeSeconds = 0.03, kSmoothSeconds = 0.02;

    void prepare (double sampleRate) noexcept;
    void reseed (uint64_t seed) noexcept;
    void reset() noexcept;
    void setParams (float amount, CorruptType type, CorruptSet set) noexcept;   // once per block
    void process (float& l, float& r) noexcept;

    // The type that actually plays: with the Original 3 set, DJ Filter and Vinyl fall back to Decimate.
    static CorruptType effectiveType (CorruptType t, CorruptSet s) noexcept;
    // The Corrupt gate's step (Stage 5): the next type in the set, wrapping.
    static CorruptType nextType (CorruptType t, CorruptSet s) noexcept;
    static int decimatePreset (float amount) noexcept;
    static bool isBypass (CorruptType t, float amount) noexcept;

    bool idle() const noexcept { return cur_.type < 0 && fadeLeft_ <= 0; }
    int playing() const noexcept { return cur_.type; }   // -1 bypassed, else a CorruptType
    float smoothedAmount() const noexcept { return (float) amount_; }

private:
    struct Slot
    {
        int type = -1;     // -1 = bypass
        int preset = 0;    // Decimate
        int inst = 0;      // which Decimate instance
        bool operator== (const Slot& o) const noexcept { return type == o.type && (type != (int) CorruptType::decimate || preset == o.preset); }
    };
    Slot wanted() const noexcept;
    void start (Slot& s) noexcept;
    void run (const Slot& s, float& l, float& r) noexcept;

    double sr_ = 48000.0, amount_ = 0.0, smoothCoef_ = 0.001;
    float target_ = 0.0f;
    CorruptType type_ = CorruptType::decimate;
    Slot cur_, prev_;
    int fadeLeft_ = 0, fadeLen_ = 1440;
    Decimate decimate_[2];
    Dropout dropout_;
    Destroy destroy_;
    DjFilter dj_;
    Vinyl vinyl_;
};
} // namespace fault::dsp
