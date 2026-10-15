// Small complex FFT helper on interleaved samples (power-of-two sizes), built on the portable kernels in dsp_compat.h.
#pragma once
#include "dsp_compat.h"
#include "ring.h"
#include <vector>

namespace dect2 {

class Fft {
public:
    explicit Fft(int n) : n_(n), re_(n), im_(n) {
        log2n_ = 0;
        while ((1 << log2n_) < n) log2n_++;
    }
    int size() const { return n_; }
    // In-place; the inverse is unnormalised (sum over carriers), like the textbook IDFT without the 1/N
    void forward(cf32* x) { run(x, false); }
    void inverse(cf32* x) { run(x, true); }

private:
    void run(cf32* x, bool inv) {
        for (int i = 0; i < n_; i++) { re_[i] = x[i].real(); im_[i] = x[i].imag(); }
        fftSplit(re_.data(), im_.data(), log2n_, inv);
        for (int i = 0; i < n_; i++) x[i] = cf32(re_[i], im_[i]);
    }
    int n_, log2n_;
    std::vector<float> re_, im_;
};

} // namespace dect2
