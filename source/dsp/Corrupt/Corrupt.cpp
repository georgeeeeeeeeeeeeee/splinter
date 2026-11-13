#include "dsp/Corrupt/Corrupt.h"

#include "dsp/SliceEnvelope.h"

namespace fault::dsp
{
void Corrupt::prepare (double sampleRate) noexcept
{
    sr_ = sampleRate;
    smoothCoef_ = 1.0 - std::exp (-1.0 / (kSmoothSeconds * sampleRate));
    fadeLen_ = std::max (1, (int) (kFadeSeconds * sampleRate));
    for (auto& d : decimate_) d.prepare (sampleRate);
    dropout_.prepare (sampleRate);
    destroy_.prepare (sampleRate);
    dj_.prepare (sampleRate);
    vinyl_.prepare (sampleRate);
    reset();
}

void Corrupt::reseed (uint64_t seed) noexcept
{
    decimate_[0].reseed (mix64 (seed ^ 0xdec1));
    decimate_[1].reseed (mix64 (seed ^ 0xdec2));
    dropout_.reseed (mix64 (seed ^ 0xd809));
    vinyl_.reseed (mix64 (seed ^ 0x7e1));
}

void Corrupt::reset() noexcept
{
    amount_ = (double) target_;
    prev_ = Slot {};
    fadeLeft_ = 0;
    cur_ = wanted();
    if (cur_.type >= 0)
        start (cur_);
}

CorruptType Corrupt::effectiveType (CorruptType t, CorruptSet s) noexcept
{
    const int i = std::clamp ((int) t, 0, kNumCorruptTypes - 1);
    return s == CorruptSet::original3 && i > (int) CorruptType::destroy ? CorruptType::decimate : (CorruptType) i;
}

CorruptType Corrupt::nextType (CorruptType t, CorruptSet s) noexcept
{
    const int n = s == CorruptSet::original3 ? 3 : kNumCorruptTypes;
    return (CorruptType) (((int) effectiveType (t, s) + 1) % n);
}

int Corrupt::decimatePreset (float amount) noexcept
{
    const int n = (int) kDecimatePresets.size();
    return std::clamp ((int) std::floor (std::clamp (amount, 0.0f, 1.0f) * (float) n), 0, n - 1);
}

bool Corrupt::isBypass (CorruptType t, float amount) noexcept
{
    return t == CorruptType::djFilter ? DjFilter::bypassed (amount) : amount <= 0.0f;
}

void Corrupt::setParams (float amount, CorruptType type, CorruptSet set) noexcept
{
    target_ = std::isfinite (amount) ? std::clamp (amount, 0.0f, 1.0f) : 0.0f;
    type_ = effectiveType (type, set);
}

Corrupt::Slot Corrupt::wanted() const noexcept
{
    Slot s;
    if (isBypass (type_, target_))
        return s;
    s.type = (int) type_;
    if (type_ == CorruptType::decimate)
        s.preset = decimatePreset (target_);
    return s;
}

void Corrupt::start (Slot& s) noexcept
{
    // A fresh start for the effect coming in (state from its last run is stale). Decimate gets whichever
    // instance the outgoing slot isn't using.
    switch ((CorruptType) s.type)
    {
        case CorruptType::decimate:
            s.inst = prev_.type == (int) CorruptType::decimate ? 1 - prev_.inst : 0;
            decimate_[s.inst].reset (s.preset);
            break;
        case CorruptType::dropout: dropout_.reset(); break;
        case CorruptType::destroy: destroy_.reset(); break;
        case CorruptType::djFilter: dj_.reset(); break;
        case CorruptType::vinyl: vinyl_.reset(); break;
    }
}

void Corrupt::run (const Slot& s, float& l, float& r) noexcept
{
    const float a = (float) amount_;
    switch ((CorruptType) s.type)
    {
        case CorruptType::decimate: decimate_[s.inst].process (l, r); break;
        case CorruptType::dropout: dropout_.process (l, r, a); break;
        case CorruptType::destroy: destroy_.process (l, r, a); break;
        case CorruptType::djFilter: dj_.process (l, r, a); break;
        case CorruptType::vinyl: vinyl_.process (l, r, a); break;
        default: break;
    }
}

void Corrupt::process (float& l, float& r) noexcept
{
    if (fadeLeft_ <= 0)
    {
        if (Slot w = wanted(); ! (w == cur_))
        {
            // The outgoing slot keeps playing under the fade. Coming out of bypass the smoothed knob is
            // still at (or near) the effect's identity point, so it eases in as well as fading in.
            prev_ = cur_;
            cur_ = w;
            if (cur_.type >= 0)
                start (cur_);   // a different effect, or Decimate's other instance
            fadeLeft_ = fadeLen_;
        }
    }
    amount_ += ((double) target_ - amount_) * smoothCoef_;
    if (std::abs ((double) target_ - amount_) < 1.0e-6)
        amount_ = (double) target_;
    if (cur_.type < 0 && fadeLeft_ <= 0)
        return;

    float cl = l, cr = r;
    if (cur_.type >= 0)
        run (cur_, cl, cr);
    if (fadeLeft_ > 0)
    {
        float pl = l, pr = r;
        if (prev_.type >= 0)
            run (prev_, pl, pr);
        const float g = SliceEnvelope::rise (1.0 - (double) fadeLeft_ / (double) fadeLen_);
        cl = pl + (cl - pl) * g;
        cr = pr + (cr - pr) * g;
        --fadeLeft_;
    }
    l = cl;
    r = cr;
}
} // namespace fault::dsp
