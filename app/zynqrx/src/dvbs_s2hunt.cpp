// DVB-S2 frame synchronisation, see dvbs_s2hunt.h.
#include "dvbs_s2hunt.h"
#include "dect2/dvbs_s2.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace dect2 {
namespace dvbs {

namespace {

constexpr float kSofGate = 0.45f;   // below this SOF score a candidate is not fitted (see sofScore)

// What the receiver knows of every PLHEADER: the 90 symbols of each of the 256 code words (DVB-S2 and S2X, indexed by the PLS code value), and the
// products of neighbouring symbols that do not depend on the carrier phase (the SOF, and the pairs of the PLS code: the second bit of a pair is the
// first one or its opposite, whatever the MODCOD; the 90 degree turn of an S2X header leaves them as they are)
struct Ref {
    cf32 hdr[256][90];
    bool valid[256];
    cf32 rs[25];          // s[k+1] conj(s[k]) of the SOF, +-j
    cf32 rp[32];          // the same for the pairs of the PLS code (pilot flag 0; the flag turns the sign)
    Ref() {
        for (int c = 0; c < 256; c++) {
            int mc; bool sh, pil;
            s2PlsSplit(c, mc, sh, pil);
            valid[c] = s2PlsKind(c) >= 0;
            s2PlHeader(mc, sh, pil, hdr[c]);
        }
        const cf32* h0 = hdr[0];
        for (int k = 0; k < 25; k++) rs[k] = h0[k + 1] * std::conj(h0[k]);
        for (int i = 0; i < 32; i++) rp[i] = h0[27 + 2 * i] * std::conj(h0[26 + 2 * i]);
    }
};
const Ref& ref() { static Ref r; return r; }

int codeLength(int code) { return s2PlsFrameSymbols(code); }

struct HdrFit {
    int code = -1, code2 = -1;
    double g = 0, theta = 0;
    float rho = 0, rho2 = 0;      // normalised correlation of the best word and of the best other word
    float amp = 0, snrDb = -99;
    bool inv = false;
};

// Maximum likelihood fit of a header: every code word, a grid of carrier frequencies (radians per symbol) and both senses of the spectrum.
// `only`: -1 all valid code words, else just that one. A word's score is its best over the grid.
HdrFit fitHeader(const cf32* v, double gCenter, double gSpan, double gStep, bool tryInv, int only = -1, int sense = -1, const bool* allow = nullptr) {
    const Ref& R = ref();
    HdrFit f;
    float bestS[256];
    for (int c = 0; c < 256; c++) bestS[c] = -1.f;
    cf32 bestC[256];
    double bestG[256];
    bool bestI[256];
    cf32 w[90];
    for (int inv = 0; inv < (tryInv ? 2 : 1); inv++) {
        if (sense >= 0 && inv != sense) continue;
        cf32 u[90];
        double P = 0;
        for (int k = 0; k < 90; k++) { u[k] = inv ? std::conj(v[k]) : v[k]; P += std::norm(u[k]); }
        if (P < 1e-9) continue;
        const float norm = (float)(1.0 / std::sqrt(P * 90.0));
        const int steps = (int)std::floor(gSpan / gStep + 0.5);
        for (int j = -steps; j <= steps; j++) {
            const double g = gCenter + j * gStep;
            const cf32 st = std::polar(1.0f, (float)(-g));
            cf32 r(1.f, 0.f);
            for (int k = 0; k < 90; k++) { w[k] = u[k] * r; r *= st; }
            cf32 sSof(0, 0);
            for (int k = 0; k < 26; k++) sSof += w[k] * std::conj(R.hdr[0][k]);
            for (int c = 0; c < 256; c++) {
                if (!R.valid[c] || (only >= 0 && c != only) || (allow && !allow[c])) continue;
                cf32 s = sSof;
                const cf32* h = R.hdr[c];
                for (int k = 26; k < 90; k++) s += w[k] * std::conj(h[k]);
                const float rho = std::abs(s) * norm;
                if (rho > bestS[c]) { bestS[c] = rho; bestC[c] = s; bestG[c] = g; bestI[c] = inv != 0; }
            }
        }
    }
    int b1 = -1;
    for (int c = 0; c < 256; c++) if (bestS[c] >= 0 && (b1 < 0 || bestS[c] > bestS[b1])) b1 = c;
    if (b1 < 0) return f;
    if (allow) {
        // Only some code words were searched, so there is no runner-up from the grid. The ambiguity test needs one (a header that fits another code word
        // about as well is not trusted): score all the other code words at the frequency and sense of the best one, a single grid point instead of all of them.
        cf32 u2[90];
        double P = 0;
        for (int k = 0; k < 90; k++) { u2[k] = bestI[b1] ? std::conj(v[k]) : v[k]; P += std::norm(u2[k]); }
        const float norm = P > 1e-9 ? (float)(1.0 / std::sqrt(P * 90.0)) : 0.f;
        const cf32 st = std::polar(1.0f, (float)(-bestG[b1]));
        cf32 w2[90], r(1.f, 0.f);
        for (int k = 0; k < 90; k++) { w2[k] = u2[k] * r; r *= st; }
        for (int c = 0; c < 256; c++) {
            if (c == b1 || !R.valid[c] || allow[c]) continue;
            cf32 sum(0, 0);
            const cf32* h = R.hdr[c];
            for (int k = 0; k < 90; k++) sum += w2[k] * std::conj(h[k]);
            bestS[c] = std::abs(sum) * norm;
        }
    }
    int b2 = -1;
    for (int c = 0; c < 256; c++) if (c != b1 && bestS[c] >= 0 && (b2 < 0 || bestS[c] > bestS[b2])) b2 = c;
    f.code = b1; f.rho = bestS[b1]; f.g = bestG[b1]; f.inv = bestI[b1];
    f.theta = std::arg(bestC[b1]);
    f.amp = std::abs(bestC[b1]) / 90.f;
    if (b2 >= 0) { f.code2 = b2; f.rho2 = bestS[b2]; }
    // Es/N0 from what the fit leaves: signal energy |S|^2 / 90 against the rest of the 90 symbols
    {
        double P = 0;
        for (int k = 0; k < 90; k++) P += std::norm(v[k]);
        const double sig = (double)std::norm(bestC[b1]) / 90.0, noise = std::max(1e-9, P - sig) * 90.0 / 89.0;
        f.snrDb = (float)(10 * std::log10(std::max(1e-9, (double)f.amp * f.amp * 90.0 / noise)));
    }
    return f;
}

// How well the 26 symbols of the SOF fit, best over a coarse grid of carrier frequencies (and the one the products of neighbouring symbols give) and over the
// senses of the spectrum: 0 to 1. A real header at an Es/N0 of 0 dB scores about 0.7; the best of the same grid on noise stays under 0.45 in nine cases of ten.
// Much cheaper than the fit of all code words, so it decides whether that is worth making.
float sofScore(const cf32* v, int sense) {
    const Ref& R = ref();
    float best = 0.f;
    for (int inv = 0; inv < 2; inv++) {
        if (sense >= 0 && inv != sense) continue;
        cf32 u[26];
        double P = 0;
        for (int k = 0; k < 26; k++) { u[k] = inv ? std::conj(v[k]) : v[k]; P += std::norm(u[k]); }
        if (P < 1e-9) continue;
        const float norm = (float)(1.0 / std::sqrt(P * 26.0));
        cf32 acc(0, 0);
        for (int k = 0; k < 25; k++) acc += u[k + 1] * std::conj(u[k]) * std::conj(R.rs[k]);
        const double g0 = std::arg(acc);
        auto at = [&](double g) {
            const cf32 st = std::polar(1.0f, (float)(-g));
            cf32 r(1.f, 0.f), s(0.f, 0.f);
            for (int k = 0; k < 26; k++) { s += u[k] * r * std::conj(R.hdr[0][k]); r *= st; }
            return std::abs(s) * norm;
        };
        for (int j = -7; j <= 7; j++) best = std::max(best, at(0.02 * j));      // the grid of the header fit, coarser
        for (int j = -1; j <= 1; j++) if (std::fabs(g0) < 1.0) best = std::max(best, at(g0 + 0.015 * j));   // beyond the grid: the frequency the SOF products give (not an alias near pi)
    }
    return best;
}

// Fine frequency: the best of a few steps around the fit, with a parabola through the three best points
double refineG(const cf32* v, const HdrFit& f, double step) {
    double bestG = f.g, rs[9];
    const Ref& R = ref();
    (void)R;
    for (int j = -4; j <= 4; j++) {
        const HdrFit t = fitHeader(v, f.g + j * step, 0.0, step, false, f.code);
        // fitHeader works on the original symbols: inversion is applied by the caller
        rs[j + 4] = t.code >= 0 ? t.rho : 0;
    }
    int bj = 0;
    for (int j = 1; j < 9; j++) if (rs[j] > rs[bj]) bj = j;
    bestG = f.g + (bj - 4) * step;
    if (bj > 0 && bj < 8) {
        const double a = rs[bj - 1], b = rs[bj], c = rs[bj + 1], den = a - 2 * b + c;
        if (den < 0) bestG += 0.5 * (a - c) / den * step;
    }
    return bestG;
}

} // namespace

S2HuntResult s2Hunt(const cf32* z, size_t n, int sense, int onlyCode, const bool* huntCodes) {
    S2HuntResult best;
    bool allowed[256] = {};
    const bool* allow = nullptr;
    if (onlyCode >= 0 && onlyCode < 256) { allowed[onlyCode] = true; allowed[0] = allowed[1] = true; allow = allowed; }   // 0, 1: the dummy frame
    else if (huntCodes) { for (int c = 0; c < 256; c++) allowed[c] = huntCodes[c]; allow = allowed; }
    if (n < 4000) return best;
    const Ref& R = ref();
    // ---- 1. header detection that does not care about the carrier phase or frequency: sum of the products of neighbouring symbols, weighted with
    // the products the header should have (SOF: 25, PLS pairs: 32), normalised by the total size of the products
    // d as two arrays and the weights as signs: the products of the header are +-j, so the correlation is a sum of added and subtracted samples that the
    // compiler turns into vector code (a loop over the positions inside a loop over the header symbols)
    std::vector<float> dr(n, 0.f), di(n, 0.f), ad(n, 0.f);
    for (size_t i = 1; i < n; i++) {
        const float a = z[i].real(), b = z[i].imag(), c = z[i - 1].real(), e = z[i - 1].imag();
        dr[i] = a * c + b * e; di[i] = b * c - a * e;                    // z[i] conj(z[i - 1])
        ad[i] = std::sqrt(dr[i] * dr[i] + di[i] * di[i]);
    }
    std::vector<double> pref(n + 1, 0.0), pref2(n + 2, 0.0);
    for (size_t i = 0; i < n; i++) pref[i + 1] = pref[i] + ad[i];
    for (size_t i = 0; i < n; i++) pref2[i + 2] = pref2[i] + ad[i];
    const size_t np = n - 90;
    std::vector<float> Cr(np, 0.f), Ci(np, 0.f), Pr(np, 0.f), Pi(np, 0.f);
    // in tiles of positions: the 57 passes over the arrays then run on data that stays in the cache (a Cortex-A9 has 32 kB of it, and the arrays are megabytes)
    constexpr size_t kTile = 768;
    for (size_t p0 = 0; p0 < np; p0 += kTile) {
        const size_t p1 = std::min(np, p0 + kTile);
        for (int k = 0; k < 25; k++) {
            // conj(rs_k) is +-j: (+-j)(dr + j di) = (-+di, +-dr)
            const float sgn = std::conj(R.rs[k]).imag();
            const float* a = &dr[(size_t)k + 1]; const float* b = &di[(size_t)k + 1];
            float* cr = Cr.data(); float* ci = Ci.data();
            for (size_t p = p0; p < p1; p++) { cr[p] -= sgn * b[p]; ci[p] += sgn * a[p]; }
        }
        for (int i = 0; i < 32; i++) {
            const float sgn = std::conj(R.rp[i]).imag();
            const float* a = &dr[(size_t)27 + 2 * (size_t)i]; const float* b = &di[(size_t)27 + 2 * (size_t)i];
            float* cr = Pr.data(); float* ci = Pi.data();
            for (size_t p = p0; p < p1; p++) { cr[p] -= sgn * b[p]; ci[p] += sgn * a[p]; }
        }
    }
    std::vector<float> T(np, 0.f);
    double mean = 0;
    for (size_t p = 0; p < np; p++) {
        const float x1 = Cr[p] + Pr[p], y1 = Ci[p] + Pi[p], x2 = Cr[p] - Pr[p], y2 = Ci[p] - Pi[p];
        const double e = (pref[p + 26] - pref[p + 1]) + (pref2[p + 27 + 64] - pref2[p + 27]);
        const float t = std::sqrt(std::max(x1 * x1 + y1 * y1, x2 * x2 + y2 * y2));
        T[p] = e > 1e-6 ? (float)(t / e) : 0.f;
        mean += T[p];
    }
    mean /= (double)np;
    double var = 0;
    for (size_t p = 0; p < np; p++) var += ((double)T[p] - mean) * ((double)T[p] - mean);
    const double sd = std::sqrt(var / (double)np) + 1e-9;      // the peaks are a few hundred symbols per frame: they hardly move mean and sd
    // Candidates: single peaks (strong signals, and streams whose frames change length), and sums of the peaks one frame length apart for every length
    // a frame can have (weak signals: the sum of J peaks stands out of the noise by sqrt(J) times as much)
    struct Cand { float z; size_t p; };
    std::vector<Cand> cand;
    auto addPeaks = [&](const std::vector<float>& z, size_t cnt, float zthr) {
        for (size_t p = 0; p < cnt; p++) {
            if (z[p] < zthr) continue;
            bool peak = true;
            const size_t a = p > 45 ? p - 45 : 0, b = std::min(cnt - 1, p + 45);
            for (size_t q = a; q <= b && peak; q++) if (z[q] > z[p] || (z[q] == z[p] && q < p)) peak = false;
            if (peak) cand.push_back({z[p], p});
        }
    };
    {
        std::vector<float> zs(np);
        for (size_t p = 0; p < np; p++) zs[p] = (float)(((double)T[p] - mean) / sd);
        addPeaks(zs, np, 6.0f);
    }
    {
        static const std::vector<int> allLens = [] {
            std::vector<int> v;
            for (int c = 0; c < 256; c++) if (s2PlsFrameSymbols(c) > 0) v.push_back(s2PlsFrameSymbols(c));
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
            return v;
        }();
        std::vector<int> oneLens;
        if (allow) {
            for (int c = 0; c < 256; c++) if (allow[c] && s2PlsFrameSymbols(c) > 0) oneLens.push_back(s2PlsFrameSymbols(c));
            std::sort(oneLens.begin(), oneLens.end());
            oneLens.erase(std::unique(oneLens.begin(), oneLens.end()), oneLens.end());
        }
        const std::vector<int>& lens = allow ? oneLens : allLens;
        std::vector<float> zf;
        const float fmean = (float)mean, inv3 = (float)(1.0 / (sd * std::sqrt(3.0))), inv4 = (float)(1.0 / (sd * 2.0));
        for (int L : lens) {
            if ((size_t)(3 * L) + 200 > np) continue;
            const size_t cnt = np - (size_t)(2 * L), l = (size_t)L;
            zf.assign(cnt, 0.f);
            // the sum of the peaks one frame apart (3 of them, 4 where the buffer has them): contiguous adds of shifted arrays, which the compiler vectorises
            const float* t0 = T.data(); const float* t1 = t0 + l; const float* t2 = t0 + 2 * l;
            float* o = zf.data();
            for (size_t p = 0; p < cnt; p++) o[p] = (t0[p] + t1[p] + t2[p] - 3.f * fmean) * inv3;
            if (np > 3 * l) {
                const size_t c4 = std::min(cnt, np - 3 * l);          // positions that have a fourth peak
                const float* t3 = t0 + 3 * l;
                for (size_t p = 0; p < c4; p++) o[p] = (t0[p] + t1[p] + t2[p] + t3[p] - 4.f * fmean) * inv4;
            }
            addPeaks(zf, cnt, 4.8f);
        }
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) { return x.z > y.z; });
    if (cand.size() > 40) cand.resize(40);
    best.candidates = (int)cand.size();

    // ---- 2. each candidate: maximum likelihood fit of the header, then follow the frames from it
    struct Chain { std::vector<size_t> pos; std::vector<int> code; HdrFit first; double g = 0; float score = 0; };
    std::vector<Chain> chains;
    for (const Cand& cd : cand) {
        if (cd.p + 90 > n) continue;
        // skip a candidate that lies inside a chain found before
        bool seen = false;
        for (const Chain& ch : chains) for (size_t q : ch.pos) if ((q > cd.p ? q - cd.p : cd.p - q) < 12) seen = true;
        if (seen) continue;
        if (sofScore(z + cd.p, sense) < kSofGate) continue;       // not a header: spare the fit of all the code words
        HdrFit f = fitHeader(z + cd.p, 0.0, 0.14, 0.012, true, -1, sense, allow);
        // The grid covers +-0.14 radians per symbol (2 % of the symbol rate) around the carrier the spectrum gave. An echo or the tilt of a long cable
        // bends the spectrum and moves that further off: the products of neighbouring SOF symbols give the frequency without that limit (in either
        // sense of the spectrum), and when the grid around zero finds no header the grid is searched around that frequency (only then: a second
        // search would also find more false headers in noise)
        if (f.code < 0 || f.rho < 0.36f || f.rho2 > 0.88f * f.rho) {
            cf32 a(0, 0), b(0, 0);
            for (int k = 0; k < 25; k++) {
                const cf32 d = z[cd.p + (size_t)k + 1] * std::conj(z[cd.p + (size_t)k]);
                a += d * std::conj(R.rs[k]);
                b += std::conj(d) * std::conj(R.rs[k]);
            }
            for (int inv = 0; inv < 2; inv++) {
                if (sense >= 0 && inv != sense) continue;
                const double g0 = std::arg(inv ? b : a);
                // |g0| near pi is an alias, not a carrier: the header is nearly its own mirror image under conjugation plus a sign change on every other
                // symbol, so the wrong sense of the spectrum can fit about as well at pi - g (seen with a carrier at +0.17 rad/symbol, just outside the grid
                // around zero: two false acquisitions, each a full scan, before the real one). After the front end a carrier is within a fraction of that.
                if (std::fabs(g0) < 0.12 || std::fabs(g0) > 1.0) continue;
                cf32 u[90];
                for (int k = 0; k < 90; k++) u[k] = inv ? std::conj(z[cd.p + (size_t)k]) : z[cd.p + (size_t)k];
                HdrFit t = fitHeader(u, g0, 0.14, 0.012, false, -1, -1, allow);
                t.inv = inv != 0;
                if (t.code >= 0 && t.rho > f.rho) f = t;
            }
        }
            if (f.code < 0 || f.rho < 0.36f || f.rho2 > 0.88f * f.rho) continue;
        // fine frequency: the symbols as the fit saw them (conjugated when the spectrum is inverted)
        std::vector<cf32> v(90);
        for (int k = 0; k < 90; k++) v[(size_t)k] = f.inv ? std::conj(z[cd.p + (size_t)k]) : z[cd.p + (size_t)k];
        const double gf = refineG(v.data(), f, 0.003);
        HdrFit f2 = fitHeader(v.data(), gf, 0.0, 0.003, false, f.code);
        if (f2.code >= 0) { f.g = gf; f.theta = f2.theta; f.amp = f2.amp; f.rho = std::max(f.rho, f2.rho); f.snrDb = f2.snrDb; }
        Chain ch;
        ch.first = f; ch.g = f.g;
        ch.pos.push_back(cd.p); ch.code.push_back(f.code);
        // follow: the next header is one frame length on; any code word is accepted there (the MODCOD may change), and the frequency is searched
        // a little around the one found (the symbol clock moves the position by a symbol or two over a frame)
        size_t pos = cd.p;
        int code = f.code;
        int misses = 0;
        for (int hop = 0; hop < 8; hop++) {
            const int L = codeLength(code);
            if (L < 90) break;
            size_t nx = pos + (size_t)L;
            if (nx + 90 + 4 > n) break;
            float bestRho = 0; size_t bestQ = 0; HdrFit bf;
            std::vector<cf32> u(90);
            auto fitAt = [&](size_t q, int only) {
                for (int k = 0; k < 90; k++) u[(size_t)k] = f.inv ? std::conj(z[q + (size_t)k]) : z[q + (size_t)k];
                return fitHeader(u.data(), ch.g, 0.012, 0.004, false, only);
            };
            // the position: the same code word as the last header is tried at each (one word, not 256), then the full fit is made at the best one;
            // when the MODCOD changed (or the header is damaged) every word is tried at every position
            for (long dq = -3; dq <= 3; dq++) {
                const long qq = (long)nx + dq;
                if (qq < 0 || (size_t)qq + 90 > n) continue;
                const HdrFit t = fitAt((size_t)qq, code);
                if (t.code >= 0 && t.rho > bestRho) { bestRho = t.rho; bestQ = (size_t)qq; }
            }
            if (bestRho >= 0.34f) {
                bf = fitAt(bestQ, -1);
                bestRho = bf.code >= 0 ? bf.rho : 0;
            } else {
                bestRho = 0;
                for (long dq = -3; dq <= 3; dq++) {
                    const long qq = (long)nx + dq;
                    if (qq < 0 || (size_t)qq + 90 > n) continue;
                    const HdrFit t = fitAt((size_t)qq, -1);
                    if (t.code >= 0 && t.rho > bestRho) { bestRho = t.rho; bestQ = (size_t)qq; bf = t; }
                }
            }
            if (bestRho >= 0.34f && bf.rho2 < 0.9f * bf.rho) {
                ch.pos.push_back(bestQ); ch.code.push_back(bf.code);
                pos = bestQ; code = bf.code; misses = 0;
            } else {
                // a frame without a readable header: keep the length and try the next one (once)
                if (++misses > 1) break;
                pos = nx;
            }
        }
        ch.score = (float)ch.pos.size() + f.rho;
            chains.push_back(std::move(ch));
        if (chains.back().pos.size() >= 4) break;       // plenty: no need to look at the weaker candidates
    }
    // ---- 3. the best chain: a verified header and at least one more a frame later
    int bi = -1;
    for (size_t i = 0; i < chains.size(); i++) {
        const Chain& c = chains[i];
        if (c.pos.size() < 2 && !(c.first.rho >= 0.8f && c.first.snrDb > 6.f)) continue;
        if (bi < 0 || c.score > chains[(size_t)bi].score) bi = (int)i;
    }
    if (bi < 0) return best;
    const Chain& c = chains[(size_t)bi];
    // the start: the first header that is a data frame (not a dummy), with another header exactly one frame later
    size_t si = 0;
    for (size_t i = 0; i < c.pos.size(); i++) if (s2PlsKind(c.code[i]) == 0) { si = i; break; }
    best.ok = true;
    best.firstPos = c.pos[si];
    best.pos = c.pos[si];
    best.lastPos = c.pos.back();
    best.headers = (int)(c.pos.size() - si);
    const int code = c.code[si];
    s2PlsSplit(code, best.modcod, best.shortFrame, best.pilots);
    best.frameLen = codeLength(code);
    best.inverted = c.first.inv;
    best.phi = c.g; best.theta = c.first.theta;
    best.plsScore = c.first.rho; best.plsSecond = c.first.rho2;
    best.sofScore = c.first.amp; best.snrDb = c.first.snrDb;
    if (si > 0 || c.pos[0] != best.firstPos) {
        // theta belongs to the first header found; re-fit the one chosen
        std::vector<cf32> v(90);
        for (int k = 0; k < 90; k++) v[(size_t)k] = best.inverted ? std::conj(z[best.firstPos + (size_t)k]) : z[best.firstPos + (size_t)k];
        const HdrFit t = fitHeader(v.data(), best.phi, 0.0, 0.01, false, code);
        if (t.code >= 0) { best.theta = t.theta; best.sofScore = t.amp; best.snrDb = t.snrDb; best.plsScore = t.rho; }
    }
    return best;
}

} // namespace dvbs
} // namespace dect2
