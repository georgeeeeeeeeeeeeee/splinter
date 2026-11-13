#pragma once
// ~65 s stereo circular record buffer (brief §3.1). Writes continuously unless frozen, including outside
// the active window: everything else only changes how it's read. Positions are absolute sample counts
// since reset (64-bit), so a window captured earlier stays valid while writing continues.

#include <cstdint>
#include <vector>

namespace fault::dsp
{
class RecordBuffer
{
public:
    static constexpr double kSeconds = 65.0;

    void prepare (double sampleRate);   // allocates: message thread only
    void reset() noexcept;

    void write (float l, float r) noexcept;   // no-op while frozen
    void setFrozen (bool f) noexcept { frozen_ = f; }
    bool frozen() const noexcept { return frozen_; }

    int64_t written() const noexcept { return count_; }                     // absolute end (exclusive)
    int64_t oldest() const noexcept { return count_ > cap_ ? count_ - cap_ : 0; }
    int64_t capacity() const noexcept { return cap_; }

    // 4-point Hermite read at an absolute fractional position; positions outside what's held read 0.
    void read (double pos, float& l, float& r) const noexcept;

private:
    float at (const std::vector<float>& ch, int64_t i) const noexcept;
    std::vector<float> l_, r_;
    int64_t cap_ = 1, count_ = 0;
    bool frozen_ = false;
};
} // namespace fault::dsp
