#include "dect2/dsp_compat.h"
#include "dect2/simd.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#if defined(__APPLE__) && !defined(DECT2_PORTABLE)
#define DECT2_USE_ACCELERATE 1
#include <Accelerate/Accelerate.h>
#include <algorithm>
#endif

namespace dect2 {

// Short filters (the channel-estimate interpolators have 16 taps): eight outputs at a time, one broadcast tap against eight
// neighbouring inputs per step, instead of one dot product (with its reduction and call overhead) per output.
DECT2_SIMD_BODY void convCorrShortBody(const float* a, const float* f, float* c, int strideC, int n, int p) {
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        const float* x = a + i;
        for (int k = 0; k < p; k++) { const float fk = f[k]; for (int l = 0; l < 8; l++) acc[l] += fk * x[k + l]; }
        for (int l = 0; l < 8; l++) c[(size_t)(i + l) * strideC] = acc[l];
    }
    for (; i < n; i++) {
        float s = 0;
        for (int k = 0; k < p; k++) s += f[k] * a[i + k];
        c[(size_t)i * strideC] = s;
    }
}
DECT2_AVX2_DISPATCH(void, convCorrShort, (const float* a, const float* f, float* c, int strideC, int n, int p), (a, f, c, strideC, n, p))

#ifdef DECT2_USE_ACCELERATE

void fftSplit(float* re, float* im, int log2n, bool inverse) {
    static std::atomic<FFTSetup> setups[24];
    FFTSetup s = setups[log2n].load(std::memory_order_acquire);
    if (!s) {
        FFTSetup fresh = vDSP_create_fftsetup(log2n, kFFTRadix2);
        FFTSetup expected = nullptr;
        if (setups[log2n].compare_exchange_strong(expected, fresh)) s = fresh;
        else { vDSP_destroy_fftsetup(fresh); s = expected; }
    }
    DSPSplitComplex sc{re, im};
    vDSP_fft_zip(s, &sc, 1, log2n, inverse ? FFT_INVERSE : FFT_FORWARD);
}
void convCorr(const float* a, const float* f, float* c, int strideC, int n, int p) {
    if (p <= 32) convCorrShort(a, f, c, strideC, n, p);
    else vDSP_conv(a, 1, f, 1, c, strideC, (vDSP_Length)n, (vDSP_Length)p);
}
void desamp(const float* a, int decim, const float* f, float* c, int n, int p) { vDSP_desamp(a, decim, f, c, (vDSP_Length)n, (vDSP_Length)p); }
void hannWindowNorm(float* w, int n) { vDSP_hann_window(w, n, vDSP_HANN_NORM); }
void syrkLowerT(const float* x, int rows, int n, float* a) { cblas_ssyrk(CblasRowMajor, CblasLower, CblasTrans, n, rows, 1.f, x, n, 0.f, a, n); }
void gemvT(const float* x, int rows, int n, const float* t, float* b) { cblas_sgemv(CblasRowMajor, CblasTrans, rows, n, 1.f, x, n, t, 1, 0.f, b, 1); }

#else  // ------------------------------------------------------------------ portable versions

namespace {
// Radix-2 decimation in time on split arrays. Twiddles are stored per stage (contiguous) so that the butterfly loops vectorise.
struct Plan {
    int n = 0;
    std::vector<uint32_t> rev;
    std::vector<float> wr, wi;   // stage with half-length h uses entries [h - 1, 2h - 1)
    explicit Plan(int log2n) : n(1 << log2n), rev(n), wr(n), wi(n) {
        for (int i = 0; i < n; i++) {
            uint32_t r = 0;
            for (int b = 0; b < log2n; b++) if (i & (1 << b)) r |= 1u << (log2n - 1 - b);
            rev[i] = r;
        }
        for (int h = 1; h < n; h <<= 1)
            for (int k = 0; k < h; k++) {
                const double a = -M_PI * k / h;
                wr[h - 1 + k] = (float)std::cos(a);
                wi[h - 1 + k] = (float)std::sin(a);
            }
    }
};

const Plan& planFor(int log2n) {
    static std::atomic<Plan*> plans[24];
    static std::mutex mu;
    Plan* p = plans[log2n].load(std::memory_order_acquire);
    if (p) return *p;
    std::lock_guard<std::mutex> lk(mu);
    p = plans[log2n].load(std::memory_order_relaxed);
    if (!p) { p = new Plan(log2n); plans[log2n].store(p, std::memory_order_release); }   // plans live for the whole run
    return *p;
}

DECT2_SIMD_BODY void forwardBody(float* __restrict re, float* __restrict im, int log2n) {
    const Plan& pl = planFor(log2n);
    const int n = pl.n;
    for (int i = 0; i < n; i++) {
        const uint32_t j = pl.rev[i];
        if (j > (uint32_t)i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    // first stage (half = 1): no multiplications
    for (int i = 0; i < n; i += 2) {
        const float ar = re[i], ai = im[i], br = re[i + 1], bi = im[i + 1];
        re[i] = ar + br; im[i] = ai + bi; re[i + 1] = ar - br; im[i + 1] = ai - bi;
    }
    for (int h = 2; h < n; h <<= 1) {
        const float* __restrict wr = &pl.wr[h - 1];
        const float* __restrict wi = &pl.wi[h - 1];
        for (int i = 0; i < n; i += 2 * h) {
            float* __restrict ar = re + i;
            float* __restrict ai = im + i;
            float* __restrict br = re + i + h;
            float* __restrict bi = im + i + h;
            for (int j = 0; j < h; j++) {
                const float tr = wr[j] * br[j] - wi[j] * bi[j];
                const float ti = wr[j] * bi[j] + wi[j] * br[j];
                br[j] = ar[j] - tr; bi[j] = ai[j] - ti;
                ar[j] += tr; ai[j] += ti;
            }
        }
    }
}
DECT2_AVX2_DISPATCH(void, forward, (float* __restrict re, float* __restrict im, int log2n), (re, im, log2n))
} // namespace

void fftSplit(float* re, float* im, int log2n, bool inverse) {
    if (log2n < 1) return;
    if (inverse) forward(im, re, log2n);   // ifft(x) = swap(fft(swap(x))): the unnormalised inverse
    else forward(re, im, log2n);
}

DECT2_SIMD_BODY void convCorrBody(const float* a, const float* f, float* c, int strideC, int n, int p) {
    if (p <= 32) { convCorrShortBody(a, f, c, strideC, n, p); return; }
    for (int i = 0; i < n; i++) {
        float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        const float* x = a + i;
        int k = 0;
        for (; k + 8 <= p; k += 8) for (int l = 0; l < 8; l++) acc[l] += f[k + l] * x[k + l];
        float s = ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
        for (; k < p; k++) s += f[k] * x[k];
        c[(size_t)i * strideC] = s;
    }
}
DECT2_AVX2_DISPATCH(void, convCorr, (const float* a, const float* f, float* c, int strideC, int n, int p), (a, f, c, strideC, n, p))

DECT2_SIMD_BODY void desampBody(const float* a, int decim, const float* f, float* c, int n, int p) {
    for (int i = 0; i < n; i++) {
        float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        const float* x = a + (size_t)i * decim;
        int k = 0;
        for (; k + 8 <= p; k += 8) for (int l = 0; l < 8; l++) acc[l] += f[k + l] * x[k + l];
        float s = ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
        for (; k < p; k++) s += f[k] * x[k];
        c[i] = s;
    }
}
DECT2_AVX2_DISPATCH(void, desamp, (const float* a, int decim, const float* f, float* c, int n, int p), (a, decim, f, c, n, p))

void hannWindowNorm(float* w, int n) {
    const double scale = std::sqrt(8.0 / 3.0);
    for (int i = 0; i < n; i++) w[i] = (float)(scale * 0.5 * (1.0 - std::cos(2.0 * M_PI * i / n)));
}

DECT2_SIMD_BODY void syrkLowerTBody(const float* x, int rows, int n, float* a) {
    std::memset(a, 0, sizeof(float) * (size_t)n * n);
    for (int r = 0; r < rows; r++) {
        const float* __restrict xr = x + (size_t)r * n;
        for (int i = 0; i < n; i++) {
            float* __restrict row = a + (size_t)i * n;
            const float xi = xr[i];
            for (int j = 0; j <= i; j++) row[j] += xi * xr[j];   // an axpy per row: vectorises
        }
    }
}
DECT2_AVX2_DISPATCH(void, syrkLowerT, (const float* x, int rows, int n, float* a), (x, rows, n, a))

DECT2_SIMD_BODY void gemvTBody(const float* x, int rows, int n, const float* t, float* b) {
    std::memset(b, 0, sizeof(float) * n);
    for (int r = 0; r < rows; r++) {
        const float* __restrict xr = x + (size_t)r * n;
        const float tr = t[r];
        for (int i = 0; i < n; i++) b[i] += tr * xr[i];
    }
}
DECT2_AVX2_DISPATCH(void, gemvT, (const float* x, int rows, int n, const float* t, float* b), (x, rows, n, t, b))

#endif

} // namespace dect2
