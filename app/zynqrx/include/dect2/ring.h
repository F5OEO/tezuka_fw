// Single-producer / single-consumer lock-free ring of complex float samples.
#pragma once
#include <atomic>
#include <complex>
#include <cstddef>
#include <cstring>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <chrono>

namespace dect2 {

using cf32 = std::complex<float>;

class IqRing {
public:
    explicit IqRing(size_t capacityPow2 = 1u << 24) : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

    // Producer. Returns number written; samples that do not fit are counted as dropped.
    size_t write(const cf32* src, size_t n) {
        size_t w = w_.load(std::memory_order_relaxed);
        size_t r = r_.load(std::memory_order_acquire);
        size_t freeN = buf_.size() - (w - r);
        size_t m = n < freeN ? n : freeN;
        copyIn(w, src, m);
        w_.store(w + m, std::memory_order_release);
        if (m < n) dropped_.fetch_add(n - m, std::memory_order_relaxed);
        // when and how much the radio had delivered (a seqlock: the reader gets a pair from the same write)
        const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        seq_.fetch_add(1, std::memory_order_acq_rel);
        offered_.store(offered_.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
        writeNs_.store(ns, std::memory_order_relaxed);
        lastN_.store(n, std::memory_order_relaxed);
        seq_.fetch_add(1, std::memory_order_release);
        return m;
    }

    // The total the producer has offered (written and dropped), the steady_clock time of its last write in nanoseconds and that write's
    // size (a radio writes one USB transfer at a time); false while nothing was written yet. The engine compares them with the radio's
    // sample rate to find samples that never came out of the radio.
    bool lastWrite(uint64_t& offered, int64_t& ns, size_t& n) const {
        for (;;) {
            const uint64_t s0 = seq_.load(std::memory_order_acquire);
            if (s0 & 1) continue;
            offered = offered_.load(std::memory_order_relaxed);
            ns = writeNs_.load(std::memory_order_relaxed);
            n = lastN_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s0) return s0 != 0;
        }
    }

    size_t capacity() const { return buf_.size(); }

    // Consumer.
    size_t read(cf32* dst, size_t n) {
        size_t r = r_.load(std::memory_order_relaxed);
        size_t w = w_.load(std::memory_order_acquire);
        size_t avail = w - r;
        size_t m = n < avail ? n : avail;
        copyOut(r, dst, m);
        r_.store(r + m, std::memory_order_release);
        return m;
    }

    // Consumer. When the consumer is too slow the ring fills and the producer can only write the few samples that happen to be free,
    // which spreads tiny gaps over every frame and nothing decodes. Throwing the whole backlog away in one go leaves one clean gap and
    // intact signal in between. Keeps the newest `keep` samples and returns how many were discarded (counted as dropped).
    size_t skipToNewest(size_t keep) {
        size_t r = r_.load(std::memory_order_relaxed);
        size_t w = w_.load(std::memory_order_acquire);
        size_t avail = w - r;
        if (avail <= keep) return 0;
        size_t skip = avail - keep;
        r_.store(r + skip, std::memory_order_release);
        dropped_.fetch_add(skip, std::memory_order_relaxed);
        return skip;
    }

    // The policy built on skipToNewest(): when more than half of the ring is backlog, jump to the newest tenth of it.
    size_t dropBacklog() { return available() > buf_.size() / 2 ? skipToNewest(buf_.size() / 16) : 0; }

    size_t available() const { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    // samples the producer never got (a network radio's connection was down): counted as dropped
    void noteLost(uint64_t n) { dropped_.fetch_add(n, std::memory_order_relaxed); }
    void clear() { r_.store(w_.load(std::memory_order_acquire), std::memory_order_release); }
    void resetDropped() { dropped_.store(0, std::memory_order_relaxed); }

private:
    // the ring is a power of two: a copy is at most two pieces (up to the end of the storage, then from its start)
    void copyIn(size_t pos, const cf32* src, size_t m) {
        const size_t at = pos & mask_, first = std::min(m, buf_.size() - at);
        std::memcpy(&buf_[at], src, first * sizeof(cf32));
        if (m > first) std::memcpy(&buf_[0], src + first, (m - first) * sizeof(cf32));
    }
    void copyOut(size_t pos, cf32* dst, size_t m) const {
        const size_t at = pos & mask_, first = std::min(m, buf_.size() - at);
        std::memcpy(dst, &buf_[at], first * sizeof(cf32));
        if (m > first) std::memcpy(dst + first, &buf_[0], (m - first) * sizeof(cf32));
    }
    std::vector<cf32> buf_;
    size_t mask_;
    std::atomic<size_t> w_{0}, r_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> seq_{0}, offered_{0};
    std::atomic<int64_t> writeNs_{0};
    std::atomic<size_t> lastN_{0};
};

} // namespace dect2
