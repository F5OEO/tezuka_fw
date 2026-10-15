#include "dect2/t2fec.h"
#include "dect2/t2fec_tables.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <cstdint>
#include <vector>

namespace dect2 {

// ============================================================================ dimensions
namespace {
const int kNormalK[6] = {32400, 38880, 43200, 48600, 51840, 54000};
const int kNormalKbch[6] = {32208, 38688, 43040, 48408, 51648, 53840};
const int kNormalQ[6] = {90, 72, 60, 45, 36, 30};
const int kNormalT[6] = {12, 12, 10, 12, 12, 10};
const int kShortK[8] = {7200, 9720, 10800, 11880, 12600, 13320, 5400, 6480};
const int kShortKbch[8] = {7032, 9552, 10632, 11712, 12432, 13152, 5232, 6312};
const int kShortQ[8] = {25, 18, 15, 12, 10, 8, 30, 27};
const char* kRateNames[8] = {"1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "1/3", "2/5"};
} // namespace

const char* rateName(int r) { return (r >= 0 && r < 8) ? kRateNames[r] : "?"; }

FecDims fecDims(const PlpFec& f) {
    FecDims d;
    if (f.mod < 0 || f.mod > 3 || f.rate < 0 || f.rate > 7) return d;
    if (!f.shortFrame) {
        if (f.rate > 5) return d;
        d.nLdpc = 64800; d.kLdpc = kNormalK[f.rate]; d.kBch = kNormalKbch[f.rate]; d.q = kNormalQ[f.rate]; d.t = kNormalT[f.rate];
    } else {
        d.nLdpc = 16200; d.kLdpc = kShortK[f.rate]; d.kBch = kShortKbch[f.rate]; d.q = kShortQ[f.rate]; d.t = 12;
    }
    d.bitsPerCell = 2 * (f.mod + 1);
    d.cellsPerBlock = d.nLdpc / d.bitsPerCell;
    d.ok = true;
    return d;
}

// ============================================================================ LDPC
const LdpcCode& ldpcFor(const PlpFec& f) {
    static std::mutex mu;
    static std::map<int, LdpcCode*> cache;
    std::lock_guard<std::mutex> lk(mu);
    int key = (f.shortFrame ? 100 : 0) + f.rate;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    FecDims d = fecDims(f);
    static const char* nn[6] = {"1_2", "3_5", "2_3", "3_4", "4_5", "5_6"};
    std::string name = f.rate < 6 ? std::string(nn[f.rate]) : (f.rate == 6 ? "1_3" : "2_5");
    name += f.shortFrame ? "S" : "N";
    const LdpcTable* tab = nullptr;
    for (int i = 0; i < kNumLdpcTables; i++) if (name == kLdpcTables[i].name) tab = &kLdpcTables[i];
    std::vector<std::vector<int>> rows;
    if (tab) for (int r = 0; r < tab->rows; r++) {
        const int* row = tab->data + (size_t)r * tab->cols;
        rows.emplace_back(row + 1, row + 1 + row[0]);
    }
    auto* c = new LdpcCode(d.kLdpc, d.nLdpc, rows);
    cache[key] = c;
    return *c;
}

// ============================================================================ BCH
BchCode::BchCode(bool shortFrame, int t) : m_(shortFrame ? 14 : 16), t_(t), deg_(m_ * t) {
    const uint32_t prim = shortFrame ? 0x402B : 0x1002D;
    const int n = (1 << m_) - 1;
    exp_.assign(2 * n + 2, 0);
    log_.assign(n + 1, 0);
    uint32_t x = 1;
    for (int i = 0; i < n; i++) {
        exp_[i] = x;
        log_[x] = i;
        x <<= 1;
        if (x & (1u << m_)) x ^= prim;
    }
    for (int i = n; i < 2 * n + 2; i++) exp_[i] = exp_[i - n];
    const uint8_t* const pn[12] = {k_polyn01, k_polyn02, k_polyn03, k_polyn04, k_polyn05, k_polyn06, k_polyn07, k_polyn08, k_polyn09, k_polyn10, k_polyn11, k_polyn12};
    const uint8_t* const ps[12] = {k_polys01, k_polys02, k_polys03, k_polys04, k_polys05, k_polys06, k_polys07, k_polys08, k_polys09, k_polys10, k_polys11, k_polys12};
    const int len = m_ + 1;
    std::vector<uint8_t> g(1, 1);
    for (int k = 0; k < t; k++) {
        const uint8_t* p = shortFrame ? ps[k] : pn[k];
        std::vector<uint8_t> out(g.size() + len - 1, 0);
        for (size_t i = 0; i < g.size(); i++) if (g[i]) for (int j = 0; j < len; j++) if (p[j]) out[i + j] ^= 1;
        g = out;
    }
    gen_ = g; // degree deg_
}

int BchCode::fieldMul(int a, int b) const {
    if (!a || !b) return 0;
    return (int)exp_[log_[a] + log_[b]];
}

// remainder of the bit string (first bit = highest power) modulo g(x)
void BchCode::remainder(const std::vector<uint8_t>& bits, std::vector<uint8_t>& rem) const {
    const int W = (deg_ + 63) / 64;
    std::vector<uint64_t> g(W, 0), r(W, 0);
    for (int i = 0; i < deg_; i++) if (gen_[i]) g[i >> 6] |= 1ull << (i & 63);
    const int topw = (deg_ - 1) >> 6, topb = (deg_ - 1) & 63;
    const uint64_t lastMask = (deg_ & 63) ? ((1ull << (deg_ & 63)) - 1) : ~0ull;
    auto stepBit = [&](unsigned bit) {
        uint64_t top = (r[topw] >> topb) & 1;
        for (int w = W - 1; w > 0; w--) r[w] = (r[w] << 1) | (r[w - 1] >> 63);
        r[0] = (r[0] << 1) | bit;
        r[W - 1] &= lastMask;
        if (top) for (int w = 0; w < W; w++) r[w] ^= g[w];
    };
    // byte-at-a-time table: tab[t] = (t(x) * x^deg) mod g(x) for the 8 bits t that leave the register
    {
        std::lock_guard<std::mutex> lk(tabMu_);
        if (tab_.empty()) {
            std::vector<uint64_t> save = r;
            tab_.assign((size_t)256 * W, 0);
            for (int t = 0; t < 256; t++) {
                std::fill(r.begin(), r.end(), 0ull);
                for (int b = 7; b >= 0; b--) stepBit((t >> b) & 1);
                for (int z = 0; z < deg_; z++) stepBit(0);
                for (int w = 0; w < W; w++) tab_[(size_t)t * W + w] = r[w];
            }
            r = save;
        }
    }
    size_t k = 0;
    const size_t nb8 = bits.size() & ~(size_t)7;
    for (; k < nb8; k += 8) {
        const unsigned byte = (bits[k] << 7) | (bits[k + 1] << 6) | (bits[k + 2] << 5) | (bits[k + 3] << 4) | (bits[k + 4] << 3) | (bits[k + 5] << 2) | (bits[k + 6] << 1) | bits[k + 7];
        // the 8 highest register bits (deg-1 .. deg-8)
        const int hb = deg_ - 8, hw = hb >> 6, ho = hb & 63;
        uint64_t t = r[hw] >> ho;
        if (ho > 56 && hw + 1 < W) t |= r[hw + 1] << (64 - ho);
        t &= 0xFF;
        for (int w = W - 1; w > 0; w--) r[w] = (r[w] << 8) | (r[w - 1] >> 56);
        r[0] = (r[0] << 8) | byte;
        r[W - 1] &= lastMask;
        const uint64_t* T = &tab_[(size_t)t * W];
        for (int w = 0; w < W; w++) r[w] ^= T[w];
    }
    for (; k < bits.size(); k++) stepBit(bits[k]);
    rem.assign(deg_, 0);
    for (int i = 0; i < deg_; i++) rem[i] = (r[i >> 6] >> (i & 63)) & 1;
}

void BchCode::encode(std::vector<uint8_t>& bits, int k) const {
    std::vector<uint8_t> msg(bits.begin(), bits.begin() + k);
    msg.resize(k + deg_, 0); // message * x^deg
    std::vector<uint8_t> rem;
    remainder(msg, rem);
    bits.resize(k + deg_);
    for (int i = 0; i < deg_; i++) bits[k + i] = rem[deg_ - 1 - i];
}

int BchCode::decode(std::vector<uint8_t>& bits) const {
    const int n = (int)bits.size();
    std::vector<uint8_t> rem;
    remainder(bits, rem);
    bool zero = true;
    for (auto v : rem) if (v) { zero = false; break; }
    if (zero) return 0;
    const int N = (1 << m_) - 1;
    std::vector<int> S(2 * t_ + 1, 0);
    for (int j = 1; j <= 2 * t_; j++) {
        int s = 0;
        for (int i = 0; i < deg_; i++) if (rem[i]) s ^= (int)exp_[((long long)i * j) % N];
        S[j] = s;
    }
    // Berlekamp-Massey
    std::vector<int> C(2 * t_ + 2, 0), B(2 * t_ + 2, 0), T;
    C[0] = B[0] = 1;
    int L = 0, mm = 1, b = 1;
    for (int nn = 0; nn < 2 * t_; nn++) {
        int d = S[nn + 1];
        for (int i = 1; i <= L; i++) d ^= fieldMul(C[i], S[nn + 1 - i]);
        if (d == 0) { mm++; continue; }
        T = C;
        int coef = fieldMul(d, (int)exp_[N - log_[b]]);
        for (int i = 0; i + mm < (int)C.size(); i++) if (B[i]) C[i + mm] ^= fieldMul(coef, B[i]);
        if (2 * L <= nn) { L = nn + 1 - L; B = T; b = d; mm = 1; } else mm++;
    }
    if (L > t_) return -1;
    // Chien search: error at coefficient position p iff C(alpha^-p) == 0
    std::vector<int> pos;
    for (int p = 0; p < n; p++) {
        int acc = 0;
        for (int i = 0; i <= L; i++) if (C[i]) acc ^= (int)exp_[(log_[C[i]] + (long long)(N - (p % N)) * i) % N];
        if (acc == 0) pos.push_back(p);
    }
    if ((int)pos.size() != L) return -1;
    for (int p : pos) bits[n - 1 - p] ^= 1;
    return L;
}

const BchCode& bchFor(const PlpFec& f) {
    static std::mutex mu;
    static std::map<int, BchCode*> cache;
    std::lock_guard<std::mutex> lk(mu);
    FecDims d = fecDims(f);
    int key = (f.shortFrame ? 100 : 0) + d.t;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    auto* c = new BchCode(f.shortFrame, d.t);
    cache[key] = c;
    return *c;
}

// ============================================================================ bit interleaver
const std::vector<int>& bitInterleaverMap(const PlpFec& f) {
    static std::mutex mu;
    static std::map<int, std::vector<int>*> cache;
    std::lock_guard<std::mutex> lk(mu);
    int key = (f.shortFrame ? 1000 : 0) + f.rate * 10 + f.mod;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    FecDims d = fecDims(f);
    const int N = d.nLdpc, nbch = d.kLdpc, q = d.q, M = d.bitsPerCell;
    auto* mapp = new std::vector<int>(N);
    std::vector<int>& out = *mapp;
    // parity interleaver
    std::vector<int> tu(N);
    for (int k = 0; k < nbch; k++) tu[k] = k;
    for (int t = 0; t < q; t++) for (int s = 0; s < 360; s++) tu[nbch + 360 * t + s] = nbch + q * s + t;
    if (f.mod == 0) {
        if (f.shortFrame && (f.rate == 6 || f.rate == 7)) out = tu; // QPSK: only 1/3 and 2/5 are parity-interleaved
        else for (int i = 0; i < N; i++) out[i] = i;
    } else {
        const int* twist; const int* mux; int cols, rowsDiv;
        const bool sh = f.shortFrame;
        if (f.mod == 1) { twist = sh ? k_twist16s : k_twist16n; cols = 8;
            mux = (!sh && f.rate == 1) ? k_mux16_35 : (sh && f.rate == 6) ? k_mux16_13 : (sh && f.rate == 7) ? k_mux16_25 : k_mux16; }
        else if (f.mod == 2) { twist = sh ? k_twist64s : k_twist64n; cols = 12;
            mux = (!sh && f.rate == 1) ? k_mux64_35 : (sh && f.rate == 6) ? k_mux64_13 : (sh && f.rate == 7) ? k_mux64_25 : k_mux64; }
        else if (!sh) { twist = k_twist256n; cols = 16;
            mux = f.rate == 1 ? k_mux256_35 : f.rate == 2 ? k_mux256_23 : k_mux256; }
        else { twist = k_twist256s; cols = 8;
            mux = f.rate == 6 ? k_mux256s_13 : f.rate == 7 ? k_mux256s_25 : k_mux256s; }
        rowsDiv = cols;
        const int rows = N / rowsDiv;
        std::vector<int> tv(N);
        int idx = 0;
        for (int col = 0; col < cols; col++) {
            int offset = twist[col];
            for (int row = 0; row < rows; row++) { tv[offset + rows * col] = tu[idx++]; offset = (offset + 1) % rows; }
        }
        idx = 0;
        for (int j = 0; j < rows; j++) for (int c = 0; c < cols; c++) tu[idx++] = tv[rows * c + j];
        // cells: groups of `cols` bits; label bit order follows the mux table
        int outp = 0;
        for (int g = 0; g < N / cols; g++) {
            for (int e = 0; e < cols; e++) out[outp++] = tu[g * cols + mux[e]];
        }
    }
    cache[key] = mapp;
    (void)M;
    return *mapp;
}

// ============================================================================ constellations
namespace {
struct QamTables {
    std::vector<cf32> plain[4], rot[4];
    QamTables() {
        auto build = [&](int mod, int n, const int8_t (*tab)[2], double norm, double angDeg) {
            plain[mod].resize(n); rot[mod].resize(n);
            double a = angDeg * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
            for (int i = 0; i < n; i++) {
                double re = tab[i][0] / norm, im = tab[i][1] / norm;
                plain[mod][i] = cf32((float)re, (float)im);
                rot[mod][i] = cf32((float)(re * c - im * s), (float)(re * s + im * c));
            }
        };
        build(0, 4, kConstQpsk, std::sqrt(2.0), 29.0);
        build(1, 16, kConstQam16, std::sqrt(10.0), 16.8);
        build(2, 64, kConstQam64, std::sqrt(42.0), 8.6);
        build(3, 256, kConstQam256, std::sqrt(170.0), 3.576334375);
    }
};
const QamTables& qam() { static QamTables t; return t; }
} // namespace

cf32 qamPoint(int mod, bool rotated, unsigned label) { return (rotated ? qam().rot[mod] : qam().plain[mod])[label]; }

void qamMapBlock(const PlpFec& f, const std::vector<uint16_t>& labels, std::vector<cf32>& cells) {
    const int n = (int)labels.size();
    cells.resize(n);
    const auto& tab = f.rotation ? qam().rot[f.mod] : qam().plain[f.mod];
    if (!f.rotation) { for (int j = 0; j < n; j++) cells[j] = tab[labels[j]]; return; }
    for (int j = 0; j < n; j++) cells[j] = cf32(tab[labels[j]].real(), tab[labels[(j + n - 1) % n]].imag());
}

// Exact max-log demapper over the full constellation (reference for tests).
void qamDemapBlockExact(const PlpFec& f, const cf32* cells, const float* n0, int nCells, float* llr) {
    const int M = 2 * (f.mod + 1), P = 1 << M;
    const auto& tab = f.rotation ? qam().rot[f.mod] : qam().plain[f.mod];
    std::vector<float> d(P);
    for (int s = 0; s < nCells; s++) {
        float yI, yQ, nI, nQ;
        if (f.rotation) { yI = cells[s].real(); nI = n0[s]; int s1 = (s + 1) % nCells; yQ = cells[s1].imag(); nQ = n0[s1]; }
        else { yI = cells[s].real(); yQ = cells[s].imag(); nI = nQ = n0[s]; }
        const float wI = 1.f / std::max(1e-9f, 2 * nI), wQ = 1.f / std::max(1e-9f, 2 * nQ);
        for (int p = 0; p < P; p++) {
            float dr = yI - tab[p].real(), di = yQ - tab[p].imag();
            d[p] = dr * dr * wI + di * di * wQ;
        }
        for (int b = 0; b < M; b++) {
            float m0 = 1e30f, m1 = 1e30f;
            const int sh = M - 1 - b;
            for (int p = 0; p < P; p++) { float v = d[p]; if ((p >> sh) & 1) { if (v < m1) m1 = v; } else if (v < m0) m0 = v; }
            llr[(size_t)s * M + b] = m1 - m0;
        }
    }
}

namespace {
// The square QAM labels are separable: some label bits select the in-phase level, the others the quadrature level.
struct AxisMap {
    int bitsPerAxis = 0;
    std::vector<int> reBits, imBits;      // label bit positions (0 = MSB), most significant axis bit first
    std::vector<float> reLevel, imLevel;  // coordinate for each axis index
    cf32 derot;                           // e^{-j phi}
};
const AxisMap& axisMap(int mod) {
    static AxisMap maps[4];
    static std::once_flag once;
    std::call_once(once, [] {
        for (int mod = 0; mod < 4; mod++) {
            AxisMap& A = maps[mod];
            const int M = 2 * (mod + 1), P = 1 << M;
            const auto& pl = qam().plain[mod];
            for (int b = 0; b < M; b++) {
                const int sh = M - 1 - b;
                bool movesRe = false, movesIm = false;
                for (int p = 0; p < P; p++) {
                    int q = p ^ (1 << sh);
                    if (std::fabs(pl[p].real() - pl[q].real()) > 1e-6f) movesRe = true;
                    if (std::fabs(pl[p].imag() - pl[q].imag()) > 1e-6f) movesIm = true;
                }
                (movesRe ? A.reBits : A.imBits).push_back(b);
            }
            A.bitsPerAxis = M / 2;
            const int L = 1 << A.bitsPerAxis;
            A.reLevel.assign(L, 0); A.imLevel.assign(L, 0);
            for (int p = 0; p < P; p++) {
                int ri = 0, ii = 0;
                for (int b : A.reBits) ri = (ri << 1) | ((p >> (M - 1 - b)) & 1);
                for (int b : A.imBits) ii = (ii << 1) | ((p >> (M - 1 - b)) & 1);
                A.reLevel[ri] = pl[p].real();
                A.imLevel[ii] = pl[p].imag();
            }
        }
    });
    return maps[mod];
}
const float kRotDeg[4] = {29.f, 16.8f, 8.6f, 3.576334375f};

// max-log LLRs of one axis: bits of the axis index, most significant first
inline void axisLlr(const std::vector<float>& lev, int nb, float y, float w, float* out) {
    float d[16];
    const int L = 1 << nb;
    for (int i = 0; i < L; i++) { float e = y - lev[i]; d[i] = e * e * w; }
    for (int b = 0; b < nb; b++) {
        const int sh = nb - 1 - b;
        float m0 = 1e30f, m1 = 1e30f;
        for (int i = 0; i < L; i++) { if ((i >> sh) & 1) m1 = std::min(m1, d[i]); else m0 = std::min(m0, d[i]); }
        out[b] = m1 - m0;
    }
}
} // namespace

namespace {
// max-log LLR of axis bit b is w * f_b(y), where f_b(y) = min over levels with bit 1 minus min over levels with bit 0 of (y-level)^2.
// f_b is piecewise linear in y, so a fine grid with linear interpolation is exact up to ~1e-3.
struct AxisLut {
    static constexpr int G = 2048;
    float R = 0, scale = 0;
    int nb = 0;
    std::vector<float> re[4], im[4]; // [bit][G+1]
};
const AxisLut& axisLut(int mod) {
    static AxisLut luts[4];
    static std::once_flag once;
    std::call_once(once, [] {
        for (int m = 0; m < 4; m++) {
            const AxisMap& A = axisMap(m);
            AxisLut& L = luts[m];
            L.nb = A.bitsPerAxis;
            float mx = 0;
            for (float v : A.reLevel) mx = std::max(mx, std::fabs(v));
            for (float v : A.imLevel) mx = std::max(mx, std::fabs(v));
            L.R = mx + 1.0f;
            L.scale = (float)AxisLut::G / (2 * L.R);
            for (int axis = 0; axis < 2; axis++) {
                const auto& lev = axis ? A.imLevel : A.reLevel;
                auto* out = axis ? L.im : L.re;
                const int nlev = 1 << L.nb;
                for (int b = 0; b < L.nb; b++) {
                    out[b].resize(AxisLut::G + 2);
                    const int sh = L.nb - 1 - b;
                    for (int g = 0; g <= AxisLut::G + 1; g++) {
                        float y = -L.R + (float)g / L.scale;
                        float m0 = 1e30f, m1 = 1e30f;
                        for (int i = 0; i < nlev; i++) { float e = y - lev[i]; e *= e; if ((i >> sh) & 1) m1 = std::min(m1, e); else m0 = std::min(m0, e); }
                        out[b][g] = m1 - m0;
                    }
                }
            }
        }
    });
    return luts[mod];
}
} // namespace

void qamDemapBlock(const PlpFec& f, const cf32* cells, const float* n0, int nCells, float* llr) {
    const AxisMap& A = axisMap(f.mod);
    const AxisLut& L = axisLut(f.mod);
    const int M = 2 * (f.mod + 1), nb = A.bitsPerAxis;
    float cr = 1.f, sr = 0.f;
    if (f.rotation) { double a = kRotDeg[f.mod] * M_PI / 180.0; cr = (float)std::cos(a); sr = (float)std::sin(a); }
    float lr[8], li[8];
    for (int s = 0; s < nCells; s++) {
        float yI, yQ, nv;
        if (f.rotation) {
            int s1 = (s + 1) % nCells;
            // derotate the (I from this cell, Q from the next cell) observation back onto the unrotated grid
            float aI = cells[s].real(), aQ = cells[s1].imag();
            yI = aI * cr + aQ * sr;
            yQ = aQ * cr - aI * sr;
            nv = 0.5f * (n0[s] + n0[s1]);
        } else { yI = cells[s].real(); yQ = cells[s].imag(); nv = n0[s]; }
        const float w = 1.f / std::max(1e-9f, 2 * nv);
        {
            auto lut = [&](const std::vector<float>* tab, float y, float* res) {
                float t = (y + L.R) * L.scale;
                t = std::min(std::max(t, 0.f), (float)AxisLut::G - 0.001f);
                const int i = (int)t;
                const float fr = t - (float)i;
                for (int b = 0; b < nb; b++) res[b] = w * (tab[b][i] + fr * (tab[b][i + 1] - tab[b][i]));
            };
            lut(L.re, yI, lr);
            lut(L.im, yQ, li);
        }
        float* o = llr + (size_t)s * M;
        for (int b = 0; b < nb; b++) { o[A.reBits[b]] = lr[b]; o[A.imBits[b]] = li[b]; }
    }
}

// ============================================================================ cell / time interleaver
namespace {
struct CiParams { int cell, degree, xorSize; int logic[6]; };
CiParams ciParams(const PlpFec& f) {
    CiParams p{};
    static const int l11[2] = {0, 3}, l12[2] = {0, 2}, l13[4] = {0, 1, 4, 6}, l14[6] = {0, 1, 4, 5, 9, 11}, l15[4] = {0, 1, 2, 12};
    auto set = [&](int cell, int deg, int x, const int* l) { p.cell = cell; p.degree = deg; p.xorSize = x; for (int i = 0; i < x; i++) p.logic[i] = l[i]; };
    if (!f.shortFrame) {
        switch (f.mod) { case 0: set(32400, 15, 4, l15); break; case 1: set(16200, 14, 6, l14); break; case 2: set(10800, 14, 6, l14); break; default: set(8100, 13, 4, l13); }
    } else {
        switch (f.mod) { case 0: set(8100, 13, 4, l13); break; case 1: set(4050, 12, 2, l12); break; case 2: set(2700, 12, 2, l12); break; default: set(2025, 11, 2, l11); }
    }
    return p;
}

const std::vector<int>& ciPermutation(const PlpFec& f) {
    static std::mutex mu;
    static std::map<int, std::vector<int>*> cache;
    std::lock_guard<std::mutex> lk(mu);
    int key = (f.shortFrame ? 10 : 0) + f.mod;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    CiParams p = ciParams(f);
    auto* perm = new std::vector<int>();
    const int maxStates = 1 << p.degree;
    const int mask = (1 << (p.degree - 1)) - 1; // reference: 0x3fff for degree 15, 0x1fff for 14 ... i.e. degree-1 bits
    int lfsr = 0;
    for (int i = 0; i < maxStates; i++) {
        if (i == 0 || i == 1) lfsr = 0;
        else if (i == 2) lfsr = 1;
        else {
            int result = 0;
            for (int k = 0; k < p.xorSize; k++) result ^= (lfsr >> p.logic[k]) & 1;
            lfsr &= mask;
            lfsr >>= 1;
            lfsr |= result << (p.degree - 2);
        }
        lfsr |= (i % 2) << (p.degree - 1);
        if (lfsr < p.cell) perm->push_back(lfsr);
    }
    cache[key] = perm;
    return *perm;
}

// per-FEC-block cyclic shifts for one time-interleaver block
std::vector<int> ciShifts(const CiParams& p, int nBlocks) {
    std::vector<int> sh(nBlocks);
    int n = 0;
    for (int r = 0; r < nBlocks; r++) {
        int shift = p.cell;
        while (shift >= p.cell) {
            int temp = n;
            shift = 0;
            for (int q = 0; q < p.degree; q++) { shift |= temp & 1; shift <<= 1; temp >>= 1; }
            n++;
        }
        sh[r] = shift;
    }
    return sh;
}

struct TiLayout { int small, big, numBig, numSmall; };
TiLayout tiLayout(int fecBlocks, int tiBlocks) {
    TiLayout t;
    if (tiBlocks == 0) { t.small = t.big = 1; t.numBig = 0; t.numSmall = fecBlocks; }
    else {
        t.small = fecBlocks / tiBlocks;
        t.big = (fecBlocks + tiBlocks - 1) / tiBlocks;
        t.numBig = fecBlocks % tiBlocks;
        t.numSmall = tiBlocks - t.numBig;
    }
    return t;
}

template <class T>
void ciForward(const PlpFec& f, int blocks, int tiBlocks, const T* in, T* out) {
    CiParams p = ciParams(f);
    const auto& perm = ciPermutation(f);
    TiLayout tl = tiLayout(blocks, tiBlocks);
    std::vector<T> ti((size_t)blocks * p.cell);
    int index = 0;
    for (int s = 0; s < tl.numSmall + tl.numBig; s++) {
        int nb = s < tl.numSmall ? tl.small : tl.big;
        auto sh = ciShifts(p, nb);
        for (int r = 0; r < nb; r++) {
            for (int w = 0; w < p.cell; w++) ti[((perm[w] + sh[r]) % p.cell) + index] = *in++;
            index += p.cell;
        }
    }
    if (tiBlocks != 0) {
        int tiIndex = 0;
        for (int s = 0; s < tl.numSmall + tl.numBig; s++) {
            int nb = s < tl.numSmall ? tl.small : tl.big;
            int numCols = 5 * nb, rows = p.cell / 5;
            for (int k = 0; k < rows; k++) for (int w = 0; w < numCols; w++) *out++ = ti[tiIndex + rows * w + k];
            tiIndex += rows * numCols;
        }
    } else {
        for (size_t i = 0; i < ti.size(); i++) *out++ = ti[i];
    }
}

template <class T>
void ciInverse(const PlpFec& f, int blocks, int tiBlocks, const T* in, T* out) {
    CiParams p = ciParams(f);
    const auto& perm = ciPermutation(f);
    TiLayout tl = tiLayout(blocks, tiBlocks);
    std::vector<T> ti((size_t)blocks * p.cell);
    if (tiBlocks != 0) {
        int tiIndex = 0;
        for (int s = 0; s < tl.numSmall + tl.numBig; s++) {
            int nb = s < tl.numSmall ? tl.small : tl.big;
            int numCols = 5 * nb, rows = p.cell / 5;
            for (int k = 0; k < rows; k++) for (int w = 0; w < numCols; w++) ti[tiIndex + rows * w + k] = *in++;
            tiIndex += rows * numCols;
        }
    } else {
        for (size_t i = 0; i < ti.size(); i++) ti[i] = *in++;
    }
    int index = 0;
    for (int s = 0; s < tl.numSmall + tl.numBig; s++) {
        int nb = s < tl.numSmall ? tl.small : tl.big;
        auto sh = ciShifts(p, nb);
        for (int r = 0; r < nb; r++) {
            for (int w = 0; w < p.cell; w++) *out++ = ti[((perm[w] + sh[r]) % p.cell) + index];
            index += p.cell;
        }
    }
}
} // namespace

void cellInterleave(const PlpFec& f, int blocks, int tiBlocks, const std::vector<cf32>& in, std::vector<cf32>& out) {
    out.resize(in.size());
    ciForward(f, blocks, tiBlocks, in.data(), out.data());
}
void cellDeinterleave(const PlpFec& f, int blocks, int tiBlocks, const cf32* in, cf32* out) { ciInverse(f, blocks, tiBlocks, in, out); }
void cellDeinterleaveF(const PlpFec& f, int blocks, int tiBlocks, const float* in, float* out) { ciInverse(f, blocks, tiBlocks, in, out); }

// ============================================================================ baseband framing
const uint8_t* bbRandomiser() {
    static std::vector<uint8_t> r = [] {
        std::vector<uint8_t> v(64800);
        int sr = 0x4A80;
        for (int i = 0; i < 64800; i++) {
            int b = (sr ^ (sr >> 1)) & 1;
            v[i] = b;
            sr >>= 1;
            if (b) sr |= 0x4000;
        }
        return v;
    }();
    return r.data();
}

uint8_t crc8Bits(const uint8_t* bits, int n) {
    // CRC-8, polynomial x^8+x^7+x^6+x^4+x^2+1 (0xD5), MSB first, zero initial value
    unsigned crc = 0;
    for (int i = 0; i < n; i++) {
        unsigned fb = ((crc >> 7) & 1) ^ bits[i];
        crc = (crc << 1) & 0xff;
        if (fb) crc ^= 0xD5;
    }
    return (uint8_t)crc;
}

static unsigned getBits(const uint8_t* b, int pos, int n) { unsigned v = 0; for (int i = 0; i < n; i++) v = (v << 1) | b[pos + i]; return v; }

bool parseBbHeader(const uint8_t* b, BbHeader& h) {
    uint8_t crc = crc8Bits(b, 72);
    unsigned got = getBits(b, 72, 8);
    h.hem = crc == (got ^ 0x01u); // MODE bit is XORed into the CRC LSB
    h.crcOk = crc == got || h.hem;
    h.tsGs = getBits(b, 0, 2); h.sisMis = b[2]; h.ccmAcm = b[3]; h.issyi = b[4]; h.npd = b[5]; h.ro = getBits(b, 6, 2);
    h.isi = getBits(b, 8, 8);
    h.upl = getBits(b, 16, 16); h.dfl = getBits(b, 32, 16); h.sync = getBits(b, 48, 8); h.syncd = getBits(b, 56, 16);
    if (h.hem) { h.issy = (getBits(b, 16, 16) << 8) | getBits(b, 48, 8); h.upl = 1504; h.sync = 0x47; }
    return h.crcOk;
}

void buildBbHeader(const BbHeader& h, uint8_t* b) {
    auto put = [&](int pos, unsigned v, int n) { for (int i = 0; i < n; i++) b[pos + i] = (v >> (n - 1 - i)) & 1; };
    put(0, h.tsGs, 2); b[2] = h.sisMis; b[3] = h.ccmAcm; b[4] = h.issyi; b[5] = h.npd; put(6, h.ro, 2);
    put(8, h.isi, 8); put(16, h.upl, 16); put(32, h.dfl, 16); put(48, h.sync, 8); put(56, h.syncd, 16);
    put(72, crc8Bits(b, 72), 8);
}

} // namespace dect2
