// DVB-S2 frame receiver, see dvbs_s2rx.h.
#include "dvbs_s2rx.h"
#include "dvbs_s2refine.h"
#include "dvbs_symthread.h"
#include "dvbs_compute.h"
#include "dect2/ldpc.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace dect2 {
namespace dvbs {

#if defined(__GNUC__)
#define DVBS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define DVBS_INLINE __forceinline
#else
#define DVBS_INLINE inline
#endif

namespace {
constexpr double kPi = 3.14159265358979323846;
const cf32 kPilot(0.70710678f, 0.70710678f);

// complex product written out (the library one tests for NaN results on every call)
inline cf32 cmul(cf32 a, cf32 b) {
    return cf32(a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real());
}

// e^{-j d} for small d: the loop never turns by more than a fraction of a radian per symbol
inline cf32 smallRot(float d) {
    const float d2 = d * d;
    const float c = 1.f - d2 * 0.5f + d2 * d2 * (1.f / 24.f);
    const float s = d * (1.f - d2 * (1.f / 6.f) + d2 * d2 * (1.f / 120.f));
    return cf32(c, -s);
}

// fast e^x for x <= 0 (weights of the soft detector: a relative error of 1e-4 is plenty)
inline float fexp(float x) {
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
inline float ftanh(float x) {
    const float ax = std::min(std::fabs(x), 12.f);
    const float e = fexp(-2.f * ax);
    const float t = (1.f - e) / (1.f + e);
    return x < 0 ? -t : t;
}

// tanh by linear interpolation in a table (2048 steps over 0 .. 12, error below 4e-6): a few cycles of latency where ftanh needs a polynomial and
// a division, which matters in the carrier loop where every symbol waits for the one before
struct TanhTable {
    static constexpr int kN = 2048;
    static constexpr float kMax = 12.f, kScale = (float)kN / kMax;
    struct E { float v, d; };
    E t[kN + 1];
    TanhTable() {
        for (int i = 0; i <= kN; i++) t[i].v = (float)std::tanh((double)i / kScale);
        for (int i = 0; i < kN; i++) t[i].d = t[i + 1].v - t[i].v;
        t[kN].d = 0.f;
    }
};
const TanhTable kTanh;
inline float ftanhTab(float x) {
    const float u = std::min(std::fabs(x), TanhTable::kMax) * TanhTable::kScale;
    const int i = (int)u;
    const TanhTable::E e = kTanh.t[i];
    const float t = e.v + (u - (float)i) * e.d;
    return std::copysign(t, x);
}

struct Job {
    uint64_t seq = 0;
    int modcod = 0, mod = 0, rate = 0;
    bool sh = false;
    std::vector<cf32> syms;
    float sigma2 = 0.1f;
    uint32_t frameSyms = 0;
};

struct Result {
    uint64_t seq = 0;
    uint32_t frameSyms = 0;
    bool dummy = false, dropped = false, ok = false;
    int iters = 0, bchFixed = 0, mod = 0, rate = 0;
    bool sh = false, gpu = false;
    double berPre = -1;
    std::vector<uint8_t> bb;      // descrambled BBFRAME bits when ok
};

} // namespace

struct S2Rx::Impl {
    S2Rx& o;
    explicit Impl(S2Rx& owner) : o(owner) {}

    std::mutex cbMu;
    std::function<void(const uint8_t*, size_t, double)> pktCb;
    std::function<void(const std::string&)> logCb;
    void log(const std::string& s) { std::function<void(const std::string&)> cb; { std::lock_guard<std::mutex> lk(cbMu); cb = logCb; } if (cb) cb(s); }

    // ------------------------------------------------------------------ carrier loop and frame walker (receiver thread)
    double theta = 0, omega = 0;
    float kpD = 0, kiD = 0, kpiD = 0;        // loop gains (kpiD: the phase step per unit of error, proportional and integral part together)
    bool dry = false;                        // trial run for the frequency search: nothing is submitted or counted
    double dryMetric = 0; int dryHeaders = 0; bool dryAbort = false;
    cf32 ph{1.f, 0.f};
    float gain = 1.f;
    float tanhScale = 0.f;                   // 0.70710678 * invSg2 * the table scale: arguments of tanh in table steps
    float sg2 = 0.1f, invSg2 = 10.f;      // noise variance per real dimension and its inverse: what the soft decision detectors work with
    float kdEma = 1.f; int kdN = 0;       // mean of Re(x conj(soft decision)): the detector gain, for the loop gain correction
    float kdGain = 1.f;
    int phCount = 0;
    int pos = 0, L = 0;
    bool haveParams = false, dummy = false, soft = true;
    bool skip = false;                       // a frame of known length that is not decoded here (S2X VL-SNR, reserved PLS codes): the loop runs free
    bool hdrS2x = false;                     // the last header was an S2X one (b0 = 1): the next is expected to be one too
    float dmin = 1.f;
    int modcod = -1, mod = 0, rate = 0;
    bool sh = false, pil = false;
    S2Dims dims;
    const cf32* constel = nullptr;
    const NearestPoint* nearest = nullptr;
    const std::vector<uint8_t>* rn = nullptr;
    std::vector<cf32> frameSyms;
    cf32 hdr[90];
    double daErr = 0; int daN = 0;           // error energy and count of known symbols in this frame
    double phErr2 = 0; uint64_t phN = 0;
    double merEma = 0;
    int hdrFail = 0;
    int lastHeaderL = 0;                     // frame length since the last header that updated the loop (0: none yet)
    int lastModcod = -1;
    bool vcm = false;
    std::vector<cf32> cellRing;
    size_t cellPos = 0, cellCount = 0;
    SymbolThread th;                         // the frame work runs here
    std::vector<cf32> cellSnap;              // for the interface thread, updated with every frame under statMu
    double phaseSnap = 0; bool lockSnap = false;
    mutable std::mutex statMu;
    S2Stats st;
    uint64_t seqNext = 0;
    int plN = 0;

    // Loop noise bandwidths (times the symbol period). The data loop is narrow: with decisions on noisy symbols a wide loop slips cycles, which costs
    // more than the phase noise of an LNB. The frequency is found before the loop starts (acquire), so it only has to follow drift. Known symbols
    // (SOF, pilots) give a clean phase and may use a wider loop, as long as they are not too noisy.
    double dataBn() const {
        // Narrow near the threshold of the MODCOD (a wide loop slips on noisy symbols), wider with margin (phase noise of the LNB is then the
        // larger part of the error and the decisions are reliable)
        const double esdb = 10 * std::log10(1.0 / (2.0 * (double)sg2));
        const double margin = dummy || mod < 0 ? 0.0 : esdb - s2QefEsN0(mod, rate, sh);
        const double base = mod == kQpsk ? 0.0015 : 0.001, cap = mod == kQpsk ? 0.008 : mod == k8psk ? 0.006 : 0.005;
        return std::min(cap, base * (1.0 + 0.7 * std::max(0.0, margin - 2.0)));
    }
    void setNoise(double v) {
        sg2 = (float)std::max(1e-3, std::min(2.0, v));
        invSg2 = 1.f / sg2;
        tanhScale = 0.70710678f * invSg2 * TanhTable::kScale;
    }
    static void gains(double bnT, double zeta, float& kp, float& ki) {
        const double th = bnT / (zeta + 1.0 / (4.0 * zeta));
        const double d = 1.0 + 2.0 * zeta * th + th * th;
        kp = (float)(4.0 * zeta * th / d);
        ki = (float)(4.0 * th * th / d);
    }
    void setLoops() {
        // overdamped: the frequency is found from the headers (see decodeHeader), the loop only has to follow the phase
        gains(dataBn(), 2.0, kpD, kiD);
        kpiD = kpD + kiD;
    }

    void begin(const S2HuntResult& h) {
        theta = h.theta; omega = h.phi;
        ph = std::polar(1.0f, (float)(-theta));
        gain = h.sofScore > 0.2f && h.sofScore < 5.f ? h.sofScore : 1.f;
        setNoise(h.snrDb > -5.f && h.snrDb < 40.f ? 0.5 * std::pow(10.0, -h.snrDb / 10.0) : 0.1);
        kdGain = 1.f; kdEma = 1.f; kdN = 0;
        // the modulation is not known before the first header is read: the loops are set again then
        setLoops();
        pos = 0; haveParams = false; dummy = false; skip = false;
        hdrS2x = s2ModcodIsS2x(h.modcod);
        frameSyms.clear();
        daErr = 0; daN = 0; hdrFail = 0; lastModcod = -1; vcm = false;
        phErr2 = 0; phN = 0; merEma = 0;
        dryMetric = 0; dryHeaders = 0; dryAbort = false;
        tail.clear(); tailing = false;
        lastHeaderL = 0;
        plN = o.plCode_.load() < 0 ? 0 : o.plCode_.load();
        rn = &s2ScramblingRn(plN);
        cellRing.assign(2048, cf32()); cellPos = 0; cellCount = 0;
        { std::lock_guard<std::mutex> lk(statMu); cellSnap.clear(); phaseSnap = 0; lockSnap = false; }
        { std::lock_guard<std::mutex> lk(statMu); st = S2Stats(); }
        // the first header is at the start: set its parameters from the hunt (the PLS is decoded again as the symbols come in)
        L = 90;
        pilotAcc = cf32(0.f, 0.f);
    }

    // The phase error of a data symbol: Im(x conj(E)), E the expected symbol given x (the mean of the constellation weighted by how likely each point
    // is, which is what an ideal detector at this noise level would use). At high signal to noise ratio E is the nearest point, at low ratio it
    // shrinks toward zero and the error stays small, which is what keeps the loop from chasing noise.
    // tanh(a magnitude scaled by the detector gain) from the table; the argument scale is folded into one constant
    inline float tanhMag(float ax) const {
        const float u = std::min(ax * tanhScale, (float)TanhTable::kN);
        const int i = (int)u;
        const TanhTable::E e = kTanh.t[i];
        return e.v + (u - (float)i) * e.d;
    }
    DVBS_INLINE float ddError(const cf32& x) {
        const float xr = x.real(), xi = x.imag();
        if (!soft) {
            switch (mod) {
            case kQpsk: {
                const float a = 0.70710678f;
                const float sr = xr > 0 ? a : -a, si = xi > 0 ? a : -a;
                return xi * sr - xr * si;
            }
            default: {
                const cf32 sp = constel[nearest->index(xr, xi)];
                return (xi * sp.real() - xr * sp.imag()) / std::max(1e-3f, std::norm(sp));
            }
            }
        }
        float er, ei;
        if (mod == kQpsk) {
            // The error is a * kdGain * (xi sgn(xr) t(xr) - xr sgn(xi) t(xi)), t = tanh of the scaled magnitude: the signs are put on the factors
            // beside the table lookups and the constant factors outside, which keeps the chain from the symbol to the loop correction short.
            const float a = 0.70710678f;
            const float tr = tanhMag(std::fabs(xr)), ti = tanhMag(std::fabs(xi));
            const float xis = std::bit_cast<float>(std::bit_cast<uint32_t>(xi) ^ (std::bit_cast<uint32_t>(xr) & 0x80000000u));
            const float xrs = std::bit_cast<float>(std::bit_cast<uint32_t>(xr) ^ (std::bit_cast<uint32_t>(xi) & 0x80000000u));
            const float err = (a * kdGain) * (xis * tr - xrs * ti);
            er = std::copysign(a * tr, xr); ei = std::copysign(a * ti, xi);
            kdEma += (xr * er + xi * ei - kdEma) * (1.f / 512.f);
            if ((++kdN & 15) == 0) kdGain = 1.f / std::max(0.3f, std::min(1.f, kdEma));
            return err;
        } else {
            // the two nearest points carry the expectation (the third is more than e^-9 weaker at the noise levels where this detector is used)
            int i1, i2;
            nearest->index2(xr, xi, i1, i2);
            const cf32 c1 = constel[i1], c2 = constel[i2];
            const float d1 = (xr - c1.real()) * (xr - c1.real()) + (xi - c1.imag()) * (xi - c1.imag());
            const float d2 = (xr - c2.real()) * (xr - c2.real()) + (xi - c2.imag()) * (xi - c2.imag());
            const float e = fexp(-(d2 - d1) * 0.5f * invSg2);       // weight of the second point against the first
            const float iw = 1.f / (1.f + e);
            er = (c1.real() + e * c2.real()) * iw; ei = (c1.imag() + e * c2.imag()) * iw;
        }
        kdEma += (xr * er + xi * ei - kdEma) * (1.f / 512.f);
        if ((++kdN & 15) == 0) kdGain = 1.f / std::max(0.3f, std::min(1.f, kdEma));
        return (xi * er - xr * ei) * kdGain;
    }

    cf32 pilotAcc{0.f, 0.f};
    // A pilot block is known: its mean phase is the phase error of the loop, with no 90 degree ambiguity. A loop that slipped to the wrong
    // quadrant (the data aided detector holds it there) is put back
    void pilotBlockDone() {
        const float a = std::abs(pilotAcc);
        if (a > 36 * 0.3f) {
            const float psi = std::arg(pilotAcc);
            if (std::fabs(psi) > 0.6f) turnLoop(psi);
            else { const float c = 0.5f * psi; ph *= std::polar(1.0f, -c); theta += c; }      // half of the measured phase error: the pilots are few
        }
        pilotAcc = cf32(0.f, 0.f);
    }
    void turnLoop(float psi) {       // the symbols leaving the loop are turned by psi: take that out
        ph *= std::polar(1.0f, -psi);
        theta += psi;
        { std::lock_guard<std::mutex> lk(statMu); st.phaseJumps++; }
    }

    // The new phase is the old one turned by omega + (kp + ki) * error. The turn by omega alone is known before the error is, it comes in as P (the
    // old phase already turned): what is left to do after the error is a rotation by a small angle (below 0.02 rad: cos and sin to the fourth order
    // are exact to a few 1e-10), so the carrier loop closes faster than with one rotation by the whole step.
    void loopUpdate(float e, const cf32 P) {
        const float de = std::max(-0.5f, std::min(0.5f, e));
        const float kap = kpiD * de, k2 = kap * kap;
        ph = cmul(P, cf32(1.f - k2 * 0.5f, -(kap * (1.f - k2 * (1.f / 6.f)))));
        theta += (float)omega + kap;
        omega += kiD * de;
        if ((++phCount & 31) == 0) ph /= std::abs(ph);
    }

    void step(const cf32 z) {
        // the scaling and the descrambling turn do not depend on the carrier loop and are done on the symbol first
        const float ig = 1.f / gain;
        cf32 zs(z.real() * ig, z.imag() * ig);
        if (pos >= 90) zs = s2RotateByR(zs, (4 - (int)(*rn)[pos - 90]) & 3);
        const cf32 P = cmul(ph, smallRot((float)omega));
        const cf32 u = cmul(zs, ph);
        float err = 0;
        bool known = false;
        cf32 ref(0, 0);
        if (pos < 26) {
            ref = s2SofSymbols()[pos]; known = true;
            err = u.imag() * ref.real() - u.real() * ref.imag();
            hdr[pos] = u;
        } else if (pos < 90) {
            hdr[pos] = u;
            // pi/2 BPSK: the point lies on the diagonal (even index) or the anti-diagonal (odd index), a bit is its sign. Soft decision: the mean of the
            // two points weighted by how likely each is at this noise level. An S2X header is turned by 90 degrees after the SOF: the detector for the
            // other orientation would push the loop away from the right phase, so it works on the symbol turned back, the orientation of the header
            // before (the next one is the same in all but a mixed stream)
            const cf32 w = hdrS2x ? cf32(u.imag(), -u.real()) : u;
            const float v = 0.70710678f * ((pos & 1) ? (w.imag() - w.real()) : (w.real() + w.imag()));
            const float t = ftanh(v * invSg2);
            const cf32 s = t * ((pos & 1) ? cf32(-0.70710678f, 0.70710678f) : cf32(0.70710678f, 0.70710678f));
            err = w.imag() * s.real() - w.real() * s.imag();
        } else {
            const int i = pos - 90;
            const cf32 x = u;      // (the descrambling turn was applied to the symbol before the loop phase)
            bool isPilot = false;
            if (dummy) isPilot = true;
            else if (pil) { const int per = i % 1476; isPilot = per >= 1440; }
            if (skip) {
                // nothing is known of these symbols: no error for the loop
            } else if (isPilot) {
                known = true; ref = kPilot;
                err = x.imag() * ref.real() - x.real() * ref.imag();
                daErr += std::norm(x - ref); daN++;
                if (!dummy) {
                    pilotAcc += x * std::conj(ref);
                    if (i % 1476 == 1475) pilotBlockDone();
                }
            } else {
                err = ddError(x);
                frameSyms.push_back(x);
                if ((frameSyms.size() & 7) == 0) { cellRing[cellPos] = x; cellPos = (cellPos + 1) & 2047; cellCount++; }
            }
        }
        if (known) { phErr2 += (double)err * err; phN++; }
        loopUpdate(err, P);
        pos++;
        if (pos == 90) decodeHeader();
        else if (pos == L && pos > 90) finishFrame();
    }

    // ---- header position tracking. The symbol clock of the front end can slip by a symbol (noise in the timing loop, a clock offset): the next header then
    // comes one symbol early or late. The last symbols of a frame and the 98 after them are held back, the header is looked for at the expected place
    // and four symbols either side with the code word of the frame before, and the frame walk goes on from the best place.
    static constexpr int kTailBefore = 4, kTailAfter = 4, kTailLen = kTailBefore + 90 + kTailAfter;
    std::vector<cf32> tail;
    bool tailing = false;

    void feedSymbol(const cf32 v) {
        if (tailing) {
            tail.push_back(v);
            if ((int)tail.size() == kTailLen) resolveTail();
            return;
        }
        if (!dry && L > 400 && L < (1 << 20) && pos == L - kTailBefore) { tailing = true; tail.clear(); tail.push_back(v); return; }
        step(v);
    }

    void resolveTail() {
        tailing = false;
        // the loop state at the first held symbol: phase rotation and frequency carry on over the held symbols
        const cf32* ref90 = nullptr;
        cf32 hh[90];
        if (haveParams && !dummy) { s2PlHeader(modcod, sh, pil, hh); ref90 = hh; }
        const cf32* sof = s2SofSymbols();
        cf32 u[kTailLen];
        {
            cf32 r = ph;
            const cf32 st = smallRot((float)omega);
            for (int k = 0; k < kTailLen; k++) { u[k] = tail[(size_t)k] * r * (1.f / gain); r *= st; }
        }
        double bestM = -1, m0 = 0; int bd = 0;
        for (int d = -kTailBefore; d <= kTailAfter; d++) {
            const int a = kTailBefore + d;
            cf32 acc(0, 0);
            const int cnt = ref90 ? 90 : 26;
            const cf32* rf = ref90 ? ref90 : sof;
            for (int k = 0; k < cnt; k++) acc += u[a + k] * std::conj(rf[k]);
            const double m = std::abs(acc);
            if (d == 0) m0 = m;
            if (m > bestM) { bestM = m; bd = d; }
        }
        // the expected place wins unless another is clearly better
        if (bd != 0 && !(bestM > 1.5 * m0 && bestM > (ref90 ? 90 : 26) * 0.25)) bd = 0;
        // symbols of the frame that is ending
        const int dataSyms = kTailBefore + std::min(bd, 0);
        for (int k = 0; k < dataSyms; k++) step(tail[(size_t)k]);
        if (pos < L && haveParams) { /* a symbol was lost: the frame is short, it ends here */ if (pos > 90) finishFrame(); }
        if (bd != 0) {
            std::lock_guard<std::mutex> lk(statMu);
            st.timingSlips++;
        }
        if (pos != 0) { pos = 0; L = 90; frameSyms.clear(); daErr = 0; daN = 0; }
        for (int k = kTailBefore + bd; k < kTailLen; k++) {
            step(tail[(size_t)k]);
            if (!o.tracking_.load()) break;
        }
        tail.clear();
    }

    void decodeHeader() {
        // header symbols after the carrier loop: SOF check and PLS decode
        cf32 acc(0, 0);
        const cf32* sof = s2SofSymbols();
        for (int k = 0; k < 26; k++) acc += hdr[k] * std::conj(sof[k]);
        // the SOF is known: its phase is the error of the loop, with no ambiguity. Whatever it is, the header is read with it taken out
        const float psi = std::abs(acc) > 1e-3f ? std::arg(acc) : 0.f;
        const cf32 derot = std::polar(1.0f, -psi);
        for (int k = 0; k < 90; k++) hdr[k] *= derot;
        const float sofScore = std::abs(acc) / 26.f;
        if (dry && dryHeaders++ >= 1) dryMetric += (double)(acc.real() / 26.f);    // how well the loop kept the phase since the last header
        const PlsResult pr = s2PlsDecode(&hdr[26]);
        bool good = sofScore > 0.25f && pr.score > 0.30f && pr.score - pr.second > 0.12f && s2PlsKind(pr.code) >= 0;
        PlsResult use = pr;
        if (!good && haveParams && sofScore > 0.25f) {
            // a noisy header: the code word of the frame before is by far the most likely one (constant coding and modulation is the usual case)
            const float sp = s2PlsScore(&hdr[26], modcod, sh, pil);
            if (sp > 0.30f && sp >= pr.score - 0.10f) {
                use.modcod = modcod; use.shortFrame = sh; use.pilots = pil; use.score = sp; use.code = s2PlsValue(modcod, sh, pil); good = true;
            }
        }
        int mc = use.modcod, md = 0, rt = 0;
        bool bs = use.shortFrame, bp = use.pilots;
        // data frames need a MODCOD this receiver has; dummy frames and the S2X frames it does not decode only need their length
        const int kind = good ? s2PlsKind(use.code) : -1;
        bool useful = kind >= 0;
        if (kind == 0 && (!s2ModcodSplit(mc, md, rt) || !s2Dims(md, rt, bs).ok)) useful = false;
        if (!useful) {
            if (dry) { dryAbort = true; return; }
            hdrFail++;
            { std::lock_guard<std::mutex> lk(statMu); st.headerBad++; st.consecutiveHeaderBad = hdrFail; }
            if (haveParams && hdrFail <= 3) { L = frameLen(modcod, mod, rate, sh, pil); setupFrame(modcod, mod, rate, sh, pil, true); lastHeaderL = 0; return; }
            o.lost_ = true; o.tracking_ = false;
            log("DVB-S2: frame synchronisation lost");
            L = 1 << 30;      // stop interpreting symbols: the owner will hunt again
            return;
        }
        hdrFail = 0;
        { std::lock_guard<std::mutex> lk(statMu); st.consecutiveHeaderBad = 0; }
        // the header is now known: the data aided phase error over its 90 symbols, and the amplitude
        cf32 known[90];
        for (int k = 0; k < 26; k++) known[k] = sof[k];
        cf32 hh[90]; s2PlHeader(mc, bs, bp, hh);
        for (int k = 26; k < 90; k++) known[k] = hh[k];
        cf32 a2(0, 0); double e2 = 0;
        for (int k = 0; k < 90; k++) a2 += hdr[k] * std::conj(known[k]);
        const float g = std::abs(a2) / 90.f;
        const cf32 rot = a2 / std::max(1e-6f, std::abs(a2));      // residual phase of the header
        for (int k = 0; k < 90; k++) e2 += std::norm(hdr[k] * std::conj(rot) / std::max(g, 0.2f) - known[k]);
        daErr = e2; daN = 90;
        // Frame rate update of the carrier loop from the 90 known symbols: the phase error of the loop over the header is the phase and, from one
        // header to the next, the frequency error times the frame length (a few millionths of a radian per symbol precision, far better than the data
        // loop could find in noise). A large error is a slip of the loop: the phase is put back and the frequency left alone.
        {
            float psiFull = psi + std::arg(a2);
            while (psiFull > 3.14159265f) psiFull -= 6.2831853f;
            while (psiFull < -3.14159265f) psiFull += 6.2831853f;
            if (std::fabs(psiFull) > 0.35f) { if (!dry) turnLoop(psiFull); else { ph *= std::polar(1.0f, -psiFull); theta += psiFull; } }
            else {
                ph *= std::polar(1.0f, -psiFull); theta += psiFull;
                if (lastHeaderL > 0) omega += 0.6 * (double)psiFull / (double)lastHeaderL;
            }
        }
        // a correction of the amplitude from what the header says (damped: the symbols are noisy)
        (void)rot;
        if (g > 0.2f && g < 5.f) gain *= std::pow(g, 0.7f);
        // the rest of the frame
        if (lastModcod >= 0 && mc != lastModcod) vcm = true;
        lastModcod = mc;
        setupFrame(mc, md, rt, bs, bp, false);
        L = frameLen(mc, md, rt, bs, bp);
        lastHeaderL = L;
        haveParams = true;
    }

    // The soft detector where the noise is large against the spacing of the points. The DVB-S2 APSK constellations do without it (their MODCODs
    // run at a high Es/N0, where it costs time and gains nothing); the S2X ones run down to a few dB above the Es/N0 of QPSK 3/4, where hard
    // decisions slip the loop (16APSK 7/15 short at 9 dB lost a frame in a thousand without it)
    bool softWanted() const { return (mod <= k8psk || s2IsS2x(rate)) && sg2 > (dmin * 0.125f) * (dmin * 0.125f); }

    int frameLen(int mc, int md, int rt, bool bs, bool bp) {
        (void)md; (void)rt;
        return s2PlsFrameSymbols(s2PlsValue(mc, bs, bp));
    }

    void setupFrame(int mc, int md, int rt, bool bs, bool bp, bool flywheel) {
        skip = s2PlsKind(s2PlsValue(mc, bs, bp)) == 2;
        hdrS2x = s2PlsValue(mc, bs, bp) >= 128;
        modcod = mc; sh = bs; pil = bp;
        if (!skip) { mod = md; rate = rt; }      // a frame that is not decoded keeps the loop settings of the one before
        dummy = mc == 0;
        if (!dummy && !skip) setLoops();
        if (!dummy && !skip) {
            dims = s2Dims(md, rt, bs); constel = s2Constellation(md, rt); nearest = &nearestPoint(md, rt);
            const int P = s2ConstellationSize(md);
            dmin = 1e9f;
            for (int i = 0; i < P; i++) for (int j = i + 1; j < P; j++) dmin = std::min(dmin, std::abs(constel[i] - constel[j]));
            soft = softWanted();
        }
        frameSyms.clear();
        if (!dummy && !skip) frameSyms.reserve(dims.xfecSymbols);
        if (!flywheel) { /* the DA statistics of the header were set by the caller */ }
        else { daErr = 0; daN = 0; }
        std::lock_guard<std::mutex> lk(statMu);
        if (skip) { st.unsupportedCode = s2PlsValue(mc, bs, bp); return; }    // the reported MODCOD stays the one decoded
        st.modcod = mc; st.mod = dummy ? st.mod : md; st.rate = dummy ? st.rate : rt; st.shortFrame = bs; st.pilots = bp; st.vcm = vcm;
    }

    void finishFrame() {
        if (dry) { frameSyms.clear(); pos = 0; L = 90; daErr = 0; daN = 0; return; }
        const uint32_t fs = (uint32_t)L;
        const double sigma2 = daN > 0 ? std::max(1e-4, daErr / daN / 2.0) : 0.1;    // per real dimension
        if (daN > 0) {
            const double mer = 10 * std::log10(std::max(1e-9, (double)daN / std::max(daErr, 1e-9)));
            merEma = merEma == 0 ? mer : 0.7 * merEma + 0.3 * mer;
        }
        if (daN >= 90) { setNoise(0.5 * sg2 + 0.5 * sigma2); if (!dummy && !skip) soft = softWanted(); }
        {
            std::lock_guard<std::mutex> lk(statMu);
            st.framesSeen++;
            st.merDb = merEma; st.snrDb = merEma;
            st.carrierRadPerSym = omega;
            const size_t have = std::min<size_t>(cellCount, 2048);
            cellSnap.resize(have);
            for (size_t i = 0; i < have; i++) cellSnap[i] = cellRing[(cellPos + 2048 - have + i) & 2047];
            phaseSnap = phN ? std::sqrt(phErr2 / (double)phN) : 0.0;
            lockSnap = phN > 200 && phaseSnap < 0.6;
        }
        if (dummy || skip) {
            { std::lock_guard<std::mutex> lk(statMu); if (dummy) st.framesDummy++; else st.framesUnsupported++; }
            Result r; r.seq = seqNext++; r.frameSyms = fs; r.dummy = true;
            deposit(std::move(r));
        } else {
            Job j;
            j.seq = seqNext++; j.modcod = modcod; j.mod = mod; j.rate = rate; j.sh = sh;
            j.syms = std::move(frameSyms); j.sigma2 = (float)sigma2; j.frameSyms = fs;
            frameSyms.clear();
            submit(std::move(j));
        }
        pos = 0; L = 90;
        daErr = 0; daN = 0;
    }

    // ------------------------------------------------------------------ frequency search
    // The frame search gives the carrier frequency to a few thousandths of a radian per symbol. A frame (up to 33 000 symbols) needs it to a few
    // millionths. The two verified headers do that: the known 90 symbols of each give a phase, and the phase difference over one frame length L
    // fixes the frequency up to a multiple of 2 pi / L (about 2e-4 rad/symbol), to within a few millionths. The right multiple is found from the data:
    // the first frame is derotated with each candidate frequency, hard decisions are made, and the one whose bits satisfy the most LDPC parity
    // checks is the right one (a wrong one turns the phase by whole cycles over the frame, which leaves a random word). If that does not give a
    // clear answer, the carrier loop is tried at a grid of frequencies instead (acquireByLoop).
    static void descramblePoint(cf32& x, int r) { x = s2RotateByR(x, (4 - r) & 3); }

    // Parity check failures of the hard decisions of one frame, derotated with theta0 + omega * k (k counted from the first symbol of the header)
    double frameSyndrome(const cf32* z, int L, double theta0, double omega, float gain, int md, int rt, bool bs, bool bp) {
        const S2Dims dm = s2Dims(md, rt, bs);
        const NearestPoint& np = nearestPoint(md, rt);
        const int m = md + 2;
        const std::vector<uint8_t>& R = s2ScramblingRn(plN);
        std::vector<float> sgn((size_t)dm.xfecSymbols * (size_t)m), de((size_t)dm.nldpc);
        const cf32 stp = std::polar(1.0f, (float)(-omega));
        cf32 rot = std::polar(1.0f, (float)(-theta0 - omega * 90.0));
        const float ig = 1.f / gain;
        int si = 0;
        for (int i = 0; i < L - 90; i++) {
            const cf32 x = z[90 + i] * rot * ig;
            rot *= stp;
            if ((i & 255) == 255) rot /= std::abs(rot);
            if (bp && (i % 1476) >= 1440) continue;
            cf32 y = x;
            descramblePoint(y, R[(size_t)i]);
            const int l = np.index(y.real(), y.imag());
            if (si >= dm.xfecSymbols) break;
            for (int b = 0; b < m; b++) sgn[(size_t)si * m + b] = ((l >> (m - 1 - b)) & 1) ? -1.f : 1.f;
            si++;
        }
        if (si != dm.xfecSymbols) return 1.0;
        s2BitDeinterleaveLlr(sgn.data(), dm.nldpc, md, rt, de.data());
        const LdpcCode& code = s2Ldpc(rt, bs);
        const std::vector<int>& cs = code.chkStart();
        const std::vector<int>& cv = code.chkVar();
        const int nChecks = (int)cs.size() - 1;
        int bad = 0;
        for (int c = 0; c < nChecks; c++) {
            int x = 0;
            for (int j = cs[(size_t)c]; j < cs[(size_t)c + 1]; j++) x ^= de[(size_t)cv[(size_t)j]] < 0 ? 1 : 0;
            bad += x;
        }
        return (double)bad / (double)nChecks;
    }

    bool acquireByHeaders(const S2HuntResult& h, const cf32* win, size_t n, S2HuntResult& out) {
        int md = 0, rt = 0;
        if (h.modcod < 1 || !s2ModcodSplit(h.modcod, md, rt)) return false;
        const S2Dims dm = s2Dims(md, rt, h.shortFrame);
        if (!dm.ok) return false;
        const int L = s2FrameSymbols(dm, h.pilots);
        const bool inv = h.inverted;
        auto zAt = [&](size_t i) { return inv ? std::conj(win[i]) : win[i]; };
        if (h.firstPos + (size_t)L + 95 > n) return false;
        cf32 ref[90];
        s2PlHeader(h.modcod, h.shortFrame, h.pilots, ref);
        const double w0 = h.phi;
        // header positions (the frame search may be a symbol off): the best coherent correlation of the 26 symbols of the SOF near each
        size_t pq[2];
        for (int t = 0; t < 2; t++) {
            const long c = (long)h.firstPos + (long)t * L;
            double bm = -1; long bq = c;
            for (long d = -2; d <= 2; d++) {
                const long q = c + d;
                if (q < 0 || (size_t)q + 95 > n) continue;
                cf32 acc(0, 0);
                for (int k = 0; k < 26; k++) acc += zAt((size_t)q + (size_t)k) * std::polar(1.0f, (float)(-w0 * k)) * std::conj(ref[k]);
                if (std::abs(acc) > bm) { bm = std::abs(acc); bq = q; }
            }
            pq[t] = (size_t)bq;
        }
        if (pq[1] - pq[0] != (size_t)L) { /* the two headers are not a frame apart: take the first and keep the length */ pq[1] = pq[0] + (size_t)L; }
        auto headerSum = [&](size_t q, double w) {
            cf32 acc(0, 0);
            for (int k = 0; k < 90; k++) acc += zAt(q + (size_t)k) * std::polar(1.0f, (float)(-w * k)) * std::conj(ref[k]);
            return acc;
        };
        const cf32 s1 = headerSum(pq[0], w0), s2 = headerSum(pq[1], w0);
        if (std::abs(s1) < 90 * 0.15f || std::abs(s2) < 90 * 0.15f) return false;
        const double psi = std::arg(s2 * std::conj(s1));
        const double span = h.snrDb < 4.f ? 0.006 : 0.0045;
        const double stepW = 2.0 * 3.14159265358979323846 / (double)L;
        // the phase of the second header minus that of the first is the true frequency times L (the same local derotation is applied to both)
        const int mmin = (int)std::ceil(((w0 - span) - psi / L) / stepW), mmax = (int)std::floor(((w0 + span) - psi / L) / stepW);
        std::vector<cf32> conjFrame;
        const cf32* frame = win + pq[0];
        if (inv) { conjFrame.resize((size_t)L); for (int i = 0; i < L; i++) conjFrame[(size_t)i] = std::conj(win[pq[0] + (size_t)i]); frame = conjFrame.data(); }
        double best = 2.0, second = 2.0, bestW = w0, bestTh = h.theta;
        std::vector<double> all;
        // nearest to the frequency of the header fit first: a wrong frequency leaves a random word (0.5 of the checks fail), the right one far fewer, so
        // the first candidate under 0.3 is the answer (at low SNR none is, and all of them are tried as before)
        std::vector<int> order;
        for (int mm = mmin; mm <= mmax; mm++) order.push_back(mm);
        std::sort(order.begin(), order.end(), [&](int x, int y) { return std::fabs(psi / L + x * stepW - w0) < std::fabs(psi / L + y * stepW - w0); });
        for (int mm : order) {
            const double w = psi / L + mm * stepW;
            const cf32 s = headerSum(pq[0], w);
            const double th = std::arg(s);
            const float gain = std::max(0.2f, std::min(3.f, std::abs(s) / 90.f));
            const double f = frameSyndrome(frame, L, th, w, gain, md, rt, h.shortFrame, h.pilots);
            all.push_back(f);
            if (f < best) { second = best; best = f; bestW = w; bestTh = th; }
            else if (f < second) second = f;
            if (best < 0.3) { second = 0.5; break; }
        }
        if (all.empty()) return false;
        // clear answer: far below the random level (0.5 for a bad frequency, with a spread of 0.5 / sqrt(checks)) and well below the runner-up
        const double sd = 0.5 / std::sqrt((double)(dm.nldpc - dm.kldpc));
        if (best > 0.5 - 10 * sd || second - best < 10 * sd) return false;
        out = h;
        out.pos = pq[0];
        out.phi = bestW;
        out.theta = bestTh;
        out.sofScore = std::max(0.2f, std::min(3.f, std::abs(headerSum(pq[0], bestW)) / 90.f));
        return true;
    }

    S2HuntResult acquire(const S2HuntResult& h, const cf32* win, size_t n) {
        if (h.headers < 2 || h.firstPos + 90 >= n) return h;
        plN = o.plCode_.load() < 0 ? 0 : o.plCode_.load();
        S2HuntResult r;
        if (acquireByHeaders(h, win, n, r)) return r;
        return acquireByLoop(h, win, n);
    }

    S2HuntResult acquireByLoop(const S2HuntResult& h, const cf32* win, size_t n) {
        if (h.headers < 2 || h.firstPos + 90 >= n) return h;
        const bool inv = h.inverted;
        auto zAt = [&](size_t i) { return inv ? std::conj(win[i]) : win[i]; };
        const cf32* sof = s2SofSymbols();
        const size_t end = std::min(n, h.lastPos + 90);
        // The data loop pulls in a frequency error of about a third of the range in which its detector is linear (the spacing of the constellation
        // points: 90 degrees for QPSK, 45 for 8PSK, 15 to 30 for APSK), so the trial frequencies are that close together
        int mod0 = 0, rate0 = 0;
        if (!s2ModcodSplit(h.modcod, mod0, rate0)) mod0 = 0;
        static const double kStep[kS2Mods] = {0.0010, 0.0005, 0.0004, 0.0003, 0.0002, 0.0002, 0.0002};
        const double dw = kStep[mod0 >= 0 && mod0 < kS2Mods ? mod0 : 0];
        const double spanW = h.snrDb < 4.f ? 0.006 : 0.004;
        const int K = (int)std::ceil(spanW / dw);
        S2HuntResult best = h;
        double bestM = -1e30;
        for (int j = -K; j <= K; j++) {
            const double w = h.phi + j * dw;
            // refine the header position with the coherent SOF correlation at this frequency
            size_t bf = h.firstPos;
            double bm = -1;
            cf32 bacc(0, 0);
            for (long d = -2; d <= 2; d++) {
                const long q = (long)h.firstPos + d;
                if (q < 0 || (size_t)q + 90 >= n) continue;
                cf32 acc(0, 0);
                for (int k = 0; k < 26; k++) acc += zAt((size_t)q + (size_t)k) * std::polar(1.0f, (float)(-w * k)) * std::conj(sof[k]);
                if (std::abs(acc) > bm) { bm = std::abs(acc); bf = (size_t)q; bacc = acc; }
            }
            if (bm <= 0) continue;
            S2HuntResult hj = h;
            hj.phi = w; hj.theta = std::arg(bacc); hj.pos = bf;
            begin(hj);
            dry = true;
            for (size_t i = bf; i < end && !dryAbort; i++) step(zAt(i));
            dry = false;
            const double m = dryAbort || dryHeaders < 2 ? -1e9 : dryMetric / (double)(dryHeaders - 1);
            if (m > bestM) { bestM = m; best = hj; }
        }
        if (bestM < 0.3) return h;      // nothing stayed locked: the frame search's own estimate at its latest header
        return best;
    }

    // The data and pilot symbols of a frame, the common case, taken in one run with the state of the carrier loop in registers (the same work as
    // step() does for these symbols). Returns the number of symbols taken: it stops before the symbol that ends a pilot block, before the held back
    // symbols at the end of the frame and before the end of the frame, which the general path handles.
    size_t fastRun(const cf32* z, size_t n, bool inverted) {
        if (tailing || dummy || skip || !rn || pos < 90 || L <= 90) return 0;
        const bool tailLogic = !dry && L > 400 && L < (1 << 20);
        const int stop = tailLogic ? L - kTailBefore : L - 1;
        if (pos >= stop) return 0;
        const size_t cnt = std::min<size_t>(n, (size_t)(stop - pos));
        const bool pilots = pil;
        const uint8_t* rnv = rn->data();
        const float ig = 1.f / gain;
        int per = pilots ? (pos - 90) % 1476 : 0;
        cf32 phl = ph, pacc = pilotAcc;
        double th = theta, om = omega, dErr = daErr, pe2 = phErr2;
        int dN = daN;
        uint64_t pN = phN;
        int pc = phCount, p = pos;
        size_t k = 0;
        for (; k < cnt; k++) {
            if (pilots && per == 1475) break;
            cf32 zin = z[k];
            if (inverted) zin = cf32(zin.real(), -zin.imag());
            const cf32 zs = s2RotateByR(cf32(zin.real() * ig, zin.imag() * ig), (4 - (int)rnv[p - 90]) & 3);
            const cf32 P = cmul(phl, smallRot((float)om));
            const cf32 x = cmul(zs, phl);
            float err;
            if (pilots && per >= 1440) {
                err = x.imag() * kPilot.real() - x.real() * kPilot.imag();
                const float dr = x.real() - kPilot.real(), di = x.imag() - kPilot.imag();
                dErr += (double)(dr * dr + di * di); dN++;
                pacc = cf32(pacc.real() + (x.real() * kPilot.real() + x.imag() * kPilot.imag()), pacc.imag() + (x.imag() * kPilot.real() - x.real() * kPilot.imag()));
                pe2 += (double)err * err; pN++;
            } else {
                err = ddError(x);
                frameSyms.push_back(x);
                if ((frameSyms.size() & 7) == 0) { cellRing[cellPos] = x; cellPos = (cellPos + 1) & 2047; cellCount++; }
            }
            const float de = std::max(-0.5f, std::min(0.5f, err));
            const float kap = kpiD * de, k2 = kap * kap;
            phl = cmul(P, cf32(1.f - k2 * 0.5f, -(kap * (1.f - k2 * (1.f / 6.f)))));
            th += (float)om + kap;
            om += kiD * de;
            if ((++pc & 31) == 0) phl /= std::abs(phl);
            p++;
            if (pilots) per++;
        }
        ph = phl; pilotAcc = pacc; theta = th; omega = om; daErr = dErr; phErr2 = pe2;
        daN = dN; phN = pN; phCount = pc; pos = p;
        return k;
    }

    void feedBlock(const cf32* z, size_t n) {
        const bool inv = o.inverted_.load();
        size_t i = 0;
        while (i < n && o.tracking_.load()) {
            const size_t used = fastRun(z + i, n - i, inv);
            if (used) { i += used; continue; }
            feedSymbol(inv ? std::conj(z[i]) : z[i]);
            i++;
        }
    }

    // Code rates from which the CPU decoder works on 8-bit values only; DECT2_LDPC_Q8_MIN_RATE overrides. Rate 2/3 (QPSK and 8PSK) decodes as
    // well as with floats down to the standard's threshold, and on a small 32-bit ARM core (memory bound) it costs up to 45% less; 3/5 and below lose
    // sensitivity with 8 bits
    std::atomic<bool> force8{false};   // setLdpc8(): the 8-bit decoder at every code rate
    static double q8MinRate() {
        static const double v = [] {
            if (const char* e = getenv("DECT2_LDPC_Q8_MIN_RATE")) return atof(e);
#if defined(__arm__) && !defined(__aarch64__)
            return 0.65;
#else
            return 0.7;
#endif
        }();
        return v;
    }
    // Below that rate: a few 8-bit iterations first, then the float decoder. A small 32-bit ARM core is limited by memory traffic (the float messages
    // of a frame do not fit its cache), where this is much cheaper for frames that decode at once; DECT2_LDPC_Q8_QUICK=0/1 overrides.
    static bool q8Quick() {
        static const bool v = [] {
            if (const char* e = getenv("DECT2_LDPC_Q8_QUICK")) return atoi(e) != 0;
#if defined(__arm__) && !defined(__aarch64__)
            return true;
#else
            return false;
#endif
        }();
        return v;
    }

    // ------------------------------------------------------------------ decoder threads
    std::vector<std::thread> workers;
    std::mutex qMu;
    std::condition_variable qCv, spaceCv;
    std::deque<Job> queue;
    bool stopFlag = false;
    size_t maxQueue = 6;
    std::mutex resMu;
    std::map<uint64_t, Result> results;
    uint64_t nextOut = 0;

    void startWorkers() {
        stopWorkers();
        stopFlag = false;
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
#if defined(__arm__) && !defined(__aarch64__)
        // 32-bit ARM boards (Zynq: two cores): the decoder is the slowest stage, so it gets both cores; the receiver thread shares them
        const unsigned n = std::max(2u, std::min(6u, hw));
#else
        const unsigned n = std::max(1u, std::min(6u, hw > 2 ? hw - 2 : 1u));
#endif
        for (unsigned i = 0; i < n; i++) workers.emplace_back([this] { workerLoop(); });
        maxQueue = 2 * n + 2;
#if defined(__arm__) && !defined(__aarch64__)
        // the receiver thread stalls now and then (the lock hunt, about a second of CPU) and then catches up faster than real time while the decoders
        // share the two cores with it: a queue of about 4 s of frames (about 260 kB each) rides that out instead of dropping frames
        maxQueue = 64;
#endif
    }
    void stopWorkers() {
        { std::lock_guard<std::mutex> lk(qMu); stopFlag = true; }
        qCv.notify_all(); spaceCv.notify_all();
        for (auto& t : workers) if (t.joinable()) t.join();
        workers.clear();
        queue.clear();
    }

    void submit(Job&& j) {
        std::unique_lock<std::mutex> lk(qMu);
        if (queue.size() >= maxQueue) {
            if (o.blocking_.load()) spaceCv.wait(lk, [&] { return queue.size() < maxQueue || stopFlag; });
            if (queue.size() >= maxQueue) {                 // behind: this frame is skipped
                lk.unlock();
                Result r; r.seq = j.seq; r.frameSyms = j.frameSyms; r.dropped = true;
                deposit(std::move(r));
                return;
            }
        }
        queue.push_back(std::move(j));
        lk.unlock();
        qCv.notify_one();
    }

    ComputeChoice compute;

    // The front half of a frame's work: phase refinement, demapper, bit deinterleaver. dl receives the LDPC input (nldpc values).
    static bool prepareFrame(Job& j, const S2Dims& d, std::vector<float>& llr, float* dl) {
        if ((int)j.syms.size() != d.xfecSymbols) return false;
        llr.resize((size_t)d.xfecSymbols * (size_t)d.bitsPerSym);
        {
            // the phase error the carrier loop left, measured in blocks against decisions (block length from the noise level)
            const float es = 1.f / (2.f * std::max(j.sigma2, 1e-3f));
            const int W = std::max(8, std::min(512, (int)(83.f / es)));
            // QPSK: one pass. A second changes nothing there (measured on radio recordings and on generated signals with LNB phase noise: the pre-FEC
            // bit error rate is the same or a little better with one pass), while 8PSK and the APSK constellations gain from two
            s2RefinePhase(j.syms.data(), d.xfecSymbols, j.mod, j.rate, j.sigma2, W, std::vector<PhaseMark>(), j.mod == kQpsk ? 1 : 2);
        }
        s2Demap(j.syms.data(), d.xfecSymbols, j.mod, j.rate, j.sigma2, llr.data());
        s2BitDeinterleaveLlr(llr.data(), d.nldpc, j.mod, j.rate, dl);
        return true;
    }

    // The back half: BCH, BBHEADER descrambling and the bit error estimate from a decoded code word
    static void finishFrame(const Job& j, const S2Dims& d, const float* dl, const uint8_t* hard, bool ldpcOk, int iters, bool gpu, std::vector<uint8_t>& bb, Result& r) {
        r.iters = iters; r.gpu = gpu;
        if (!ldpcOk) return;
        const int fixed = s2BchDecode(hard, j.rate, j.sh, bb);
        if (fixed < 0) return;
        s2BbScramble(bb.data(), d.kbch);
        r.ok = true; r.bchFixed = fixed; r.bb = bb;
        // bit errors before the LDPC decoder: hard decisions on the input against the decoded code word
        size_t diff = 0;
        for (int i = 0; i < d.nldpc; i++) diff += (dl[(size_t)i] < 0) != (hard[(size_t)i] != 0);
        r.berPre = (double)diff / d.nldpc;
    }

    void workerLoop() {
#if defined(__linux__)
        // Optional: DECT2_WORKER_NICE_OFFSET (default 0 = off) is added to the nice value the decoder threads inherited (at most 19), so that they run at a lower
        // priority than the front end and carrier loop that rx --iio / --mqtt makes more important (-10). Measured live on a 1.5 Msym/s beacon on a Zynq (two
        // cores, not enough for it): an offset of 10 halves the lost frame synchronisations (6 against 11 in 40 s) but the input still drops signal as often
        // and the delivered transport stream does not improve (about 510-560 packets/s against 555 with the same priority), so it is off by default.
        {
            static const int off = [] { const char* e = getenv("DECT2_WORKER_NICE_OFFSET"); return e ? atoi(e) : 0; }();
            const int cur = getpriority(PRIO_PROCESS, 0);
            if (off > 0 && cur < 0) setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), std::min(19, cur + off));
        }
#endif
        int quickScore = 8;
        unsigned quickTick = 0;
        std::vector<float> llr, dlAll;
        std::vector<uint8_t> hard, bb, hardAll, okAll;
        std::vector<int> itAll;
        constexpr size_t kBatch = 8;
        for (;;) {
            std::vector<Job> jobs;
            bool gpu = false, probe = false;
            {
                std::unique_lock<std::mutex> lk(qMu);
                qCv.wait(lk, [&] { return !queue.empty() || stopFlag; });
                if (stopFlag && queue.empty()) return;
                jobs.push_back(std::move(queue.front()));
                queue.pop_front();
                gpu = compute.pickGpu(&probe);
                // frames of the same MODCOD that are already waiting go to the GPU together: one call decodes them all
                while (gpu && jobs.size() < kBatch && !queue.empty() && queue.front().mod == jobs[0].mod && queue.front().rate == jobs[0].rate && queue.front().sh == jobs[0].sh) {
                    jobs.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
            }
            spaceCv.notify_all();
            const Job& j0 = jobs[0];
            const S2Dims d = s2Dims(j0.mod, j0.rate, j0.sh);
            const size_t nb = jobs.size();
            std::vector<Result> res(nb);
            std::vector<char> usable(nb, 0);
            dlAll.resize(nb * (size_t)d.nldpc);
            for (size_t i = 0; i < nb; i++) {
                res[i].seq = jobs[i].seq; res[i].frameSyms = jobs[i].frameSyms; res[i].mod = jobs[i].mod; res[i].rate = jobs[i].rate; res[i].sh = jobs[i].sh;
                usable[i] = prepareFrame(jobs[i], d, llr, dlAll.data() + i * (size_t)d.nldpc) ? 1 : 0;
            }
            const LdpcCode& code = s2Ldpc(j0.rate, j0.sh);
            const float alpha = s2LdpcAlpha(j0.rate);
            bool gpuDone = false;
            if (gpu) {
                // unusable frames (wrong symbol count) are decoded as zeros and ignored
                hardAll.resize(nb * (size_t)d.nldpc); okAll.assign(nb, 0); itAll.assign(nb, 0);
                const auto t0 = std::chrono::steady_clock::now();
                gpuDone = GpuLdpc::instance().decode(code, dlAll.data(), (int)nb, 50, hardAll.data(), okAll.data(), itAll.data(), alpha);
                if (gpuDone) compute.report(true, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / (double)nb);
            }
            for (size_t i = 0; i < nb; i++) {
                if (!usable[i]) { deposit(std::move(res[i])); continue; }
                const float* dl = dlAll.data() + i * (size_t)d.nldpc;
                std::vector<float> dlv;
                int iters = 0;
                bool ok = false, onGpu = false;
                const uint8_t* hp = nullptr;
                if (gpuDone && okAll[i]) { ok = true; onGpu = true; iters = itAll[i]; hp = hardAll.data() + i * (size_t)d.nldpc; }
                else {
                    // the CPU decoder: the chosen path, or the answer for a frame the GPU could not decode (it uses the same code with the right scaling)
                    dlv.assign(dl, dl + d.nldpc);
                    const auto t0 = std::chrono::steady_clock::now();
                    // the high code rates converge in a few iterations, where the 8-bit decoder is about twice as fast; the low rates need as many iterations
                    // again with it, so they keep the float one
                    if (force8.load() || s2RateValue(j0.rate) >= q8MinRate()) ok = code.decodeFast8(dlv, 50, hard, &iters);
                    else if (q8Quick() && (quickScore > 4 || (++quickTick & 31) == 0)) {
                        // low rates: the 8-bit decoder loses up to a dB of sensitivity, so it only gets a few iterations (a clean frame is done by then)
                        // and what it leaves goes to the float decoder. On a marginal signal the attempt is wasted: the score of recent attempts turns
                        // it off, and one frame in 32 tries again
                        ok = code.decodeFast8(dlv, 8, hard, &iters);
                        quickScore = ok ? std::min(16, quickScore + 1) : std::max(0, quickScore - 4);
                        if (!ok) ok = code.decodeFast(dlv, 50, hard, &iters, alpha);
                    }
                    else ok = code.decodeFast(dlv, 50, hard, &iters, alpha);
                    if (!gpuDone) compute.report(false, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                    hp = hard.data();
                }
                finishFrame(jobs[i], d, dl, hp, ok, iters, onGpu, bb, res[i]);
                deposit(std::move(res[i]));
            }
        }
    }

    // ------------------------------------------------------------------ results in order, packets
    // transport stream assembly
    std::vector<uint8_t> carry;
    bool haveCarry = false;
    uint8_t prevCrc = 0;
    bool havePrevCrc = false;
    double pendingSyms = 0;
    int isiFirst = -1;
    std::vector<uint8_t> outPk;

    void deposit(Result&& r) {
        std::lock_guard<std::mutex> lk(resMu);
        const uint64_t s = r.seq;
        results.emplace(s, std::move(r));
        while (true) {
            auto it = results.find(nextOut);
            if (it == results.end()) break;
            Result res = std::move(it->second);
            results.erase(it);
            nextOut++;
            processResult(res);
        }
    }

    void emitPackets(double secs) {
        if (outPk.empty()) return;
        std::function<void(const uint8_t*, size_t, double)> cb;
        { std::lock_guard<std::mutex> lk(cbMu); cb = pktCb; }
        const size_t n = outPk.size() / 188;
        if (cb) cb(outPk.data(), n, secs);
        outPk.clear();
    }

    void processResult(const Result& r) {
        pendingSyms += r.frameSyms;
        if (r.dummy) return;
        {
            std::lock_guard<std::mutex> lk(statMu);
            if (r.dropped) { st.fecBad++; st.dropped++; st.consecutiveBad++; }
            else if (r.ok) {
                st.fecOk++; st.consecutiveBad = 0;
                st.ldpcIterAvg = st.ldpcIterAvg == 0 ? r.iters : 0.9 * st.ldpcIterAvg + 0.1 * r.iters;
                st.ldpcOnGpu = r.gpu; if (r.gpu) st.ldpcGpuFrames++;
                st.berPre = st.berPre < 0 ? r.berPre : 0.9 * st.berPre + 0.1 * r.berPre;
            } else { st.fecBad++; st.consecutiveBad++; st.bchBad++; }
        }
        if (!r.ok) { haveCarry = false; carry.clear(); havePrevCrc = false; return; }
        S2BbHeader h;
        if (!s2ParseBbHeader(r.bb.data(), h)) {
            std::lock_guard<std::mutex> lk(statMu);
            st.bbHeaderBad++;
            haveCarry = false; carry.clear(); havePrevCrc = false;
            return;
        }
        {
            std::lock_guard<std::mutex> lk(statMu);
            st.roSignalled = h.ro; st.sis = h.sis; st.tsGs = h.tsGs;
            { int m1 = 0, m2 = 0; for (int i = 0; i < 8; i++) { m1 = m1 << 1 | (r.bb[(size_t)i] & 1); m2 = m2 << 1 | (r.bb[(size_t)(8 + i)] & 1); } st.matype1 = m1; st.matype2 = m2; }
        }
        if (!h.sis) {
            const int want = o.isiSel_.load() >= 0 ? o.isiSel_.load() : isiFirst;
            if (isiFirst < 0 && o.isiSel_.load() < 0) isiFirst = h.isi;
            const int sel = o.isiSel_.load() >= 0 ? o.isiSel_.load() : isiFirst;
            (void)want;
            { std::lock_guard<std::mutex> lk(statMu); st.isi = sel; }
            if (h.isi != sel) { std::lock_guard<std::mutex> lk(statMu); st.otherIsi++; return; }
        } else { std::lock_guard<std::mutex> lk(statMu); st.isi = -1; }
        if (h.tsGs != 3) { std::lock_guard<std::mutex> lk(statMu); st.gseFrames++; haveCarry = false; carry.clear(); return; }
        const int d = s2Dims(r.mod, r.rate, r.sh).kbch;
        if (h.dfl > d - 80 || h.dfl < 0) { std::lock_guard<std::mutex> lk(statMu); st.bbHeaderBad++; return; }
        // the data field as bytes
        const int nbytes = h.dfl / 8;
        std::vector<uint8_t> by((size_t)nbytes);
        for (int i = 0; i < nbytes; i++) { unsigned c = 0; for (int b = 0; b < 8; b++) c = (c << 1) | r.bb[80 + (size_t)i * 8 + b]; by[(size_t)i] = (uint8_t)c; }
        const int upl = h.upl / 8;
        if (upl < 2 || upl > 400) return;
        size_t start = 0;
        if (h.syncd == 65535) { carry.insert(carry.end(), by.begin(), by.end()); haveCarry = true; return; }
        const size_t sbytes = (size_t)h.syncd / 8;
        if (sbytes > by.size()) { std::lock_guard<std::mutex> lk(statMu); st.bbHeaderBad++; return; }
        if (haveCarry && !carry.empty() && carry.size() + sbytes == (size_t)upl) {
            carry.insert(carry.end(), by.begin(), by.begin() + (std::ptrdiff_t)sbytes);
            emitUp(carry.data(), upl, h);
        } else if (haveCarry && !carry.empty()) { havePrevCrc = false; }
        carry.clear();
        start = sbytes;
        while (start + (size_t)upl <= by.size()) { emitUp(&by[start], upl, h); start += (size_t)upl; }
        carry.assign(by.begin() + (std::ptrdiff_t)start, by.end());
        haveCarry = true;
        emitPackets(pendingSyms / std::max(1.0, o.symbolRate_));
        pendingSyms = 0;
    }

    // one user packet: [CRC-8 of the previous packet][187 bytes][optional ISSY][optional DNP]
    void emitUp(const uint8_t* up, int upl, const S2BbHeader& h) {
        {
            const uint8_t crcOfThis = s2Crc8(up + 1, upl - 1);
            if (havePrevCrc && up[0] != prevCrc) { std::lock_guard<std::mutex> lk(statMu); st.crcErrors++; }
            prevCrc = crcOfThis; havePrevCrc = true;
        }
        if (h.npd) {
            const int dnp = up[upl - 1];
            for (int i = 0; i < dnp; i++) {
                uint8_t n[188]; memset(n, 0xFF, 188); n[0] = 0x47; n[1] = 0x1F; n[2] = 0xFF; n[3] = 0x10;
                outPk.insert(outPk.end(), n, n + 188);
            }
        }
        uint8_t pkt[188];
        pkt[0] = (uint8_t)h.sync;
        memcpy(pkt + 1, up + 1, 187);
        outPk.insert(outPk.end(), pkt, pkt + 188);
        std::lock_guard<std::mutex> lk(statMu);
        st.packets++;
    }
};

// ============================================================================ the public class
S2Rx::S2Rx() : p_(std::make_unique<Impl>(*this)) {}
S2Rx::~S2Rx() { p_->th.stop(); p_->stopWorkers(); }

void S2Rx::setPacketCallback(std::function<void(const uint8_t*, size_t, double)> cb) { std::lock_guard<std::mutex> lk(p_->cbMu); p_->pktCb = std::move(cb); }
void S2Rx::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->cbMu); p_->logCb = std::move(cb); }

void S2Rx::start(const S2HuntResult& h, const cf32* win, size_t n) {
    Impl& I = *p_;
    stop();
    I.startWorkers();
    I.carry.clear(); I.haveCarry = false; I.havePrevCrc = false; I.pendingSyms = 0; I.isiFirst = -1;
    I.seqNext = 0; I.nextOut = 0; I.results.clear();
    inverted_ = h.inverted;
    lost_ = false;
    tracking_ = true;
    // up to four seconds of symbols may wait for the thread at 1 Msym/s, a few frames at the highest rates; a recording waits instead of dropping
    // blocking (a file): a short queue, so the front end cannot run seconds ahead; the symbols queued when sync is lost are thrown away,
    // and how many that were depended on the thread scheduling
    I.th.start([&I](const cf32* z, size_t m) { I.feedBlock(z, m); }, blocking_.load() ? 1u << 16 : 1u << 22, blocking_.load());
    std::vector<cf32> copy(win, win + n);
    I.th.post([this, &I, h, copy = std::move(copy)]() {
        const size_t m = copy.size();
        const S2HuntResult h2 = I.acquire(h, copy.data(), m);
        I.begin(h2);
        I.feedBlock(copy.data() + h2.pos, m - h2.pos);
    });
}

void S2Rx::push(const cf32* z, size_t n) {
    if (!tracking_.load()) return;
    p_->th.push(z, n);
}

void S2Rx::drain() {
    Impl& I = *p_;
    I.th.sync();
    for (int i = 0; i < 2500; i++) {
        { std::lock_guard<std::mutex> lk(I.resMu); if (I.nextOut >= I.seqNext) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void S2Rx::stop() {
    Impl& I = *p_;
    tracking_ = false;                            // symbols still queued are skipped
    I.th.stop();
    if (!I.workers.empty()) {
        // let the frames in the queue finish
        for (int i = 0; i < 200; i++) {
            { std::lock_guard<std::mutex> lk(I.qMu); if (I.queue.empty()) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        I.stopWorkers();
    }
    tracking_ = false;
}

void S2Rx::setComputeMode(int m) { p_->compute.mode = m; }
void S2Rx::setLdpc8(bool on) { p_->force8 = on; }

S2Stats S2Rx::stats() const { std::lock_guard<std::mutex> lk(p_->statMu); return p_->st; }
void S2Rx::cells(std::vector<cf32>& out) const { std::lock_guard<std::mutex> lk(p_->statMu); out = p_->cellSnap; }
double S2Rx::phaseRms() const { std::lock_guard<std::mutex> lk(p_->statMu); return p_->phaseSnap; }
bool S2Rx::carrierLocked() const { std::lock_guard<std::mutex> lk(p_->statMu); return p_->lockSnap; }

} // namespace dvbs
} // namespace dect2
