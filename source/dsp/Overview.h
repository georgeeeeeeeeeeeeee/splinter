#pragma once
// A min/max overview of the record buffer for the UI's buffer view: one bucket per 32 samples, per
// channel, mirroring exactly what the buffer holds (it stops when the buffer is frozen). Written on the
// audio thread, read lock-free by the UI (relaxed atomics: a bucket being overwritten may tear between
// min and max, which only ever shows as one stale pixel column).

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace fault::dsp
{
class Overview
{
public:
    static constexpr int kBucket = 32;

    void prepare (int64_t capacitySamples)   // allocates: message thread only
    {
        n_ = capacitySamples / kBucket + 2;
        for (auto& a : v_) a = std::make_unique<std::atomic<float>[]> ((size_t) n_);
        reset();
    }
    void reset() noexcept
    {
        for (auto& a : v_)
            if (a) for (int64_t i = 0; i < n_; ++i) a[(size_t) i].store (0.0f, std::memory_order_relaxed);
        count_.store (0, std::memory_order_release);
        fill_ = 0;
        acc_ = { 1.0f, -1.0f, 1.0f, -1.0f };
    }
    void push (float l, float r) noexcept
    {
        acc_[0] = std::min (acc_[0], l); acc_[1] = std::max (acc_[1], l);
        acc_[2] = std::min (acc_[2], r); acc_[3] = std::max (acc_[3], r);
        if (++fill_ < kBucket || n_ <= 0)
            return;
        const int64_t c = count_.load (std::memory_order_relaxed);
        const auto i = (size_t) (c % n_);
        for (int k = 0; k < 4; ++k) v_[(size_t) k][i].store (acc_[(size_t) k], std::memory_order_relaxed);
        count_.store (c + 1, std::memory_order_release);
        fill_ = 0;
        acc_ = { 1.0f, -1.0f, 1.0f, -1.0f };
    }

    // UI side. Buckets are absolute: bucket b covers samples [b * kBucket, (b + 1) * kBucket).
    int64_t count() const noexcept { return count_.load (std::memory_order_acquire); }
    int64_t oldest() const noexcept { const auto c = count(); return c > n_ - 2 ? c - (n_ - 2) : 0; }
    // min L, max L, min R, max R of bucket b (zeros if it isn't held).
    void bucket (int64_t b, float* out4) const noexcept
    {
        if (n_ <= 0 || b < oldest() || b >= count()) { out4[0] = out4[1] = out4[2] = out4[3] = 0.0f; return; }
        for (int k = 0; k < 4; ++k) out4[k] = v_[(size_t) k][(size_t) (b % n_)].load (std::memory_order_relaxed);
    }

private:
    std::unique_ptr<std::atomic<float>[]> v_[4];
    int64_t n_ = 0;
    std::atomic<int64_t> count_ { 0 };
    int fill_ = 0;
    std::array<float, 4> acc_ { 1.0f, -1.0f, 1.0f, -1.0f };
};
} // namespace fault::dsp
