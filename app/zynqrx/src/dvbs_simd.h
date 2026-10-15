// Dot products for the DVB-S/S2 filters: one set of taps against several data windows (n a multiple of 8).
// NEON and SSE2 intrinsics where available, plain code with independent partial sums elsewhere.
#pragma once
#include <cstddef>
#if (defined(__ARM_NEON) || defined(__aarch64__)) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#define DVBS_NEON 1
#if !defined(__aarch64__)
// 32-bit ARM (Cortex-A9 NEON has no fused multiply-add and no across-vector add; vmla is the plain multiply-accumulate)
#define vfmaq_f32 vmlaq_f32
static inline float vaddvq_f32(float32x4_t v) { float32x2_t s = vadd_f32(vget_low_f32(v), vget_high_f32(v)); return vget_lane_f32(vpadd_f32(s, s), 0); }
#endif
#elif defined(__SSE2__) && !defined(DECT2_NO_SIMD)
#include <emmintrin.h>
#define DVBS_SSE2 1
#endif

namespace dect2 {
namespace dvbs {

// out[k] = sum_i h[i] * a[k][i], k < 4
inline void dot4(const float* h, const float* a0, const float* a1, const float* a2, const float* a3, int n, float* out) {
#if defined(DVBS_NEON)
    float32x4_t s0 = vdupq_n_f32(0), s1 = s0, s2 = s0, s3 = s0, t0 = s0, t1 = s0, t2 = s0, t3 = s0;
    for (int i = 0; i < n; i += 8) {
        const float32x4_t ha = vld1q_f32(h + i), hb = vld1q_f32(h + i + 4);
        s0 = vfmaq_f32(s0, ha, vld1q_f32(a0 + i)); t0 = vfmaq_f32(t0, hb, vld1q_f32(a0 + i + 4));
        s1 = vfmaq_f32(s1, ha, vld1q_f32(a1 + i)); t1 = vfmaq_f32(t1, hb, vld1q_f32(a1 + i + 4));
        s2 = vfmaq_f32(s2, ha, vld1q_f32(a2 + i)); t2 = vfmaq_f32(t2, hb, vld1q_f32(a2 + i + 4));
        s3 = vfmaq_f32(s3, ha, vld1q_f32(a3 + i)); t3 = vfmaq_f32(t3, hb, vld1q_f32(a3 + i + 4));
    }
    out[0] = vaddvq_f32(vaddq_f32(s0, t0)); out[1] = vaddvq_f32(vaddq_f32(s1, t1));
    out[2] = vaddvq_f32(vaddq_f32(s2, t2)); out[3] = vaddvq_f32(vaddq_f32(s3, t3));
#elif defined(DVBS_SSE2)
    __m128 s0 = _mm_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
    for (int i = 0; i < n; i += 4) {
        const __m128 hv = _mm_loadu_ps(h + i);
        s0 = _mm_add_ps(s0, _mm_mul_ps(hv, _mm_loadu_ps(a0 + i)));
        s1 = _mm_add_ps(s1, _mm_mul_ps(hv, _mm_loadu_ps(a1 + i)));
        s2 = _mm_add_ps(s2, _mm_mul_ps(hv, _mm_loadu_ps(a2 + i)));
        s3 = _mm_add_ps(s3, _mm_mul_ps(hv, _mm_loadu_ps(a3 + i)));
    }
    auto hsum = [](__m128 v) { float f[4]; _mm_storeu_ps(f, v); return (f[0] + f[2]) + (f[1] + f[3]); };
    out[0] = hsum(s0); out[1] = hsum(s1); out[2] = hsum(s2); out[3] = hsum(s3);
#else
    const float* a[4] = {a0, a1, a2, a3};
    for (int k = 0; k < 4; k++) {
        float acc[8] = {};
        for (int i = 0; i < n; i += 8)
            for (int j = 0; j < 8; j++) acc[j] += h[i + j] * a[k][i + j];
        out[k] = ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
    }
#endif
}

// out[k] = sum_i h[i] * a[k][i], k < 2
inline void dot2(const float* h, const float* a0, const float* a1, int n, float* out) {
#if defined(DVBS_NEON)
    float32x4_t s0 = vdupq_n_f32(0), s1 = s0, t0 = s0, t1 = s0;
    for (int i = 0; i < n; i += 8) {
        const float32x4_t ha = vld1q_f32(h + i), hb = vld1q_f32(h + i + 4);
        s0 = vfmaq_f32(s0, ha, vld1q_f32(a0 + i)); t0 = vfmaq_f32(t0, hb, vld1q_f32(a0 + i + 4));
        s1 = vfmaq_f32(s1, ha, vld1q_f32(a1 + i)); t1 = vfmaq_f32(t1, hb, vld1q_f32(a1 + i + 4));
    }
    out[0] = vaddvq_f32(vaddq_f32(s0, t0)); out[1] = vaddvq_f32(vaddq_f32(s1, t1));
#elif defined(DVBS_SSE2)
    __m128 s0 = _mm_setzero_ps(), s1 = s0;
    for (int i = 0; i < n; i += 4) {
        const __m128 hv = _mm_loadu_ps(h + i);
        s0 = _mm_add_ps(s0, _mm_mul_ps(hv, _mm_loadu_ps(a0 + i)));
        s1 = _mm_add_ps(s1, _mm_mul_ps(hv, _mm_loadu_ps(a1 + i)));
    }
    auto hsum = [](__m128 v) { float f[4]; _mm_storeu_ps(f, v); return (f[0] + f[2]) + (f[1] + f[3]); };
    out[0] = hsum(s0); out[1] = hsum(s1);
#else
    const float* a[2] = {a0, a1};
    for (int k = 0; k < 2; k++) {
        float acc[8] = {};
        for (int i = 0; i < n; i += 8)
            for (int j = 0; j < 8; j++) acc[j] += h[i + j] * a[k][i + j];
        out[k] = ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
    }
#endif
}

} // namespace dvbs
} // namespace dect2
