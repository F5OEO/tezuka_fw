#include "dect2/ldpc.h"
#include "dect2/simd.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#include <cstdint>
#include <vector>
#endif

namespace dect2 {

#if !(defined(__ARM_NEON) && !defined(DECT2_NO_SIMD))
namespace {
// Branch-free min-sum kernels over one connection (Z checks at a time); the compiler vectorises them (AVX2 clone on x86-64).
DECT2_SIMD_BODY void minSumAccumulateBody(const float* __restrict Lj, float* __restrict min1, float* __restrict min2,
                                         int* __restrict idx1, uint32_t* __restrict sg, int j, int Z) {
    for (int m = 0; m < Z; m++) {
        const float x = Lj[m], a = std::fabs(x);
        uint32_t sb; std::memcpy(&sb, &x, 4);
        sg[m] ^= sb & 0x80000000u;
        const float m1 = min1[m], m2 = min2[m];
        const bool lt = a < m1;
        min2[m] = lt ? m1 : (a < m2 ? a : m2);
        idx1[m] = lt ? j : idx1[m];
        min1[m] = lt ? a : m1;
    }
}
DECT2_SIMD_BODY void minSumApplyBody(float* __restrict Lj, float* __restrict mj, const float* __restrict min1, const float* __restrict min2,
                                    const int* __restrict idx1, const uint32_t* __restrict sg, int j, int Z, float alpha) {
    for (int m = 0; m < Z; m++) {
        const float mag = (idx1[m] == j ? min2[m] : min1[m]) * alpha;
        uint32_t sb; std::memcpy(&sb, &Lj[m], 4);
        uint32_t mb; std::memcpy(&mb, &mag, 4);
        mb |= (sg[m] ^ (sb & 0x80000000u));
        float nm; std::memcpy(&nm, &mb, 4);
        const float old = mj[m];
        mj[m] = nm;
        Lj[m] = nm - old;
    }
}
} // namespace
#endif

LdpcCode::LdpcCode(int k, int n, const std::vector<std::vector<int>>& rows) : k_(k), n_(n), m_(n - k) {
    const int q = m_ / 360;
    q_ = q;
    groups_ = (int)rows.size();
    layers_.assign(q, {});
    for (int g = 0; g < groups_; g++) for (int a : rows[g]) layers_[a % q].push_back({g, a / q});
    std::vector<std::vector<int>> chk(m_);
    int im = 0;
    for (auto& row : rows) {
        for (int c = 0; c < 360; c++, im++) {
            for (int a : row) {
                int p = (a + c * q) % m_;
                encP_.push_back(p);
                encD_.push_back(im);
                chk[p].push_back(im);
            }
        }
    }
    // zig-zag parity structure: check j involves parity bits j and j-1
    for (int j = 0; j < m_; j++) {
        chk[j].push_back(k_ + j);
        if (j > 0) chk[j].push_back(k_ + j - 1);
    }
    chkStart_.resize(m_ + 1);
    chkStart_[0] = 0;
    for (int j = 0; j < m_; j++) chkStart_[j + 1] = chkStart_[j] + (int)chk[j].size();
    chkVar_.reserve(chkStart_[m_]);
    for (int j = 0; j < m_; j++) chkVar_.insert(chkVar_.end(), chk[j].begin(), chk[j].end());
}

void LdpcCode::encode(std::vector<uint8_t>& bits) const {
    bits.resize(n_);
    std::fill(bits.begin() + k_, bits.end(), 0);
    for (size_t e = 0; e < encP_.size(); e++) bits[k_ + encP_[e]] ^= bits[encD_[e]];
    for (int j = 1; j < m_; j++) bits[k_ + j] ^= bits[k_ + j - 1];
}

bool LdpcCode::decode(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters) const {
    const int E = chkStart_[m_];
    std::vector<float> c2v(E, 0.f), v2c(E, 0.f), total(n_);
    hard.assign(n_, 0);
    const float alpha = 0.8f; // normalisation factor
    int it = 0;
    bool ok = false;
    for (; it < maxIter; it++) {
        // variable -> check: total LLR minus the check's own contribution
        std::copy(llr.begin(), llr.end(), total.begin());
        for (int e = 0; e < E; e++) total[chkVar_[e]] += c2v[e];
        for (int e = 0; e < E; e++) v2c[e] = total[chkVar_[e]] - c2v[e];
        // check update (min-sum)
        for (int j = 0; j < m_; j++) {
            int s = chkStart_[j], t = chkStart_[j + 1];
            float min1 = 1e30f, min2 = 1e30f;
            int idx1 = -1;
            int sign = 0;
            for (int e = s; e < t; e++) {
                float a = std::fabs(v2c[e]);
                if (v2c[e] < 0) sign ^= 1;
                if (a < min1) { min2 = min1; min1 = a; idx1 = e; }
                else if (a < min2) min2 = a;
            }
            for (int e = s; e < t; e++) {
                float mag = (e == idx1 ? min2 : min1) * alpha;
                int sg = sign ^ (v2c[e] < 0 ? 1 : 0);
                c2v[e] = sg ? -mag : mag;
            }
        }
        // decisions and syndrome
        std::copy(llr.begin(), llr.end(), total.begin());
        for (int e = 0; e < E; e++) total[chkVar_[e]] += c2v[e];
        for (int i = 0; i < n_; i++) hard[i] = total[i] < 0;
        ok = true;
        for (int j = 0; j < m_ && ok; j++) {
            int p = 0;
            for (int e = chkStart_[j]; e < chkStart_[j + 1]; e++) p ^= hard[chkVar_[e]];
            if (p) ok = false;
        }
        if (ok) { it++; break; }
    }
    if (iters) *iters = it;
    return ok;
}

DECT2_SIMD_MEMBER_BODY bool LdpcCode::decodeFastBody(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha) const {
    constexpr int Z = 360;
    constexpr float kBig = 1e4f;
    const int q = q_, G = groups_;
    // posterior LLRs: information groups [G][Z]; parity groups [q][Z+1] with a leading pad slot (index -1)
    std::vector<float> inf((size_t)G * Z), par((size_t)q * (Z + 1));
    auto P = [&](int r) { return par.data() + (size_t)r * (Z + 1) + 1; };
    for (int i = 0; i < G * Z; i++) inf[i] = llr[i];
    for (int r = 0; r < q; r++) {
        float* p = P(r);
        p[-1] = kBig;
        for (int m = 0; m < Z; m++) p[m] = llr[k_ + r + q * m];
    }
    // check-to-variable messages per layer and connection (+2 parity connections)
    std::vector<size_t> base(q + 1, 0);
    for (int r = 0; r < q; r++) base[r + 1] = base[r] + (layers_[r].size() + 2) * Z;
    std::vector<float> msg(base[q], 0.f);
    std::vector<float> L, min1(Z), min2(Z);
    std::vector<int> idx1(Z);
    std::vector<uint32_t> sg(Z);
    int it = 0;
    bool ok = false;
    for (; it < maxIter; it++) {
        for (int r = 0; r < q; r++) {
            const auto& cn = layers_[r];
            const int nc = (int)cn.size() + 2;
            L.assign((size_t)nc * Z, 0.f);
            float* mr = msg.data() + base[r];
            // gather variable-to-check messages
            for (int j = 0; j < nc; j++) {
                float* Lj = L.data() + (size_t)j * Z;
                const float* mj = mr + (size_t)j * Z;
                if (j < (int)cn.size()) {
                    const float* src = inf.data() + (size_t)cn[j].group * Z;
                    const int s = cn[j].shift;
                    // check m reads variable n = (m - s) mod Z
                    for (int m = s; m < Z; m++) Lj[m] = src[m - s] - mj[m];
                    for (int m = 0; m < s; m++) Lj[m] = src[m - s + Z] - mj[m];
                } else {
                    const float* src = (j == (int)cn.size()) ? P(r) : (r > 0 ? P(r - 1) : P(q - 1) - 1);
                    for (int m = 0; m < Z; m++) Lj[m] = src[m] - mj[m];
                }
            }
            // min-sum over the nc connections, for all 360 checks of the layer at once
            for (int m = 0; m < Z; m++) { min1[m] = 1e30f; min2[m] = 1e30f; idx1[m] = -1; sg[m] = 0; }
            for (int j = 0; j < nc; j++) {
                const float* Lj = L.data() + (size_t)j * Z;
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
                const int32x4_t jv = vdupq_n_s32(j);
                for (int m = 0; m < Z; m += 4) {
                    float32x4_t x = vld1q_f32(Lj + m);
                    float32x4_t a = vabsq_f32(x);
                    float32x4_t m1 = vld1q_f32(&min1[m]), m2 = vld1q_f32(&min2[m]);
                    uint32x4_t lt = vcltq_f32(a, m1);
                    vst1q_f32(&min2[m], vbslq_f32(lt, m1, vminq_f32(m2, a)));
                    vst1q_s32(&idx1[m], vbslq_s32(lt, jv, vld1q_s32(&idx1[m])));
                    vst1q_f32(&min1[m], vminq_f32(m1, a));
                    vst1q_u32(&sg[m], veorq_u32(vld1q_u32(&sg[m]), vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u))));
                }
#else
                minSumAccumulateBody(Lj, min1.data(), min2.data(), idx1.data(), sg.data(), j, Z);
#endif
            }
            for (int j = 0; j < nc; j++) {
                float* Lj = L.data() + (size_t)j * Z;
                float* mj = mr + (size_t)j * Z;
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
                const int32x4_t jv = vdupq_n_s32(j);
                const float32x4_t al = vdupq_n_f32(alpha);
                for (int m = 0; m < Z; m += 4) {
                    float32x4_t x = vld1q_f32(Lj + m);
                    uint32x4_t isMin = vceqq_s32(vld1q_s32(&idx1[m]), jv);
                    float32x4_t mag = vmulq_f32(vbslq_f32(isMin, vld1q_f32(&min2[m]), vld1q_f32(&min1[m])), al);
                    uint32x4_t sbit = veorq_u32(vld1q_u32(&sg[m]), vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u)));
                    float32x4_t nm = vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(mag), sbit));
                    float32x4_t old = vld1q_f32(mj + m);
                    vst1q_f32(mj + m, nm);
                    vst1q_f32(Lj + m, vsubq_f32(nm, old)); // posterior increment (a variable can appear twice in a layer)
                }
#else
                minSumApplyBody(Lj, mj, min1.data(), min2.data(), idx1.data(), sg.data(), j, Z, alpha);
#endif
                if (j < (int)cn.size()) {
                    float* dst = inf.data() + (size_t)cn[j].group * Z;
                    const int s = cn[j].shift;
                    for (int m = s; m < Z; m++) dst[m - s] += Lj[m];
                    for (int m = 0; m < s; m++) dst[m - s + Z] += Lj[m];
                } else {
                    float* dst = (j == (int)cn.size()) ? P(r) : (r > 0 ? P(r - 1) : P(q - 1) - 1);
                    for (int m = 0; m < Z; m++) dst[m] += Lj[m];
                }
            }
            P(q - 1)[-1] = kBig; // the non-existent parity bit before the first one stays known
        }
        // syndrome check on hard decisions
        ok = true;
        for (int r = 0; r < q && ok; r++) {
            const auto& cn = layers_[r];
            for (int m = 0; m < Z; m++) {
                int p = (P(r)[m] < 0) ^ (r > 0 ? (P(r - 1)[m] < 0) : (m > 0 ? (P(q - 1)[m - 1] < 0) : 0));
                for (const auto& c : cn) {
                    int n = m - c.shift; if (n < 0) n += Z;
                    p ^= inf[(size_t)c.group * Z + n] < 0;
                }
                if (p) { ok = false; break; }
            }
        }
        if (ok) { it++; break; }
    }
    hard.assign(n_, 0);
    for (int i = 0; i < G * Z; i++) hard[i] = inf[i] < 0;
    for (int r = 0; r < q; r++) for (int m = 0; m < Z; m++) hard[k_ + r + q * m] = P(r)[m] < 0;
    if (iters) *iters = it;
    return ok;
}

#if defined(DECT2_X86_DISPATCH)
DECT2_AVX2_TARGET bool LdpcCode::decodeFastAvx2(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha) const {
    return decodeFastBody(llr, maxIter, hard, iters, alpha);
}
#endif

bool LdpcCode::decodeFast(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha) const {
#if defined(DECT2_X86_DISPATCH)
    if (cpuHasAvx2()) return decodeFastAvx2(llr, maxIter, hard, iters, alpha);
#endif
    return decodeFastBody(llr, maxIter, hard, iters, alpha);
}

// ---- the same layered decoder on 8-bit values
// Ranges keep every stored value inside int8: posteriors are clamped to +-96, messages are +-31, so "posterior - old message" is at most 127; the
// sum "posterior + (new - old message)" is made in int and clamped.

namespace {
constexpr int kQMax = 96, kQMsg = 31;

DECT2_SIMD_BODY void q8Accumulate(const int8_t* __restrict Lj, int8_t* __restrict min1, int8_t* __restrict min2, int8_t* __restrict idx1,
                                  int8_t* __restrict sg, int8_t j, int Z) {
    for (int m = 0; m < Z; m++) {
        const int8_t x = Lj[m], a = x < 0 ? (int8_t)-x : x;
        const int8_t m1 = min1[m], m2 = min2[m];
        const bool lt = a < m1;
        min2[m] = lt ? m1 : (a < m2 ? a : m2);
        idx1[m] = lt ? j : idx1[m];
        min1[m] = lt ? a : m1;
        sg[m] ^= x;                       // only the sign bit is used
    }
}
DECT2_SIMD_BODY void q8Apply(const int8_t* __restrict Lj, int8_t* __restrict mj, int8_t* __restrict inc, const int8_t* __restrict min1,
                             const int8_t* __restrict min2, const int8_t* __restrict idx1, const int8_t* __restrict sg, int j, int Z) {
    for (int m = 0; m < Z; m++) {
        const int x = Lj[m];
        const int a1 = min1[m], a2 = min2[m];
        int mag = idx1[m] == j ? a2 : a1;
        mag = (mag * 13 + 8) >> 4;                         // normalised min-sum, 13/16
        mag = mag > kQMsg ? kQMsg : mag;
        const int neg = (sg[m] ^ x) >> 7;                  // -1 where the message is negative
        const int8_t nm = (int8_t)((mag ^ neg) - neg);
        inc[m] = (int8_t)(nm - mj[m]);
        mj[m] = nm;
    }
}
DECT2_SIMD_BODY void q8Xor(int8_t* __restrict dst, const int8_t* __restrict src, int n) {
    for (int m = 0; m < n; m++) dst[m] ^= src[m];
}
DECT2_SIMD_BODY void q8Add(int8_t* __restrict dst, const int8_t* __restrict inc, int n) {
    for (int m = 0; m < n; m++) {
        const int v = dst[m] + inc[m];
        dst[m] = (int8_t)(v > kQMax ? kQMax : (v < -kQMax ? -kQMax : v));
    }
}
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
// One layer of the 8-bit decoder for all lanes: the minimum, second minimum, index of the minimum and sign product of 16 checks stay in registers
// over the connections (the generic path writes and reads them in memory for every connection). Rows are kQStride bytes (the lanes after Z are
// padding and not used). Same arithmetic as q8Accumulate / q8Apply: results are identical.
constexpr int kQStride = 368;
inline void q8LayerNeon(const int8_t* L, int8_t* mr, int8_t* inc, int nc) {
    for (int o = 0; o < kQStride; o += 16) {
        int8x16_t m1 = vdupq_n_s8(127), m2 = m1, idx = vdupq_n_s8(-1), sgn = vdupq_n_s8(0);
        for (int j = 0; j < nc; j++) {
            const int8x16_t x = vld1q_s8(L + (size_t)j * kQStride + o);
            const int8x16_t a = vabsq_s8(x);
            const uint8x16_t lt = vcltq_s8(a, m1);
            m2 = vbslq_s8(lt, m1, vminq_s8(m2, a));
            idx = vbslq_s8(lt, vdupq_n_s8((int8_t)j), idx);
            m1 = vminq_s8(m1, a);
            sgn = veorq_s8(sgn, x);
        }
        for (int j = 0; j < nc; j++) {
            const size_t at = (size_t)j * kQStride + (size_t)o;
            const int8x16_t x = vld1q_s8(L + at), mj = vld1q_s8(mr + at);
            const uint8x16_t isMin = vceqq_s8(idx, vdupq_n_s8((int8_t)j));
            // normalised min-sum 13/16 with the clamp to kQMsg: min(mag, 38) first gives the same result as clamping afterwards
            const uint8x16_t mag = vminq_u8(vreinterpretq_u8_s8(vbslq_s8(isMin, m2, m1)), vdupq_n_u8(38));
            const uint8x8_t lo = vrshrn_n_u16(vmull_u8(vget_low_u8(mag), vdup_n_u8(13)), 4);
            const uint8x8_t hi = vrshrn_n_u16(vmull_u8(vget_high_u8(mag), vdup_n_u8(13)), 4);
            const int8x16_t mg = vreinterpretq_s8_u8(vcombine_u8(lo, hi));
            const int8x16_t neg = vshrq_n_s8(veorq_s8(sgn, x), 7);        // -1 where the message is negative
            const int8x16_t nm = vsubq_s8(veorq_s8(mg, neg), neg);
            vst1q_s8(inc + at, vsubq_s8(nm, mj));
            vst1q_s8(mr + at, nm);
        }
    }
}
// dst += inc, saturated to +-kQMax
inline void q8AddNeon(int8_t* dst, const int8_t* inc, int n) {
    int m = 0;
    const int8x16_t hi = vdupq_n_s8(kQMax), lo = vdupq_n_s8(-kQMax);
    for (; m + 16 <= n; m += 16) vst1q_s8(dst + m, vmaxq_s8(vminq_s8(vqaddq_s8(vld1q_s8(dst + m), vld1q_s8(inc + m)), hi), lo));
    for (; m < n; m++) { const int v = dst[m] + inc[m]; dst[m] = (int8_t)(v > kQMax ? kQMax : (v < -kQMax ? -kQMax : v)); }
}
#endif
} // namespace

DECT2_SIMD_MEMBER_BODY bool LdpcCode::decodeQ8Body(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float, int* unsat) const {
    constexpr int Z = 360;
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
    constexpr int ZS = kQStride;     // row stride of the messages: whole 16-lane blocks
#else
    constexpr int ZS = Z;
#endif
    const int q = q_, G = groups_;
    // the scale: the mean magnitude of the input becomes about 8 (the min-sum decoder does not care about the scale, only about the resolution)
    float sum = 0;
    for (int i = 0; i < n_; i++) sum += std::fabs(llr[(size_t)i]);
    const float scale = sum > 0 ? 16.f * (float)n_ / sum : 1.f;
    // round to nearest without the library call (nearbyint is a libm function on a Cortex-A9, once per input value): half away from zero
    auto quant = [&](float v) { float t = v * scale; t += t < 0 ? -0.5f : 0.5f; return (int8_t)(t > kQMsg ? kQMsg : (t < -kQMsg ? -kQMsg : (int)t)); };
    std::vector<int8_t> inf((size_t)G * Z), par((size_t)q * (Z + 1));
    auto P = [&](int r) { return par.data() + (size_t)r * (Z + 1) + 1; };
    for (int i = 0; i < G * Z; i++) inf[(size_t)i] = quant(llr[(size_t)i]);
    for (int r = 0; r < q; r++) {
        int8_t* p = P(r);
        p[-1] = kQMax;
        for (int m = 0; m < Z; m++) p[m] = quant(llr[(size_t)(k_ + r + q * m)]);
    }
    std::vector<size_t> base((size_t)q + 1, 0);
    for (int r = 0; r < q; r++) base[(size_t)r + 1] = base[(size_t)r] + (layers_[(size_t)r].size() + 2) * ZS;
    std::vector<int8_t> msg(base[(size_t)q], 0);
    std::vector<int8_t> L, inc, min1(Z), min2(Z), idx1(Z), sg(Z);
    int it = 0, bad = 0;
    bool ok = false;
    // checks that fail, layer by layer (the 360 checks of a layer together: the sign bits of its variables, rotated, xor-ed); `all` false stops at the first
    std::vector<int8_t> acc(Z);
    auto syndrome = [&](bool all) {
        int n = 0;
        for (int r = 0; r < q; r++) {
            const auto& cn = layers_[(size_t)r];
            std::copy(P(r), P(r) + Z, acc.begin());
            if (r > 0) q8Xor(acc.data(), P(r - 1), Z);
            else { q8Xor(acc.data() + 1, P(q - 1), Z - 1); }
            for (const auto& c : cn) {
                const int8_t* src = inf.data() + (size_t)c.group * Z;
                q8Xor(acc.data() + c.shift, src, Z - c.shift);
                q8Xor(acc.data(), src + (Z - c.shift), c.shift);
            }
            for (int m = 0; m < Z; m++) n += acc[(size_t)m] < 0;
            if (n && !all) return n;
        }
        return n;
    };
    // a frame whose hard decisions already satisfy every check (a clean signal) is done before the first iteration
    if (syndrome(false) == 0) ok = true;
    else
    for (; it < maxIter; it++) {
        for (int r = 0; r < q; r++) {
            const auto& cn = layers_[(size_t)r];
            const int nc = (int)cn.size() + 2;
            L.assign((size_t)nc * ZS, 0);
            inc.assign((size_t)nc * ZS, 0);
            int8_t* mr = msg.data() + base[(size_t)r];
            for (int j = 0; j < nc; j++) {
                int8_t* Lj = L.data() + (size_t)j * ZS;
                const int8_t* mj = mr + (size_t)j * ZS;
                if (j < (int)cn.size()) {
                    const int8_t* src = inf.data() + (size_t)cn[(size_t)j].group * Z;
                    const int s = cn[(size_t)j].shift;
                    for (int m = s; m < Z; m++) Lj[m] = (int8_t)(src[m - s] - mj[m]);
                    for (int m = 0; m < s; m++) Lj[m] = (int8_t)(src[m - s + Z] - mj[m]);
                } else {
                    const int8_t* src = (j == (int)cn.size()) ? P(r) : (r > 0 ? P(r - 1) : P(q - 1) - 1);
                    for (int m = 0; m < Z; m++) Lj[m] = (int8_t)(src[m] - mj[m]);
                }
            }
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
            q8LayerNeon(L.data(), mr, inc.data(), nc);
            for (int j = 0; j < nc; j++) {
                const int8_t* incj = inc.data() + (size_t)j * ZS;
                if (j < (int)cn.size()) {
                    int8_t* dst = inf.data() + (size_t)cn[(size_t)j].group * Z;
                    const int s = cn[(size_t)j].shift;
                    q8AddNeon(dst, incj + s, Z - s);
                    q8AddNeon(dst + (Z - s), incj, s);
                } else {
                    int8_t* dst = (j == (int)cn.size()) ? P(r) : (r > 0 ? P(r - 1) : P(q - 1) - 1);
                    q8AddNeon(dst, incj, Z);
                }
            }
#else
            for (int m = 0; m < Z; m++) { min1[(size_t)m] = 127; min2[(size_t)m] = 127; idx1[(size_t)m] = -1; sg[(size_t)m] = 0; }
            for (int j = 0; j < nc; j++) q8Accumulate(L.data() + (size_t)j * ZS, min1.data(), min2.data(), idx1.data(), sg.data(), (int8_t)j, Z);
            for (int j = 0; j < nc; j++) {
                int8_t* incj = inc.data() + (size_t)j * ZS;
                q8Apply(L.data() + (size_t)j * ZS, mr + (size_t)j * ZS, incj, min1.data(), min2.data(), idx1.data(), sg.data(), (int8_t)j, Z);
                if (j < (int)cn.size()) {
                    int8_t* dst = inf.data() + (size_t)cn[(size_t)j].group * Z;
                    const int s = cn[(size_t)j].shift;
                    q8Add(dst, incj + s, Z - s);
                    q8Add(dst + (Z - s), incj, s);
                } else {
                    int8_t* dst = (j == (int)cn.size()) ? P(r) : (r > 0 ? P(r - 1) : P(q - 1) - 1);
                    q8Add(dst, incj, Z);
                }
            }
#endif
            P(q - 1)[-1] = kQMax;
        }
        bad = syndrome(false);
        ok = bad == 0;
        if (ok) { it++; break; }
    }
    hard.assign((size_t)n_, 0);
    for (int i = 0; i < G * Z; i++) hard[(size_t)i] = inf[(size_t)i] < 0;
    for (int r = 0; r < q; r++) for (int m = 0; m < Z; m++) hard[(size_t)(k_ + r + q * m)] = P(r)[m] < 0;
    if (iters) *iters = it;
    if (!ok) bad = syndrome(true);
    if (unsat) *unsat = bad;
    return ok;
}

#if defined(DECT2_X86_DISPATCH)
DECT2_AVX2_TARGET bool LdpcCode::decodeQ8Avx2(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha, int* unsat) const {
    return decodeQ8Body(llr, maxIter, hard, iters, alpha, unsat);
}
#endif

bool LdpcCode::decodeFast8(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters, float alpha) const {
    int unsat = 0, it8 = 0;
#if defined(DECT2_X86_DISPATCH)
    bool ok = cpuHasAvx2() ? decodeQ8Avx2(llr, maxIter, hard, &it8, alpha, &unsat) : decodeQ8Body(llr, maxIter, hard, &it8, alpha, &unsat);
#else
    bool ok = decodeQ8Body(llr, maxIter, hard, &it8, alpha, &unsat);
#endif
    if (iters) *iters = it8;
    // the float decoder only for a frame that is close: a damaged one has a large part of its checks unsatisfied
    if (!ok && unsat * 8 <= m_) {
        int it = 0;
        ok = decodeFast(llr, maxIter, hard, &it, alpha);
        if (iters) *iters = it8 + it;
    }
    return ok;
}

} // namespace dect2
