// DVB-S/S2 receiver signal path, see dvbs_dsp.h.
#include "dvbs_dsp.h"
#include "dvbs_simd.h"
#include "dect2/dvbs_gen.h"     // rrcAt
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {
namespace dvbs {

namespace {
constexpr double kPi = 3.14159265358979323846;

double besselI0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 60; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-12 * s) break; }
    return s;
}
double kaiser(double x, double beta) {            // x in [-1, 1]
    if (std::fabs(x) >= 1) return 0;
    return besselI0(beta * std::sqrt(1 - x * x)) / besselI0(beta);
}
double sinc(double x) { return std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x); }
}

// ============================================================================ mixer and DC removal
void MixDc::makeTable() {
    for (int k = 0; k < kBlock; k++) { tc_[k] = (float)std::cos(k * step_); ts_[k] = -(float)std::sin(k * step_); }
}

void MixDc::configure(double fs, double freqHz) {
    fs_ = fs;
    step_ = 2 * kPi * freqHz / fs;
    makeTable();
    reset();
}

// Blocks of 64 samples: one phasor per block, the rest from a table, so that the loop has no chain of dependent operations; the DC estimate is the
// running mean of the blocks (a time constant of about 131 000 samples) and is applied to the next block
void MixDc::process(const cf32* x, size_t n, SplitBuf& out) {
    const size_t base = out.size();
    out.resize(base + n);
    float* re = out.re.data() + base;
    float* im = out.im.data() + base;
    if (!dcInit_ && n) { double a = 0, b = 0; for (size_t i = 0; i < n; i++) { a += x[i].real(); b += x[i].imag(); } dcRe_ = (float)(a / n); dcIm_ = (float)(b / n); dcInit_ = true; }
    const float mu = (float)kBlock / 131072.f;
    const bool rotate = std::fabs(step_) > 1e-12;
    size_t i = 0;
    while (i < n) {
        const size_t m = std::min<size_t>(kBlock, n - i);
        const float c0 = rotate ? (float)std::cos(-phase_) : 1.f, s0 = rotate ? (float)std::sin(-phase_) : 0.f;   // the phasor is only needed when the mixer turns
        const float dr = dcRe_, di = dcIm_;
        float sr = 0.f, si = 0.f;
        const float* xp = reinterpret_cast<const float*>(x + i);
        if (rotate) {
            for (size_t k = 0; k < m; k++) {
                const float xr = xp[2 * k] - dr, xi = xp[2 * k + 1] - di;
                sr += xp[2 * k]; si += xp[2 * k + 1];
                const float a = c0 * tc_[k] - s0 * ts_[k], b = c0 * ts_[k] + s0 * tc_[k];
                re[i + k] = xr * a - xi * b;
                im[i + k] = xr * b + xi * a;
            }
        } else {
            size_t k = 0;
#if defined(DVBS_NEON)
            // no mixing: de-interleave, take the DC estimate off and add up the block, four samples a step. The input comes from the L2 cache (another
            // thread wrote it): the prefetch ahead takes the loop from about 44 to 27 ns a sample on a Cortex-A9
            float32x4_t vsr = vdupq_n_f32(0.f), vsi = vdupq_n_f32(0.f);
            const float32x4_t vdr = vdupq_n_f32(dr), vdi = vdupq_n_f32(di);
            for (; k + 4 <= m; k += 4) {
                __builtin_prefetch(xp + 2 * k + 64);
                const float32x4x2_t v = vld2q_f32(xp + 2 * k);
                vsr = vaddq_f32(vsr, v.val[0]); vsi = vaddq_f32(vsi, v.val[1]);
                vst1q_f32(re + i + k, vsubq_f32(v.val[0], vdr));
                vst1q_f32(im + i + k, vsubq_f32(v.val[1], vdi));
            }
            {
                const float32x2_t a = vadd_f32(vget_low_f32(vsr), vget_high_f32(vsr)), b = vadd_f32(vget_low_f32(vsi), vget_high_f32(vsi));
                sr = vget_lane_f32(a, 0) + vget_lane_f32(a, 1); si = vget_lane_f32(b, 0) + vget_lane_f32(b, 1);
            }
#endif
            for (; k < m; k++) {
                sr += xp[2 * k]; si += xp[2 * k + 1];
                re[i + k] = xp[2 * k] - dr;
                im[i + k] = xp[2 * k + 1] - di;
            }
        }
        dcRe_ += (sr / (float)m - dcRe_) * mu;
        dcIm_ += (si / (float)m - dcIm_) * mu;
        if (rotate) phase_ = std::fmod(phase_ + (double)m * step_, 2 * kPi);
        i += m;
    }
}

// ============================================================================ half-band decimator
namespace {
constexpr int kHbJ = 8;     // odd taps at offsets 1, 3, ..., 2J-1 on each side
}

Halfband::Halfband() {
    h_.assign(kHbJ, 0.f);
    const double beta = 7.0, half = 2.0 * kHbJ;
    double sum = 0.5;
    for (int j = 0; j < kHbJ; j++) {
        const int off = 2 * j + 1;
        const double v = 0.5 * sinc(off / 2.0) * kaiser(off / (half + 1), beta);
        h_[j] = (float)v;
        sum += 2 * v;
    }
    const float g = (float)(1.0 / sum);          // unit gain at DC
    for (auto& v : h_) v *= g;
    centre_ = 0.5f * g;
    reset();
}

void Halfband::reset() {
    hist_.clear();
    hist_.re.assign(2 * kHbJ - 1, 0.f);
    hist_.im.assign(2 * kHbJ - 1, 0.f);
    phase_ = 2 * kHbJ - 1;
}

void Halfband::process(const SplitBuf& in, size_t n, SplitBuf& out) {
    hist_.re.insert(hist_.re.end(), in.re.begin(), in.re.begin() + (std::ptrdiff_t)n);
    hist_.im.insert(hist_.im.end(), in.im.begin(), in.im.begin() + (std::ptrdiff_t)n);
    const size_t reach = 2 * kHbJ - 1;
    const size_t sz = hist_.size();
    if (sz <= phase_ + reach) return;
    const size_t outs = (sz - 1 - reach - phase_) / 2 + 1;
    const size_t base = out.size();
    out.resize(base + outs);
    const float* xr = hist_.re.data();
    const float* xi = hist_.im.data();
    for (size_t m = 0; m < outs; m++) {
        const size_t c = phase_ + 2 * m;
        float sr = centre_ * xr[c], si = centre_ * xi[c];
        for (int j = 0; j < kHbJ; j++) {
            const size_t o = 2 * (size_t)j + 1;
            sr += h_[j] * (xr[c - o] + xr[c + o]);
            si += h_[j] * (xi[c - o] + xi[c + o]);
        }
        out.re[base + m] = sr; out.im[base + m] = si;
    }
    phase_ += 2 * outs;
    const size_t drop = phase_ - reach;
    hist_.eraseFront(drop);
    phase_ -= drop;
}

// ============================================================================ polyphase resampler
void PolyResampler::design(double r) {
    // lowpass at half the output rate (or half the input rate when interpolating)
    const double fc = 0.5 * std::min(1.0, 1.0 / r);
    int n = (int)std::ceil(8.0 * std::max(r, 1.5));
    n = std::max(16, std::min(64, (n + 7) / 8 * 8));
    taps_ = n;
    bank_.assign((size_t)phases_ * n, 0.f);
    const double beta = 7.0;
    for (int p = 0; p < phases_; p++) {
        double sum = 0;
        std::vector<double> t(n);
        for (int i = 0; i < n; i++) {
            const double tau = (double)p / phases_ + (n / 2 - 1 - i);
            t[i] = 2 * fc * sinc(2 * fc * tau) * kaiser(tau / (n / 2.0), beta);
            sum += t[i];
        }
        for (int i = 0; i < n; i++) bank_[(size_t)p * n + i] = (float)(t[i] / sum);
    }
}

void PolyResampler::configure(double ratio) {
    ratio_ = ratio;
    design(ratio);
    reset();
}

void PolyResampler::reset() {
    hist_.clear();
    hist_.re.assign(taps_ / 2, 0.f);
    hist_.im.assign(taps_ / 2, 0.f);
    pos_ = taps_ / 2 - 1;
}

void PolyResampler::process(const SplitBuf& in, size_t n, SplitBuf& out) {
    hist_.re.insert(hist_.re.end(), in.re.begin(), in.re.begin() + (std::ptrdiff_t)n);
    hist_.im.insert(hist_.im.end(), in.im.begin(), in.im.begin() + (std::ptrdiff_t)n);
    const int N = taps_;
    const double need = N / 2;
    const float* xr = hist_.re.data();
    const float* xi = hist_.im.data();
    // upper bound on the number of outputs
    const double avail = (double)hist_.size() - need - pos_;
    if (avail <= 0) return;
    const size_t cap = (size_t)(avail / ratio_) + 2;
    const size_t base = out.size();
    out.resize(base + cap);
    size_t produced = 0;
    while (pos_ + need < (double)hist_.size()) {
        const long i0 = (long)std::floor(pos_);
        const double fr = pos_ - (double)i0;
        int p = (int)(fr * phases_ + 0.5);
        long ii = i0;
        if (p >= phases_) { p = 0; ii++; }
        if (ii + N / 2 >= (long)hist_.size()) break;
        const float* h = &bank_[(size_t)p * N];
        const float* ar = xr + (ii - N / 2 + 1);
        const float* ai = xi + (ii - N / 2 + 1);
        float sv[2];
        dot2(h, ar, ai, N, sv);
        const float sr = sv[0], si = sv[1];
        if (produced >= cap) break;
        out.re[base + produced] = sr; out.im[base + produced] = si;
        produced++;
        pos_ += ratio_;
    }
    out.resize(base + produced);
    const long drop = (long)std::floor(pos_) - N / 2 + 1;
    if (drop > 0) { hist_.eraseFront((size_t)drop); pos_ -= (double)drop; }
}

// ============================================================================ matched filter and timing loop
void SymbolTimer::design(double alpha) {
    alpha_ = alpha;
    m_ = 2 * hs_;
    phases_ = 512;
    const int taps = 2 * m_;
    bank_.assign((size_t)phases_ * taps, 0.f);
    for (int p = 0; p < phases_; p++)
        for (int i = 0; i < taps; i++) {
            const double d = (double)p / phases_ + (m_ - 1 - i);   // distance in samples from the tap to the instant
            if (!prefilt_) { bank_[(size_t)p * taps + i] = (float)(0.5 * rrcAt(0.5 * d, alpha)); continue; }
            const double px = kPi * d;
            bank_[(size_t)p * taps + i] = (float)((std::fabs(d) < 1e-9 ? 1.0 : std::sin(px) / px) * 0.5 * (1.0 + std::cos(kPi * d / m_)));
        }
    kd_ = std::max(3.1 * alpha, 0.15);
    setLoop(bnt_, zeta_);
}

void SymbolTimer::configure(double rollOff, int halfSpanSymbols, bool prefiltered) {
    hs_ = halfSpanSymbols;
    prefilt_ = prefiltered;
    design(rollOff);
    reset();
}

void SymbolTimer::setLoop(double bnT, double zeta) {
    bnt_ = bnT; zeta_ = zeta;
    const double th = bnT / (zeta + 1.0 / (4.0 * zeta));
    const double d = 1.0 + 2.0 * zeta * th + th * th;
    kp_ = 4.0 * zeta * th / d / kd_;
    ki_ = 4.0 * th * th / d / kd_;
}

void SymbolTimer::reset() {
    buf_.clear();
    t_ = m_;
    integ_ = 0;
    havePrev_ = false;
    power_ = 0; corr_ = 0; invPower_ = 1; gain_ = 1;
    errRms_ = 0; errMs_ = 0;
    count_ = 0;
}

// Four consecutive symbols with one row h of the filter bank (taps entries, a multiple of 8): the instants are exactly two samples apart, so
//   y[k] = sum_j h[j] x[2k + j]   and   m[k] = sum_j h[j] x[2k + j - 1]   (m: the sample half a symbol earlier, for the timing error)
// x points at the first sample of the first window; x[-2] must exist. The lanes are the symbols: x[2k + j] for k < 4 is one strided load.
static inline void symbolBlock4(const float* h, const float* x, int taps, float* y, float* m) {
#if defined(DVBS_NEON)
    float32x4_t ya = vdupq_n_f32(0), yb = ya, ma = ya, mb = ya;
    {   // b = -2: (X_-2, X_-1); only X_-1 is used, for tap 0 of m
        const float32x4x2_t v = vld2q_f32(x - 2);
        ma = vmlaq_n_f32(ma, v.val[1], h[0]);
    }
    for (int b = 0; b < taps; b += 2) {
        const float32x4x2_t v = vld2q_f32(x + b);               // val[0] = X_b, val[1] = X_b+1 (X_j: x[j], x[j+2], x[j+4], x[j+6])
        ya = vmlaq_n_f32(ya, v.val[0], h[b]);
        yb = vmlaq_n_f32(yb, v.val[1], h[b + 1]);
        ma = vmlaq_n_f32(ma, v.val[0], h[b + 1]);
        if (b + 2 < taps) mb = vmlaq_n_f32(mb, v.val[1], h[b + 2]);
    }
    vst1q_f32(y, vaddq_f32(ya, yb));
    vst1q_f32(m, vaddq_f32(ma, mb));
#else
    for (int k = 0; k < 4; k++) {
        float sy = 0, sm = 0;
        for (int j = 0; j < taps; j++) { sy += h[j] * x[2 * k + j]; sm += h[j] * x[2 * k + j - 1]; }
        y[k] = sy; m[k] = sm;
    }
#endif
}

// y only (no half-symbol outputs): four consecutive symbols with one row of the filter bank, as symbolBlock4
static inline void symbolBlock4Y(const float* h, const float* x, int taps, float* y) {
#if defined(DVBS_NEON)
    float32x4_t ya = vdupq_n_f32(0), yb = ya;
    for (int b = 0; b < taps; b += 2) {
        const float32x4x2_t v = vld2q_f32(x + b);
        ya = vmlaq_n_f32(ya, v.val[0], h[b]);
        yb = vmlaq_n_f32(yb, v.val[1], h[b + 1]);
    }
    vst1q_f32(y, vaddq_f32(ya, yb));
#else
    for (int k = 0; k < 4; k++) { float sy = 0; for (int j = 0; j < taps; j++) sy += h[j] * x[2 * k + j]; y[k] = sy; }
#endif
}

void SymbolTimer::process(SplitBuf& in, std::vector<cf32>& out) {
    buf_.re.insert(buf_.re.end(), in.re.begin(), in.re.end());
    buf_.im.insert(buf_.im.end(), in.im.begin(), in.im.end());
    in.clear();
    run(out);
}

void SymbolTimer::run(std::vector<cf32>& out) {
    const int taps = 2 * m_;
    const float* xr = buf_.re.data();
    const float* xi = buf_.im.data();
    const size_t outBase = out.size();
    // How many symbols fit in what we have (a symbol is about two samples; the loop corrections are tiny)
    const double lim = (double)buf_.size() - m_ - 1;
    out.resize(outBase + (size_t)std::max(0.0, (lim - t_) / 1.5) + 2);
    cf32* o = out.data() + outBase;
    size_t made = 0;
    double corrPrev = corr_;                 // the correction from the previous symbol's error is applied with a delay of one symbol, which
    float invPow = invPower_, gain = gain_;  // lets the filter outputs of consecutive symbols be computed without waiting for each other
    // With the loop settled (narrow) four symbols are made at once: one phase of the filter bank, windows exactly two samples apart.
    const bool blockOk = bnt_ <= 0.0045 && taps % 8 == 0;
    while (t_ < lim) {
        // setFast(): eight symbols, one row of the filter bank, the timing detector on the last four with twice the gains (see dvbs_dsp.h)
        if (fast_ && blockOk && havePrev_ && count_ >= 64 && std::fabs(corrPrev) < 2.0e-3 && t_ + 24.0 < lim) {
            constexpr int L = 8;
            const double tb = t_ - (double)(L - 1) * corrPrev;       // the row of the middle of the eight instants
            const long i0 = (long)tb;
            int p = (int)((tb - (double)i0) * phases_ + 0.5);
            long ii = i0;
            if (p >= phases_) { p = 0; ii++; }
            const long s0 = ii - m_ + 1;
            if (s0 >= 2) {
                float yr[L], yi[L], mr4[4], mi4[4];
                const float* h = &bank_[(size_t)p * taps];
                symbolBlock4Y(h, xr + s0, taps, yr);
                symbolBlock4Y(h, xi + s0, taps, yi);
                symbolBlock4(h, xr + s0 + 8, taps, yr + 4, mr4);
                symbolBlock4(h, xi + s0 + 8, taps, yi + 4, mi4);
                float pwSum = 0;
                for (int k = 0; k < L; k++) pwSum += yr[k] * yr[k] + yi[k] * yi[k];
                float pr = yr[3], pi = yi[3], integ = (float)integ_, corr = (float)corrPrev, errMs = errMs_;
                const float kp = (float)(kp_ * (L / 4)), ki = (float)(ki_ * (L / 4));
                for (int k = 0; k < 4; k++) {
                    float e = (mr4[k] * (yr[4 + k] - pr) + mi4[k] * (yi[4 + k] - pi)) * invPow;    // Gardner
                    e = std::max(-2.f, std::min(2.f, e));
                    integ += ki * e;
                    corr = kp * e + integ;
                    errMs += (e * e - errMs) * (0.002f * (float)(L / 4));
                    pr = yr[4 + k]; pi = yi[4 + k];
                }
                t_ += 2.0 * L * (1.0 - corrPrev);
                integ_ = integ; corrPrev = corr; errMs_ = errMs;
                power_ += (pwSum * (1.f / (float)L) - power_) * ((float)L / 4096.f);
                invPow = 1.f / power_; gain = std::sqrt(invPow);
                prevRe_ = pr; prevIm_ = pi;
                for (int k = 0; k < L; k++) o[made++] = cf32(yr[k] * gain, yi[k] * gain);
                count_ += L;
                continue;
            }
        }
        if (blockOk && havePrev_ && count_ >= 64 && std::fabs(corrPrev) < 1.0e-2 && t_ + 8.0 < lim) {
            // the shared phase is that of the middle of the four instants (they are 2 (1 - correction) apart, the grid of the block 2): the drift of
            // the real instants from the grid is then at most half of 6 * correction, and only the part of the correction that changes within the block
            const double tb = t_ - 3.0 * corrPrev;
            const long i0 = (long)tb;
            int p = (int)((tb - (double)i0) * phases_ + 0.5);
            long ii = i0;
            if (p >= phases_) { p = 0; ii++; }
            const long s0 = ii - m_ + 1;
            if (s0 >= 2) {
                float yr[4], yi[4], mr[4], mi[4];
                const float* h = &bank_[(size_t)p * taps];
                symbolBlock4(h, xr + s0, taps, yr, mr);
                symbolBlock4(h, xi + s0, taps, yi, mi);
                // the loop runs symbol by symbol as in the general path (averaging its error over the four symbols costs 0.4 dB of MER at 25 dB), but in
                // single precision: the double arithmetic of the timing instants is what made this function slow
                float pwSum = 0, pr = prevRe_, pi = prevIm_, integ = (float)integ_, corr = (float)corrPrev, errMs = errMs_, tAdv = 0;
                const float kp = (float)kp_, ki = (float)ki_;
                for (int k = 0; k < 4; k++) {
                    pwSum += yr[k] * yr[k] + yi[k] * yi[k];
                    float e = (mr[k] * (yr[k] - pr) + mi[k] * (yi[k] - pi)) * invPow;    // Gardner: positive when the sampling instant is late
                    e = std::max(-2.f, std::min(2.f, e));
                    tAdv += 2.f * (1.f - corr);                   // the correction from the previous symbol's error, as in the general path
                    integ += ki * e;
                    corr = kp * e + integ;
                    errMs += (e * e - errMs) * 0.002f;
                    pr = yr[k]; pi = yi[k];
                }
                t_ += (double)tAdv;
                integ_ = integ; corrPrev = corr; errMs_ = errMs;
                power_ += (pwSum * 0.25f - power_) * (4.f / 4096.f);
                invPow = 1.f / power_; gain = std::sqrt(invPow);
                prevRe_ = pr; prevIm_ = pi;
                for (int k = 0; k < 4; k++) o[made++] = cf32(yr[k] * gain, yi[k] * gain);
                count_ += 4;
                continue;
            }
        }
        const long i0 = (long)t_;
        const double fr = t_ - (double)i0;
        int p = (int)(fr * phases_ + 0.5);
        long ii = i0;
        if (p >= phases_) { p = 0; ii++; }
        const float* h = &bank_[(size_t)p * taps];
        const long s0 = ii - m_ + 1;
        float dv[4];
        dot4(h, xr + s0, xi + s0, xr + s0 - 1, xi + s0 - 1, taps, dv);
        const float yr = dv[0], yi = dv[1], mr = dv[2], mi = dv[3];
        // the next timing instant, from the correction known so far
        t_ += 2.0 * (1.0 - corrPrev);
        const float pw = yr * yr + yi * yi;
        if (count_ < 64) {                    // quick start of the power estimate
            power_ += (pw - power_) / (float)(count_ + 1);
            invPow = power_ > 0 ? 1.f / power_ : 1.f; gain = std::sqrt(invPow);
        } else {
            power_ += (pw - power_) * (1.f / 4096.f);
            if ((count_ & 15) == 0) { invPow = 1.f / power_; gain = std::sqrt(invPow); }
        }
        if (havePrev_) {
            // Gardner: positive when the sampling instant is late
            float e = (mr * (yr - prevRe_) + mi * (yi - prevIm_)) * invPow;
            e = std::max(-2.f, std::min(2.f, e));
            integ_ += ki_ * e;
            corrPrev = kp_ * e + integ_;
            errMs_ += (e * e - errMs_) * 0.002f;
        }
        prevRe_ = yr; prevIm_ = yi; havePrev_ = true;
        o[made++] = cf32(yr * gain, yi * gain);
        count_++;
    }
    corr_ = corrPrev; invPower_ = invPow; gain_ = gain;
    errRms_ = std::sqrt(errMs_);
    out.resize(outBase + made);
    const long drop = (long)std::floor(t_) - m_ - 1;
    if (drop > 0) { buf_.eraseFront((size_t)drop); t_ -= (double)drop; }
}

} // namespace dvbs
} // namespace dect2
