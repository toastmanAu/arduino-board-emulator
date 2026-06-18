#pragma once
// Single-producer / single-consumer lock-free ring buffer.
//
// Built for the audio mirror tap: the SDL audio callback (one producer thread)
// pushes each computed sample; one HTTP streaming thread (the consumer) drains.
// push() must never block or take a lock — the audio thread is real-time. When
// the ring is full, push() drops the NEW sample (an audio mirror tolerates a
// dropped tail far better than a stalled callback).
//
// Capacity must be a power of two; one slot is reserved to disambiguate
// full from empty, so usable capacity is Cap - 1.
#include <atomic>
#include <cstddef>

namespace boardghost {

template <typename T, size_t Cap>
class SpscRing {
    static_assert(Cap >= 2 && (Cap & (Cap - 1)) == 0,
                  "Cap must be a power of two >= 2");

public:
    // Producer side. Returns false (drops v) when the ring is full.
    bool push(T v) {
        const size_t w = w_.load(std::memory_order_relaxed);
        const size_t next = (w + 1) & (Cap - 1);
        // acquire so we see the consumer's latest read position before deciding
        // the ring is full.
        if (next == r_.load(std::memory_order_acquire)) return false;
        buf_[w] = v;
        // release so the consumer that observes this w also sees buf_[w].
        w_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Copies up to `max` items into dst, returns the count.
    size_t drain(T* dst, size_t max) {
        size_t r = r_.load(std::memory_order_relaxed);
        const size_t w = w_.load(std::memory_order_acquire);
        size_t n = 0;
        while (n < max && r != w) {
            dst[n++] = buf_[r];
            r = (r + 1) & (Cap - 1);
        }
        r_.store(r, std::memory_order_release);
        return n;
    }

    // Approximate fill (consumer-safe; may race the producer by one item).
    size_t size() const {
        const size_t w = w_.load(std::memory_order_acquire);
        const size_t r = r_.load(std::memory_order_acquire);
        return (w - r) & (Cap - 1);
    }

private:
    T buf_[Cap];
    std::atomic<size_t> w_{0};
    std::atomic<size_t> r_{0};
};

}  // namespace boardghost
