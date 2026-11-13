#include "dsp/RecordBuffer.h"

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
void RecordBuffer::prepare (double sampleRate)
{
    cap_ = (int64_t) std::ceil (kSeconds * sampleRate) + 8;
    l_.assign ((size_t) cap_, 0.0f);
    r_.assign ((size_t) cap_, 0.0f);
    reset();
}

void RecordBuffer::reset() noexcept
{
    std::fill (l_.begin(), l_.end(), 0.0f);
    std::fill (r_.begin(), r_.end(), 0.0f);
    count_ = 0;
    frozen_ = false;
}

void RecordBuffer::write (float l, float r) noexcept
{
    if (frozen_)
        return;
    const auto i = (size_t) (count_ % cap_);
    l_[i] = l;
    r_[i] = r;
    ++count_;
}

float RecordBuffer::at (const std::vector<float>& ch, int64_t i) const noexcept
{
    if (i < oldest() || i >= count_)
        return 0.0f;
    return ch[(size_t) (i % cap_)];
}

void RecordBuffer::read (double pos, float& l, float& r) const noexcept
{
    const double fl = std::floor (pos);
    const auto i = (int64_t) fl;
    const float t = (float) (pos - fl);
    if (t == 0.0f)   // exact sample: no interpolation (a clean copy stays bit-exact)
    {
        l = at (l_, i);
        r = at (r_, i);
        return;
    }
    const auto hermite = [t] (float y0, float y1, float y2, float y3) {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    };
    l = hermite (at (l_, i - 1), at (l_, i), at (l_, i + 1), at (l_, i + 2));
    r = hermite (at (r_, i - 1), at (r_, i), at (r_, i + 1), at (r_, i + 2));
}
} // namespace fault::dsp
