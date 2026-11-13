#pragma once
// Active window + Repeats slicing (brief §3.3). Captured as read offsets only: no copying. On each tick
// the window is the most recent `period` samples (capped at what the buffer holds); Repeats cuts it into
// N equal slices and the playhead loops the current one. At N = 1 the whole window plays.

#include <algorithm>
#include <cstdint>

namespace fault::dsp
{
struct Window
{
    double start = 0.0;   // absolute buffer position of the window's first sample
    double length = 0.0;  // samples (fractional: periods aren't whole samples)
    int repeats = 1;      // N
    int slice = 0;        // current slice, 0 .. N-1

    bool valid() const noexcept { return length >= 2.0; }
    double sliceLength() const noexcept { return length / (double) std::max (1, repeats); }
    double sliceStart() const noexcept { return start + sliceLength() * (double) std::clamp (slice, 0, std::max (0, repeats - 1)); }
};
} // namespace fault::dsp
