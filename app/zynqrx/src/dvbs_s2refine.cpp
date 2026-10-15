// DVB-S2 receiver: decisions and phase refinement, see dvbs_s2refine.h.
#include "dvbs_s2refine.h"
#include "dect2/dvbs_s2.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#if (defined(__ARM_NEON) || defined(__aarch64__)) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#define S2REFINE_NEON 1
#endif

namespace dect2 {
namespace dvbs {

void NearestPoint::build(const cf32* p, int count) {
    pts_ = p; n_ = count;
    float mx = 0.f;
    for (int i = 0; i < count; i++) mx = std::max(mx, std::abs(p[i]));
    lim_ = std::max(1.5f, mx * 1.35f);
    grid_ = count <= 32 ? 128 : count <= 64 ? 256 : 512;
    scale_ = (float)grid_ / (2.f * lim_);
    nbr_.assign((size_t)count * 2, 0);
    for (int i = 0; i < count; i++) {
        float d1 = 1e30f, d2 = 1e30f; int j1 = i, j2 = i;
        for (int j = 0; j < count; j++) {
            if (j == i) continue;
            const float d = std::norm(p[i] - p[j]);
            if (d < d1) { d2 = d1; j2 = j1; d1 = d; j1 = j; }
            else if (d < d2) { d2 = d; j2 = j; }
        }
        nbr_[(size_t)i * 2] = (uint8_t)j1; nbr_[(size_t)i * 2 + 1] = (uint8_t)j2;
    }
    idx_.assign((size_t)grid_ * grid_, 0);
    idx2_.assign((size_t)grid_ * grid_, 0);
    for (int iy = 0; iy < grid_; iy++)
        for (int ix = 0; ix < grid_; ix++) {
            const float re = ((float)ix + 0.5f) / scale_ - lim_, im = ((float)iy + 0.5f) / scale_ - lim_;
            float bd = 1e30f, bd2 = 1e30f; int bi = 0, bi2 = 0;
            for (int i = 0; i < count; i++) {
                const float a = re - p[i].real(), b = im - p[i].imag(), d = a * a + b * b;
                if (d < bd) { bd2 = bd; bi2 = bi; bd = d; bi = i; }
                else if (d < bd2) { bd2 = d; bi2 = i; }
            }
            idx_[(size_t)iy * grid_ + (size_t)ix] = (uint8_t)bi;
            idx2_[(size_t)iy * grid_ + (size_t)ix] = (uint8_t)bi2;
        }
}

const NearestPoint& nearestPoint(int mod, int rate) {
    static std::mutex mu;
    static std::map<int, NearestPoint*> cache;
    std::lock_guard<std::mutex> lk(mu);
    const int key = mod < 2 && !s2IsS2x(rate) ? mod * 1000 : mod * 1000 + rate;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    auto* np = new NearestPoint();          // kept for the life of the program: callers keep references
    np->build(s2Constellation(mod, rate), s2ConstellationSize(mod));
    cache[key] = np;
    return *np;
}

namespace {
inline float fexpNeg(float x) {        // e^x for x <= 0
    x = std::max(x, -60.f);
    const float t = x * 1.44269504f;
    int ti = (int)t;                       // floor without the library call (t is negative or zero)
    ti -= t < (float)ti;
    const float fi = (float)ti, f = t - fi;
    const float p = 1.f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * 0.00961813f)));
    union { float f; int32_t i; } u;
    u.i = ((int32_t)fi + 127) << 23;
    return p * u.f;
}
inline float ftanhf(float x) {
    const float ax = std::min(std::fabs(x), 12.f);
    const float e = fexpNeg(-2.f * ax);
    const float t = (1.f - e) / (1.f + e);
    return x < 0 ? -t : t;
}

#ifdef S2REFINE_NEON
// tanh of four values: e^(-2|x|) from a polynomial on the fractional part of the base-2 exponent, then (1 - e) / (1 + e) with a Newton reciprocal
inline float32x4_t tanh4(float32x4_t x) {
    const float32x4_t ax = vminq_f32(vabsq_f32(x), vdupq_n_f32(12.f));
    float32x4_t t = vmulq_n_f32(ax, -2.88539008f);                         // -2|x| / ln 2, <= 0
    int32x4_t ti = vcvtq_s32_f32(t);                                       // truncates toward zero: step down where that rounded up
    ti = vaddq_s32(ti, vreinterpretq_s32_u32(vcltq_f32(t, vcvtq_f32_s32(ti))));
    const float32x4_t f = vsubq_f32(t, vcvtq_f32_s32(ti));
    float32x4_t p = vmlaq_n_f32(vdupq_n_f32(0.05550411f), f, 0.00961813f);   // Horner: 1 + f (c1 + f (c2 + f (c3 + f c4)))
    p = vmlaq_f32(vdupq_n_f32(0.24022651f), f, p);
    p = vmlaq_f32(vdupq_n_f32(0.69314718f), f, p);
    p = vmlaq_f32(vdupq_n_f32(1.f), f, p);
    const float32x4_t e = vmulq_f32(p, vreinterpretq_f32_s32(vshlq_n_s32(vaddq_s32(ti, vdupq_n_s32(127)), 23)));
    const float32x4_t den = vaddq_f32(vdupq_n_f32(1.f), e);
    float32x4_t r = vrecpeq_f32(den);
    r = vmulq_f32(vrecpsq_f32(den, r), r);
    r = vmulq_f32(vrecpsq_f32(den, r), r);
    const float32x4_t th = vmulq_f32(vsubq_f32(vdupq_n_f32(1.f), e), r);
    const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u));
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(th), sign));
}
// Soft QPSK block: sums x conj(E[x]) over x[k0, k1), four symbols at a time
inline void softQpskSum(const cf32* x, int k0, int k1, float invS, float& ar, float& ai) {
    const float32x4_t s = vdupq_n_f32(0.70710678f * invS), h = vdupq_n_f32(0.70710678f);
    float32x4_t accR = vdupq_n_f32(0.f), accI = accR;
    int k = k0;
    for (; k + 4 <= k1; k += 4) {
        const float32x4x2_t v = vld2q_f32(reinterpret_cast<const float*>(x + k));
        const float32x4_t er = vmulq_f32(h, tanh4(vmulq_f32(s, v.val[0]))), ei = vmulq_f32(h, tanh4(vmulq_f32(s, v.val[1])));
        accR = vmlaq_f32(vmlaq_f32(accR, v.val[0], er), v.val[1], ei);       // xr er + xi ei
        accI = vmlsq_f32(vmlaq_f32(accI, v.val[1], er), v.val[0], ei);       // xi er - xr ei
    }
    float r[4], i[4];
    vst1q_f32(r, accR); vst1q_f32(i, accI);
    ar = (r[0] + r[1]) + (r[2] + r[3]); ai = (i[0] + i[1]) + (i[2] + i[3]);
    for (; k < k1; k++) {
        const float xr = x[k].real(), xi = x[k].imag();
        const float er = 0.70710678f * ftanhf(0.70710678f * invS * xr), ei = 0.70710678f * ftanhf(0.70710678f * invS * xi);
        ar += xr * er + xi * ei;
        ai += xi * er - xr * ei;
    }
}
#endif

#ifdef S2REFINE_NEON
// x[j] *= exp(-j (e0 + slope (j - k))) for j in [k, kEnd): four rotors side by side (each a symbol apart by four steps), renewed here at the start of every stretch
inline void rotateRamp(cf32* x, int k, int kEnd, float e0, float slope) {
    const cf32 stp(std::cos(slope), -std::sin(slope));
    cf32 r[4];
    r[0] = cf32(std::cos(e0), -std::sin(e0));
    for (int l = 1; l < 4; l++) r[l] = r[l - 1] * stp;
    const cf32 s2 = stp * stp, s4 = s2 * s2;
    float rr[4] = {r[0].real(), r[1].real(), r[2].real(), r[3].real()}, ri[4] = {r[0].imag(), r[1].imag(), r[2].imag(), r[3].imag()};
    float32x4_t vr = vld1q_f32(rr), vi = vld1q_f32(ri);
    const float32x4_t cr = vdupq_n_f32(s4.real()), ci = vdupq_n_f32(s4.imag());
    int j = k;
    for (; j + 4 <= kEnd; j += 4) {
        float32x4x2_t v = vld2q_f32(reinterpret_cast<float*>(x + j));
        const float32x4_t yr = vmlsq_f32(vmulq_f32(v.val[0], vr), v.val[1], vi);     // xr rr - xi ri
        const float32x4_t yi = vmlaq_f32(vmulq_f32(v.val[0], vi), v.val[1], vr);     // xr ri + xi rr
        v.val[0] = yr; v.val[1] = yi;
        vst2q_f32(reinterpret_cast<float*>(x + j), v);
        const float32x4_t nr = vmlsq_f32(vmulq_f32(vr, cr), vi, ci), ni = vmlaq_f32(vmulq_f32(vr, ci), vi, cr);
        vr = nr; vi = ni;
    }
    if (j < kEnd) {
        float tr[4], ti[4];
        vst1q_f32(tr, vr); vst1q_f32(ti, vi);
        cf32 rot(tr[0], ti[0]);
        for (; j < kEnd; j++) { x[j] *= rot; rot *= stp; }
    }
}
#endif
} // namespace

float s2RefinePhase(cf32* x, int n, int mod, int rate, float sigma2, int W, const std::vector<PhaseMark>& marks, int iterations) {
    (void)marks;
    if (n < 4 * W || W < 4) return 0.f;
    const cf32* c = s2Constellation(mod, rate);
    const int P = s2ConstellationSize(mod);
    const NearestPoint& np = nearestPoint(mod, rate);
    float dmin = 1e9f;
    for (int i = 0; i < P; i++) for (int j = i + 1; j < P; j++) dmin = std::min(dmin, std::abs(c[i] - c[j]));
    const bool soft = sigma2 > (dmin * 0.125f) * (dmin * 0.125f);
    const float invS = 1.f / std::max(sigma2, 1e-4f);
    const int nb = n / W;
    std::vector<cf32> acc((size_t)nb), sm((size_t)nb);
    double sq = 0;
    for (int it = 0; it < iterations; it++) {
        for (int b = 0; b < nb; b++) {
            const int k0 = b * W, k1 = b == nb - 1 ? n : k0 + W;
            float ar = 0, ai = 0;
#ifdef S2REFINE_NEON
            if (soft && mod == kQpsk) { softQpskSum(x, k0, k1, invS, ar, ai); acc[(size_t)b] = cf32(ar, ai); continue; }
#endif
            for (int k = k0; k < k1; k++) {
                const float xr = x[k].real(), xi = x[k].imag();
                float er, ei;
                if (!soft) { const cf32 e = c[np.index(xr, xi)]; er = e.real(); ei = e.imag(); }
                else if (mod == kQpsk) { er = 0.70710678f * ftanhf(0.70710678f * invS * xr); ei = 0.70710678f * ftanhf(0.70710678f * invS * xi); }
                else if (P == 8) {
                    // 8PSK: the nearest point and its two neighbours on the circle carry the expectation (the fourth is more than a decision distance
                    // further out: below 1% of the weight at the noise level where this path is used)
                    const int i1 = np.index(xr, xi);
                    int i2, i3;
                    np.neighbours(i1, i2, i3);
                    const float h = 0.5f * invS;
                    const float d1 = (xr - c[i1].real()) * (xr - c[i1].real()) + (xi - c[i1].imag()) * (xi - c[i1].imag());
                    const float d2 = (xr - c[i2].real()) * (xr - c[i2].real()) + (xi - c[i2].imag()) * (xi - c[i2].imag());
                    const float d3 = (xr - c[i3].real()) * (xr - c[i3].real()) + (xi - c[i3].imag()) * (xi - c[i3].imag());
                    const float e2 = fexpNeg(-(d2 - d1) * h), e3 = fexpNeg(-(d3 - d1) * h), iw = 1.f / (1.f + e2 + e3);
                    er = (c[i1].real() + e2 * c[i2].real() + e3 * c[i3].real()) * iw;
                    ei = (c[i1].imag() + e2 * c[i2].imag() + e3 * c[i3].imag()) * iw;
                }
                else if (P > 32) {
                    // the dense S2X constellations: the two nearest points carry the expectation
                    int i1, i2;
                    np.index2(xr, xi, i1, i2);
                    const float d1 = (xr - c[i1].real()) * (xr - c[i1].real()) + (xi - c[i1].imag()) * (xi - c[i1].imag());
                    const float d2 = (xr - c[i2].real()) * (xr - c[i2].real()) + (xi - c[i2].imag()) * (xi - c[i2].imag());
                    const float e = fexpNeg(-(d2 - d1) * 0.5f * invS), iw = 1.f / (1.f + e);
                    er = (c[i1].real() + e * c[i2].real()) * iw; ei = (c[i1].imag() + e * c[i2].imag()) * iw;
                } else {
                    float w[32], mx = -1e30f;
                    const float h = 0.5f * invS;
                    for (int i = 0; i < P; i++) { const float a = xr - c[i].real(), bb = xi - c[i].imag(); w[i] = -(a * a + bb * bb) * h; mx = std::max(mx, w[i]); }
                    float sw = 0, sr = 0, si = 0;
                    for (int i = 0; i < P; i++) { const float e = fexpNeg(w[i] - mx); sw += e; sr += e * c[i].real(); si += e * c[i].imag(); }
                    er = sr / sw; ei = si / sw;
                }
                ar += xr * er + xi * ei;           // x conj(E)
                ai += xi * er - xr * ei;
            }
            acc[(size_t)b] = cf32(ar, ai);
        }
        // zero lag smoothing across the blocks: a 1 2 1 weighted vector sum (the weights are the block magnitudes, so unreliable blocks count little)
        for (int b = 0; b < nb; b++) {
            cf32 s = 2.f * acc[(size_t)b];
            if (b > 0) s += acc[(size_t)b - 1];
            if (b + 1 < nb) s += acc[(size_t)b + 1];
            sm[(size_t)b] = s;
        }
        // phase per block centre, linear interpolation in between (the phase is unwrapped along the way)
        std::vector<float> ph((size_t)nb);
        float prev = 0;
        for (int b = 0; b < nb; b++) {
            float a = std::abs(sm[(size_t)b]) > 1e-9f ? std::arg(sm[(size_t)b]) : prev;
            while (a - prev > 3.14159265f) a -= 6.2831853f;
            while (a - prev < -3.14159265f) a += 6.2831853f;
            ph[(size_t)b] = prev = a;
        }
        // the correction is linear between block centres: a rotor with a constant step inside each stretch, renewed at the stretch start
        {
            int k = 0;
            while (k < n) {
                const float t = ((float)k - 0.5f * (float)W) / (float)W;
                int b0 = (int)std::floor(t);
                int b1 = b0 + 1;
                int kEnd;
                float e0, slope;
                if (b0 < 0) { e0 = ph[0]; slope = 0.f; kEnd = std::min(n, W / 2); }
                else if (b1 >= nb) { e0 = ph[(size_t)nb - 1]; slope = 0.f; kEnd = n; }
                else {
                    const float f = t - (float)b0;
                    e0 = ph[(size_t)b0] * (1.f - f) + ph[(size_t)b1] * f;
                    slope = (ph[(size_t)b1] - ph[(size_t)b0]) / (float)W;
                    kEnd = std::min(n, b1 * W + W / 2);
                }
                if (kEnd <= k) kEnd = k + 1;
                if (it == iterations - 1) {   // the rms of what is applied in the last pass: sum over the stretch of (e0 + slope m)^2 in closed form
                    const double L = kEnd - k, e = e0, sl = slope;
                    sq += L * e * e + e * sl * L * (L - 1) + sl * sl * (L - 1) * L * (2 * L - 1) / 6.0;
                }
#ifdef S2REFINE_NEON
                rotateRamp(x, k, kEnd, e0, slope);
#else
                cf32 rot(std::cos(e0), -std::sin(e0));
                const cf32 stp(std::cos(slope), -std::sin(slope));
                for (int j = k; j < kEnd; j++) { x[j] *= rot; rot *= stp; }
#endif
                k = kEnd;
            }
        }
    }
    return (float)std::sqrt(sq / n);
}

} // namespace dvbs
} // namespace dect2
