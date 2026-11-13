#pragma once
// One read head (brief §3.4): a fractional position inside the current slice, a signed speed (negative =
// reverse), wrapping inside the slice in either direction. Slew and tape stop arrive with Macro Bend
// (Stage 3); in Stage 1 the speed is 1.

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
struct Playhead
{
    double offset = 0.0;     // within the slice, 0 .. sliceLength
    double progress = 0.0;   // playback samples since this slice (re)started
    double speed = 1.0;

    // Advances one sample; true if it wrapped (a slice boundary).
    bool advance (double sliceLength) noexcept
    {
        offset += speed;
        progress += 1.0;
        if (sliceLength <= 0.0)
            return false;
        if (offset >= sliceLength || offset < 0.0)
        {
            offset -= sliceLength * std::floor (offset / sliceLength);
            progress = 0.0;
            return true;
        }
        return false;
    }
    double duration (double sliceLength) const noexcept { return sliceLength / std::max (1.0e-6, std::abs (speed)); }
};
} // namespace fault::dsp
