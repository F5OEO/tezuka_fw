// Is there a linearly modulated signal in the input? A cheap test for input at two samples per symbol (the radio's own root raised cosine filter, or any
// signal sampled at twice its symbol rate), made before any timing recovery or search is spent on it.
//
// A signal with excess bandwidth has a line at the symbol rate in its second order statistics. At exactly two samples per symbol that line shows up as the
// alternating sum of the powers |x[n]|^2 (lag 0) and of the products x[n] conj(x[n-1]) (lag 1); with the timing phase between the clocks drifting, each of the
// two swings between zero and its maximum, in quadrature with each other, so their sum does not (measured on radio recordings: 7.3 to 9.9 for a 6.5 dB signal,
// 2.6 to 4.1 for a 3 dB one, 13 to 14.6 at 25 dB, against 1.2 to 2.0 for noise, median 1.5). Each is the squared magnitude of the sum over a block, divided
// by what it would be for noise: the sum of the squared powers (lag 0), the sum of |x[n]|^2 |x[n-1]|^2 (lag 1).
#pragma once
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace dect2 {
namespace dvbs {

class CyclicPresence {
public:
    static constexpr int kBlock = 4096;            // samples of one block
    static constexpr int kEvery = 4;               // a block out of this many is used (the rest is skipped: a quarter of the cost)
    static constexpr float kThreshold = 2.3f;      // statistic at which a signal is taken to be there (noise: 1.5, rarely above 2)

    void reset() { a0_ = d0_ = a1_ = d1_ = 0; blocks_ = 0; skip_ = 0; }
    // x: samples at two per symbol; any number of them
    void update(const std::complex<float>* x, size_t n) {
        size_t i = 0;
        while (i + kBlock + 1 <= n) {
            if (skip_ == 0) block(x + i);
            skip_ = (skip_ + 1) % kEvery;
            i += kBlock;
        }
    }
    bool ready() const { return blocks_ >= 24; }
    float value() const { return (d0_ > 0 ? (float)(a0_ / d0_) : 0.f) + (d1_ > 0 ? (float)(a1_ / d1_) : 0.f); }
    bool present() const { return ready() && value() >= kThreshold; }

private:
    void block(const std::complex<float>* x) {
        float s0 = 0, d0 = 0, s1r = 0, s1i = 0, d1 = 0;
        const float* f = reinterpret_cast<const float*>(x);
        float pPrev = f[0] * f[0] + f[1] * f[1];
        for (int k = 1; k <= kBlock; k++) {
            const float re = f[2 * k], im = f[2 * k + 1], pr = f[2 * k - 2], pi = f[2 * k - 1];
            const float p = re * re + im * im;
            const float sg = (k & 1) ? 1.f : -1.f;
            s0 += sg * p; d0 += p * p;
            // x[k] conj(x[k-1])
            s1r += sg * (re * pr + im * pi); s1i += sg * (im * pr - re * pi);
            d1 += p * pPrev;
            pPrev = p;
        }
        // the sums are over 32 blocks' worth of memory: an exponential forgetting
        constexpr double keep = 1.0 - 1.0 / 32.0;
        a0_ = a0_ * keep + (double)s0 * s0; d0_ = d0_ * keep + d0;
        a1_ = a1_ * keep + (double)s1r * s1r + (double)s1i * s1i; d1_ = d1_ * keep + d1;
        if (blocks_ < 1000000) blocks_++;
    }
    double a0_ = 0, d0_ = 0, a1_ = 0, d1_ = 0;
    int blocks_ = 0, skip_ = 0;
};

} // namespace dvbs
} // namespace dect2
