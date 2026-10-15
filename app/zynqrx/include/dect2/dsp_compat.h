// Small DSP kernels the receivers need, with portable implementations (plain C++, written so that compilers vectorise them) and, on
// macOS, Accelerate versions that are used by default. Define DECT2_PORTABLE to force the portable code everywhere.
#pragma once
#include <cstddef>

namespace dect2 {

// In-place complex FFT of 1 << log2n points on split arrays. Natural order in and out. Forward uses exp(-j...), inverse exp(+j...);
// neither is normalised (the inverse is the plain sum). Thread safe.
void fftSplit(float* re, float* im, int log2n, bool inverse);

// c[i * strideC] = sum_{k < p} f[k] * a[i + k]   for i < n   (a correlation, what a polyphase FIR sum needs)
void convCorr(const float* a, const float* f, float* c, int strideC, int n, int p);

// c[i] = sum_{k < p} f[k] * a[i * decim + k]   for i < n   (decimating FIR)
void desamp(const float* a, int decim, const float* f, float* c, int n, int p);

// Hann window 0.5 (1 - cos(2 pi i / n)) scaled to unit rms (sqrt(8/3)), i = 0..n-1
void hannWindowNorm(float* w, int n);

// a = x^T x (lower triangle, row-major n x n) and b = x^T t for a row-major x of `rows` x n floats
void syrkLowerT(const float* x, int rows, int n, float* a);
void gemvT(const float* x, int rows, int n, const float* t, float* b);

} // namespace dect2
