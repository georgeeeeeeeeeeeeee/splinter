#pragma once
// One read head (brief §3.4): a fractional position inside the current slice and a signed speed
// (negative = reverse), wrapping inside the slice in either direction. `fadeIn` counts samples since a
// mid-slice jump (Traverse skip, direction change), for the jump's fade-in. Slew and tape stop arrive
// with Macro Bend (Stage 3).

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
struct Playhead
{
    double offset = 0.0;    // within the slice, 0 .. sliceLength
    double speed = 1.0;
    double fadeIn = 1.0e30; // samples since a mid-slice jump (huge = none pending)

    // Advances one sample; true if it wrapped (a slice boundary).
    bool advance (double sliceLength) noexcept
    {
        offset += speed;
        fadeIn += 1.0;
        if (sliceLength <= 0.0)
            return false;
        if (offset >= sliceLength || offset < 0.0)
        {
            offset -= sliceLength * std::floor (offset / sliceLength);
            return true;
        }
        return false;
    }
    double duration (double sliceLength) const noexcept { return sliceLength / std::max (1.0e-6, std::abs (speed)); }
    // Playback samples since the slice's playback start (its start going forward, its end in reverse).
    double sinceStart (double sliceLength) const noexcept { return (speed >= 0.0 ? offset : sliceLength - offset) / std::max (1.0e-6, std::abs (speed)); }
};
} // namespace fault::dsp
