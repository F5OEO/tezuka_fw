// DVB-S2 and DVB-S2X building blocks, see dvbs_s2.h. Clause numbers refer to ETSI EN 302 307 V1.3.1 unless they name EN 302 307-2 (V1.2.1).
#include "dect2/dvbs_s2.h"
#include "dvbs_ldpc_tables.h"
#include "dvbs_s2x.h"
#include "dvbs_s2x_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace dect2 {
namespace dvbs {

namespace {
constexpr double kPi = 3.14159265358979323846;
const char* kRateNames[kS2Rates] = {"1/4", "1/3", "2/5", "1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "8/9", "9/10"};
const double kRateValues[kS2Rates] = {1.0 / 4, 1.0 / 3, 2.0 / 5, 1.0 / 2, 3.0 / 5, 2.0 / 3, 3.0 / 4, 4.0 / 5, 5.0 / 6, 8.0 / 9, 9.0 / 10};
// table 5a (normal) and 5b (short): kldpc, kbch, t, and tables 7a / 7b: q
const int kNK[kS2Rates] = {16200, 21600, 25920, 32400, 38880, 43200, 48600, 51840, 54000, 57600, 58320};
const int kNKbch[kS2Rates] = {16008, 21408, 25728, 32208, 38688, 43040, 48408, 51648, 53840, 57472, 58192};
const int kNT[kS2Rates] = {12, 12, 12, 12, 12, 10, 12, 12, 10, 8, 8};
const int kNQ[kS2Rates] = {135, 120, 108, 90, 72, 60, 45, 36, 30, 20, 18};
const int kSK[kS2Rates] = {3240, 5400, 6480, 7200, 9720, 10800, 11880, 12600, 13320, 14400, 0};
const int kSKbch[kS2Rates] = {3072, 5232, 6312, 7032, 9552, 10632, 11712, 12432, 13152, 14232, 0};
const int kSQ[kS2Rates] = {36, 30, 27, 25, 18, 15, 12, 10, 8, 5, 0};

// table 12: MODCOD number -> modulation, rate index
struct McEntry { int mod, rate; };
const McEntry kModcod[29] = {
    {-1, -1},                                                                                                                  // 0: dummy PLFRAME
    {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {0, 8}, {0, 9}, {0, 10},                                   // QPSK 1/4 ... 9/10
    {1, 4}, {1, 5}, {1, 6}, {1, 8}, {1, 9}, {1, 10},                                                                           // 8PSK 3/5 2/3 3/4 5/6 8/9 9/10
    {2, 5}, {2, 6}, {2, 7}, {2, 8}, {2, 9}, {2, 10},                                                                           // 16APSK 2/3 ... 9/10
    {3, 6}, {3, 7}, {3, 8}, {3, 9}, {3, 10},                                                                                   // 32APSK 3/4 ... 9/10
};

// The LDPC code of an S2X MODCOD: its annex B / C table (nullptr: a DVB-S2 code)
const S2xLdpcTable* s2xLdpcTable(const S2xModcod& m) {
    if (!m.table) return nullptr;
    for (int i = 0; i < kS2xLdpcTableCount; i++) if (strcmp(kS2xLdpcTables[i].id, m.table) == 0) return &kS2xLdpcTables[i];
    return nullptr;
}

// Code dimensions of a rate index and frame size (S2: tables 5a and 5b; S2X: EN 302 307-2 tables 4 and 6, or the S2 code it names). False when
// there is no such code.
bool codeDims(int rate, bool shortFrame, int& n, int& k, int& kbch, int& t) {
    if (const S2xModcod* m = s2xModcod(rate)) {
        if (m->shortFrame != shortFrame) return false;
        if (!m->table) return codeDims(m->s2rate, shortFrame, n, k, kbch, t);
        const S2xLdpcTable* lt = s2xLdpcTable(*m);
        if (!lt) return false;
        n = lt->nldpc; k = lt->kldpc; t = 12;
        kbch = k - (shortFrame ? 168 : 192);            // BCH with t = 12: 192 parity bits (normal), 168 (short), tables 4 and 6
        return true;
    }
    if (rate < 0 || rate >= kS2Rates) return false;
    if (shortFrame) {
        if (kSK[rate] == 0) return false;
        n = 16200; k = kSK[rate]; kbch = kSKbch[rate]; t = 12;
    } else {
        n = 64800; k = kNK[rate]; kbch = kNKbch[rate]; t = kNT[rate];
    }
    return true;
}

// bits of the XFECFRAME: 128APSK pads the code word with 6 bits and the interleaver output with 84 (EN 302 307-2 clauses 5.3.2.2 and 5.3.3), which
// makes 9 270 symbols, 103 slots (table 16)
int xfecBits(int mod, int nldpc) { return mod == k128apsk ? nldpc + 6 + 84 : nldpc; }
} // namespace

const char* s2RateName(int rate) {
    if (const S2xModcod* m = s2xModcod(rate)) return m->rate;
    return rate >= 0 && rate < kS2Rates ? kRateNames[rate] : "?";
}
double s2RateValue(int rate) {
    if (const S2xModcod* m = s2xModcod(rate)) {
        int n, k, kb, t;
        return codeDims(rate, m->shortFrame, n, k, kb, t) ? (double)k / n : 0;
    }
    return rate >= 0 && rate < kS2Rates ? kRateValues[rate] : 0;
}
const char* s2ModName(int mod) {
    static const char* n[kS2Mods] = {"QPSK", "8PSK", "16APSK", "32APSK", "64APSK", "128APSK", "256APSK"};
    return mod >= 0 && mod < kS2Mods ? n[mod] : "?";
}
const char* s2ModNameFor(int mod, int rate) {
    const S2xModcod* m = s2xModcod(rate);
    return m && m->mod == mod ? m->modName : s2ModName(mod);
}
int s2xRateCount(int mod, bool shortFrame) {
    int c = 0;
    for (int i = 0; i < kS2xModcods; i++) { const S2xModcod* m = s2xModcod(kS2Rates + i); if (m->mod == mod && m->shortFrame == shortFrame) c++; }
    return c;
}
int s2xRate(int mod, bool shortFrame, int nth) {
    for (int i = 0; i < kS2xModcods; i++) {
        const S2xModcod* m = s2xModcod(kS2Rates + i);
        if (m->mod == mod && m->shortFrame == shortFrame && nth-- == 0) return kS2Rates + i;
    }
    return -1;
}
const char* s2xCodeName(int rate) { const S2xModcod* m = s2xModcod(rate); return m ? m->code : ""; }
int s2RateFromName(int mod, const char* name, bool shortFrame) {
    if (!name) return -1;
    for (int r = 0; r < kS2Rates + kS2xModcods; r++)
        if (strcmp(s2RateName(r), name) == 0 && s2Dims(mod, r, shortFrame).ok) return r;
    return -1;
}

double s2QefEsN0(int mod, int rate, bool shortFrame) {
    // table 13 of EN 302 307-1, normal FECFRAME; 0 = not a MODCOD
    static const double q[4][kS2Rates] = {
        {-2.35, -1.24, -0.30, 1.00, 2.23, 3.10, 4.03, 4.68, 5.18, 6.20, 6.42},
        {0, 0, 0, 0, 5.50, 6.62, 7.91, 0, 9.35, 10.69, 10.98},
        {0, 0, 0, 0, 0, 8.97, 10.21, 11.03, 11.61, 12.89, 13.13},
        {0, 0, 0, 0, 0, 0, 12.73, 13.64, 14.28, 15.69, 16.05}};
    if (const S2xModcod* m = s2xModcod(rate)) return m->mod == mod && m->shortFrame == shortFrame ? m->esn0 : 99;
    if (mod < 0 || mod > 3 || rate < 0 || rate >= kS2Rates || s2Modcod(mod, rate) < 0) return 99;
    if (shortFrame && rate == 10) return 99;
    return q[mod][rate] + (shortFrame ? 0.25 : 0.0);
}

int s2Modcod(int mod, int rate) {
    if (const S2xModcod* m = s2xModcod(rate)) return m->mod == mod ? m->pls >> 1 : -1;
    for (int i = 1; i < 29; i++) if (kModcod[i].mod == mod && kModcod[i].rate == rate) return i;
    return -1;
}
bool s2ModcodSplit(int modcod, int& mod, int& rate) {
    if (s2ModcodIsS2x(modcod)) {
        const S2xModcod* m = s2xModcodByPls(modcod << 1);
        if (!m) return false;
        mod = m->mod; rate = kS2Rates + (int)(m - s2xModcod(kS2Rates));
        return true;
    }
    if (modcod < 1 || modcod > 28) return false;
    mod = kModcod[modcod].mod; rate = kModcod[modcod].rate;
    return true;
}

S2Dims s2Dims(int mod, int rate, bool shortFrame) {
    S2Dims d;
    if (mod < 0 || mod >= kS2Mods || s2Modcod(mod, rate) < 0) return d;
    if (!codeDims(rate, shortFrame, d.nldpc, d.kldpc, d.kbch, d.t)) return d;
    d.q = (d.nldpc - d.kldpc) / 360;
    d.bitsPerSym = mod + 2;
    d.xfecSymbols = xfecBits(mod, d.nldpc) / d.bitsPerSym;
    d.slots = d.xfecSymbols / 90;
    d.ok = true;
    return d;
}

int s2FrameSymbols(int mod, bool shortFrame, bool pilots) {
    const int bps = mod + 2;
    const int xs = xfecBits(mod, shortFrame ? 16200 : 64800) / bps;
    const int slots = xs / 90;
    return 90 * (slots + 1) + (pilots ? 36 * ((slots - 1) / 16) : 0);
}

// ============================================================================ codes
const LdpcCode& s2Ldpc(int rate, bool shortFrame) {
    static std::mutex mu;
    static std::map<int, LdpcCode*> cache;
    std::lock_guard<std::mutex> lk(mu);
    // S2X MODCODs that use a DVB-S2 code share it; the others are keyed by their annex table
    const S2xModcod* m = s2xModcod(rate);
    if (m && !m->table) { rate = m->s2rate; m = nullptr; }
    const S2xLdpcTable* xt = m ? s2xLdpcTable(*m) : nullptr;
    const int key = xt ? 1000 + (int)(xt - kS2xLdpcTables) : (shortFrame ? 100 : 0) + rate;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    const uint16_t* p = xt ? xt->data : kS2LdpcTables[shortFrame ? 1 : 0][rate].data;
    const int nrows = xt ? xt->rows : kS2LdpcTables[shortFrame ? 1 : 0][rate].rows;
    std::vector<std::vector<int>> rows;
    for (int r = 0; r < nrows; r++) {
        const int n = *p++;
        rows.emplace_back(p, p + n);
        p += n;
    }
    const int k = xt ? xt->kldpc : shortFrame ? kSK[rate] : kNK[rate];
    auto* c = new LdpcCode(k, xt ? xt->nldpc : shortFrame ? 16200 : 64800, rows);
    cache[key] = c;
    return *c;
}

float s2LdpcAlpha(int rate) {
    // measured with tools of the kind of tests/test_dvbs_s2fec.cpp: for every MODCOD the factor with the fewest failed frames and iterations at
    // 0.5 dB above the Es/N0 of table 13 (the low rates want a weak correction, 3/5 is the touchy one)
    static const float a[kS2Rates] = {0.92f, 0.90f, 0.90f, 0.88f, 0.88f, 0.85f, 0.85f, 0.85f, 0.85f, 0.85f, 0.85f};
    if (const S2xModcod* m = s2xModcod(rate)) {
        if (!m->table) return a[m->s2rate];
        // S2X codes: the steps of the S2 ones by code rate
        const double r = s2RateValue(rate);
        return r < 0.30 ? 0.92f : r < 0.45 ? 0.90f : r < 0.62 ? 0.88f : 0.85f;
    }
    return rate >= 0 && rate < kS2Rates ? a[rate] : 0.85f;
}

const BchCode& s2Bch(int rate, bool shortFrame) {
    static std::mutex mu;
    static std::map<int, BchCode*> cache;
    std::lock_guard<std::mutex> lk(mu);
    int n = 0, k = 0, kb = 0, t = 12;
    codeDims(rate, shortFrame, n, k, kb, t);
    const int key = (shortFrame ? 100 : 0) + t;
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    auto* c = new BchCode(shortFrame, t);
    cache[key] = c;
    return *c;
}

void s2EncodeFec(const std::vector<uint8_t>& bb, int rate, bool shortFrame, std::vector<uint8_t>& fec) {
    int n = 0, k = 0, kbch = 0, t = 0;
    codeDims(rate, shortFrame, n, k, kbch, t);
    fec.assign(bb.begin(), bb.begin() + kbch);
    s2Bch(rate, shortFrame).encode(fec, kbch);
    s2Ldpc(rate, shortFrame).encode(fec);
}

int s2BchDecode(const uint8_t* hard, int rate, bool shortFrame, std::vector<uint8_t>& bb) {
    int n = 0, kldpc = 0, kbch = 0, t = 0;
    codeDims(rate, shortFrame, n, kldpc, kbch, t);
    std::vector<uint8_t> w(hard, hard + kldpc);
    const int r = s2Bch(rate, shortFrame).decode(w);
    bb.assign(w.begin(), w.begin() + kbch);
    return r;
}

// ============================================================================ bit interleaver
void s2InterleavePattern(int mod, int rate, int cols[8]) {
    const int m = mod + 2;
    for (int j = 0; j < m; j++) cols[j] = j;
    if (const S2xModcod* x = s2xModcod(rate)) {
        // EN 302 307-2 tables 9a and 9b: digit j of the pattern is the column read out j-th
        if ((int)strlen(x->il) == m) for (int j = 0; j < m; j++) cols[j] = x->il[j] - '0';
        return;
    }
    // 8PSK with rate 3/5: the MSB of the BBHEADER is read out third (figure 8): columns are read in the order 3, 2, 1
    if (mod == k8psk && rate == 4) { cols[0] = 2; cols[1] = 1; cols[2] = 0; }
}

void s2BitInterleave(const std::vector<uint8_t>& in, int mod, int rate, std::vector<uint8_t>& out) {
    const int m = mod + 2, n = (int)in.size();
    if (mod == kQpsk) { out = in; return; }
    // 128APSK: 6 zeros after the code word go through the interleaver, 84 ones follow it (EN 302 307-2 clauses 5.3.2.2 and 5.3.3)
    const int nw = mod == k128apsk ? n + 6 : n;
    out.assign((size_t)xfecBits(mod, n), 1);
    int cols[8];
    s2InterleavePattern(mod, rate, cols);
    const int R = nw / m;
    for (int row = 0; row < R; row++)
        for (int j = 0; j < m; j++) {
            const int s = cols[j] * R + row;
            out[(size_t)row * m + j] = s < n ? in[(size_t)s] : 0;
        }
}

void s2BitDeinterleaveLlr(const float* in, int n, int mod, int rate, float* out) {
    const int m = mod + 2;
    if (mod == kQpsk) { memcpy(out, in, sizeof(float) * n); return; }
    const int nw = mod == k128apsk ? n + 6 : n;
    int cols[8];
    s2InterleavePattern(mod, rate, cols);
    const int R = nw / m;
    for (int row = 0; row < R; row++)
        for (int j = 0; j < m; j++) {
            const int d = cols[j] * R + row;
            if (d < n) out[(size_t)d] = in[(size_t)row * m + j];
        }
}

// ============================================================================ constellations
namespace {
// ring ratios, tables 9 and 10
double gamma16(int rate) {
    switch (rate) { case 5: return 3.15; case 6: return 2.85; case 7: return 2.75; case 8: return 2.70; case 9: return 2.60; default: return 2.57; }
}
void gamma32(int rate, double& g1, double& g2) {
    switch (rate) {
    case 6: g1 = 2.84; g2 = 5.27; break;
    case 7: g1 = 2.72; g2 = 4.87; break;
    case 8: g1 = 2.64; g2 = 4.64; break;
    case 9: g1 = 2.54; g2 = 4.33; break;
    default: g1 = 2.53; g2 = 4.30; break;
    }
}
cf32 polar(double r, double deg) { const double a = deg * kPi / 180.0; return cf32((float)(r * std::cos(a)), (float)(r * std::sin(a))); }

// Label, ring (0 = innermost) and angle in degrees of every point, read from the constellation figures of the specification
// (figures 11 and 12: the position of each marker in the drawing, matched to its label).
struct ApskPoint { const char* label; int ring; double deg; };
const ApskPoint k16Points[16] = {
    {"1100", 0, 45}, {"1110", 0, 135}, {"1111", 0, 225}, {"1101", 0, 315},
    {"0100", 1, 15}, {"0000", 1, 45}, {"1000", 1, 75}, {"1010", 1, 105}, {"0010", 1, 135}, {"0110", 1, 165},
    {"0111", 1, 195}, {"0011", 1, 225}, {"1011", 1, 255}, {"1001", 1, 285}, {"0001", 1, 315}, {"0101", 1, 345},
};
const ApskPoint k32Points[32] = {
    {"10001", 0, 45}, {"10101", 0, 135}, {"10111", 0, 225}, {"10011", 0, 315},
    {"10000", 1, 15}, {"00000", 1, 45}, {"00001", 1, 75}, {"00101", 1, 105}, {"00100", 1, 135}, {"10100", 1, 165},
    {"10110", 1, 195}, {"00110", 1, 225}, {"00111", 1, 255}, {"00011", 1, 285}, {"00010", 1, 315}, {"10010", 1, 345},
    {"11000", 2, 0}, {"01000", 2, 22.5}, {"11001", 2, 45}, {"01001", 2, 67.5}, {"01101", 2, 90}, {"11101", 2, 112.5},
    {"01100", 2, 135}, {"11100", 2, 157.5}, {"11110", 2, 180}, {"01110", 2, 202.5}, {"11111", 2, 225}, {"01111", 2, 247.5},
    {"01011", 2, 270}, {"11011", 2, 292.5}, {"01010", 2, 315}, {"11010", 2, 337.5},
};

void buildConstellation(int mod, int rate, std::vector<cf32>& pts) {
    if (const S2xModcod* x = s2xModcod(rate)) {
        if (x->shape == kSh412) {
            // 4+12APSK: the DVB-S2 16APSK labels with the ring ratio of EN 302 307-2 tables 11a and 11b
            const double g = x->g[0], r1 = 2.0 / std::sqrt(1 + 3 * g * g), rr[2] = {r1, g * r1};
            pts.assign(16, cf32());
            for (const ApskPoint& p : k16Points) pts[strtol(p.label, nullptr, 2)] = polar(rr[p.ring], p.deg);
            return;
        }
        if (x->shape != kShS2) { s2xBuildConstellation(*x, pts); return; }
    }
    if (mod == kQpsk) {
        pts.assign(4, cf32());
        for (int l = 0; l < 4; l++) pts[l] = cf32((1 - 2 * (l >> 1)) * 0.70710678f, (1 - 2 * (l & 1)) * 0.70710678f);   // figure 9: I = MSB, 0 -> positive
    } else if (mod == k8psk) {
        // figure 10 (angle of each label in degrees)
        static const int ang[8] = {45, 0, 180, 225, 90, 315, 135, 270};
        pts.assign(8, cf32());
        for (int l = 0; l < 8; l++) pts[l] = polar(1.0, ang[l]);
    } else if (mod == k16apsk) {
        const double g = gamma16(rate);
        const double r1 = 2.0 / std::sqrt(1 + 3 * g * g), rr[2] = {r1, g * r1};    // [R1]^2 + 3 [R2]^2 = 4
        pts.assign(16, cf32());
        for (const ApskPoint& p : k16Points) pts[strtol(p.label, nullptr, 2)] = polar(rr[p.ring], p.deg);
    } else {
        double g1, g2;
        gamma32(rate, g1, g2);
        const double r1 = std::sqrt(8.0 / (1 + 3 * g1 * g1 + 4 * g2 * g2)), rr[3] = {r1, g1 * r1, g2 * r1};   // [R1]^2 + 3 [R2]^2 + 4 [R3]^2 = 8
        pts.assign(32, cf32());
        for (const ApskPoint& p : k32Points) pts[strtol(p.label, nullptr, 2)] = polar(rr[p.ring], p.deg);
    }
}
} // namespace

int s2ConstellationSize(int mod) { return 1 << (mod + 2); }

const cf32* s2Constellation(int mod, int rate) {
    static std::mutex mu;
    static std::map<int, std::vector<cf32>*> cache;
    std::lock_guard<std::mutex> lk(mu);
    const int key = mod < 2 && !s2IsS2x(rate) ? mod * 1000 : mod * 1000 + rate;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second->data();
    auto* v = new std::vector<cf32>();
    buildConstellation(mod, rate, *v);
    cache[key] = v;
    return v->data();
}

void s2MapBits(const uint8_t* bits, int nbits, int mod, int rate, cf32* out) {
    const int m = mod + 2;
    const cf32* c = s2Constellation(mod, rate);
    for (int i = 0; i < nbits / m; i++) {
        unsigned l = 0;
        for (int j = 0; j < m; j++) l = (l << 1) | (bits[(size_t)i * m + j] & 1);
        out[i] = c[l];
    }
}

void s2Demap(const cf32* sym, int n, int mod, int rate, float sigma2, float* llr) {
    const int m = mod + 2;
    const float w = 1.f / (2.f * std::max(sigma2, 1e-6f));
    if (mod == kQpsk) {
        const float k = 4.f * 0.70710678f * w;   // (|y-s1|^2 - |y-s0|^2) / (2 sigma^2) = 4 a y w with a = 1/sqrt(2)
        for (int i = 0; i < n; i++) { llr[2 * i] = k * sym[i].real(); llr[2 * i + 1] = k * sym[i].imag(); }
        return;
    }
    const cf32* c = s2Constellation(mod, rate);
    const int P = 1 << m;
    float cr[256], ci[256];
    for (int p = 0; p < P; p++) { cr[p] = c[p].real(); ci[p] = c[p].imag(); }
    for (int i = 0; i < n; i++) {
        float d[256];
        const float yr = sym[i].real(), yi = sym[i].imag();
        for (int p = 0; p < P; p++) { const float a = yr - cr[p], b = yi - ci[p]; d[p] = a * a + b * b; }
        for (int b = 0; b < m; b++) {
            // the points whose bit b is 0 or 1 come in runs of 2^sh labels
            const int sh = m - 1 - b, run = 1 << sh;
            float m0 = 1e30f, m1 = 1e30f;
            for (int base = 0; base < P; base += 2 * run) {
                for (int p = base; p < base + run; p++) m0 = std::min(m0, d[p]);
                for (int p = base + run; p < base + 2 * run; p++) m1 = std::min(m1, d[p]);
            }
            llr[(size_t)i * m + b] = (m1 - m0) * w;
        }
    }
}

// ============================================================================ BBHEADER
uint8_t s2Crc8(const uint8_t* bytes, int n) {
    unsigned crc = 0;
    for (int i = 0; i < n; i++) {
        crc ^= bytes[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) & 0xFF : (crc << 1) & 0xFF;
    }
    return (uint8_t)crc;
}

void s2BuildBbHeader(const S2BbHeader& h, uint8_t* bits) {
    uint8_t by[10];
    by[0] = (uint8_t)((h.tsGs & 3) << 6 | (h.sis ? 1 : 0) << 5 | (h.ccm ? 1 : 0) << 4 | (h.issyi ? 1 : 0) << 3 | (h.npd ? 1 : 0) << 2 | (h.ro & 3));
    by[1] = (uint8_t)h.isi;
    by[2] = (uint8_t)(h.upl >> 8); by[3] = (uint8_t)h.upl;
    by[4] = (uint8_t)(h.dfl >> 8); by[5] = (uint8_t)h.dfl;
    by[6] = (uint8_t)h.sync;
    by[7] = (uint8_t)(h.syncd >> 8); by[8] = (uint8_t)h.syncd;
    by[9] = s2Crc8(by, 9);
    for (int i = 0; i < 80; i++) bits[i] = (by[i >> 3] >> (7 - (i & 7))) & 1;
}

bool s2ParseBbHeader(const uint8_t* bits, S2BbHeader& h) {
    uint8_t by[10] = {};
    for (int i = 0; i < 80; i++) by[i >> 3] |= (uint8_t)((bits[i] & 1) << (7 - (i & 7)));
    h.crcOk = s2Crc8(by, 9) == by[9];
    h.tsGs = by[0] >> 6; h.sis = (by[0] >> 5) & 1; h.ccm = (by[0] >> 4) & 1; h.issyi = (by[0] >> 3) & 1; h.npd = (by[0] >> 2) & 1; h.ro = by[0] & 3;
    h.isi = by[1];
    h.upl = by[2] << 8 | by[3]; h.dfl = by[4] << 8 | by[5]; h.sync = by[6]; h.syncd = by[7] << 8 | by[8];
    return h.crcOk;
}

void s2BbScramble(uint8_t* bits, int n) {
    // clause 5.2.2, figure 5: 15-stage register loaded with 100101010000000, feedback = stage 14 xor stage 15 into stage 1,
    // the feedback bit is the PRBS bit
    static const std::vector<uint8_t> prbs = [] {
        std::vector<uint8_t> v(64800);
        unsigned s[16];                                      // s[1] .. s[15]
        const char* init = "100101010000000";
        for (int i = 0; i < 15; i++) s[i + 1] = init[i] - '0';
        for (size_t i = 0; i < v.size(); i++) {
            const unsigned fb = s[14] ^ s[15];
            v[i] = (uint8_t)fb;
            for (int k = 15; k > 1; k--) s[k] = s[k - 1];
            s[1] = fb;
        }
        return v;
    }();
    for (int i = 0; i < n && i < 64800; i++) bits[i] ^= prbs[i];
}

// ============================================================================ PL signalling
namespace {
// Generator matrix of the (32,6) code of clause 5.5.2.4 (figure 13b), first transmitted bit = bit 31: the rows of the first order Reed-Muller code
// in natural order (Walsh functions), as printed in TR 102 376-1 annex B.1 and used by the open source transmitters and receivers that work with
// real satellites (gr-dtv, gr-dvbs2rx). The text layer of figure 13b in the EN pdf gives other rows (a column permuted code): not used, it would
// not decode real signals. The S2X code adds the row 0x90AC2DDD for b0 (EN 302 307-2 figure 20, from gr-dtv; its distance of 12 to the code
// is the best possible for a row on 32 bits, which tests/test_dvbs_s2fec.cpp checks). Rows for b0, b1, ..., b6.
const uint32_t kPlsG[7] = {0x90AC2DDDu, 0x55555555u, 0x33333333u, 0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu, 0xFFFFFFFFu};
const uint64_t kPlsScramble = 0x719D83C953422DFAull;     // clause 5.5.2.4: 0111000110011101100000111100100101010011010000100010110111111010

// pi/2 BPSK symbol of PLS bit k (header position 26 + k) of a code value: after the SOF an S2X header (b0 = 1) is turned by 90 degrees
// (EN 302 307-2 clause 5.5.2: I = -Q on the odd positions, I = Q on the even ones, both with the sign of S2 turned)
cf32 plsSymbol(int code, int k, int bit) {
    const cf32 s = s2Bpsk(26 + k, bit);
    return code >= 128 ? cf32(-s.imag(), s.real()) : s;
}

struct PlsTables {
    cf32 sym[256][64];
    PlsTables() {
        for (int p = 0; p < 256; p++) {
            int mc; bool sh, pil;
            s2PlsSplit(p, mc, sh, pil);
            const uint64_t code = s2PlsCode(mc, sh, pil);
            for (int k = 0; k < 64; k++) sym[p][k] = plsSymbol(p, k, (int)((code >> (63 - k)) & 1));
        }
    }
};
const PlsTables& plsTables() { static PlsTables t; return t; }
} // namespace

int s2PlsValue(int modcod, bool shortFrame, bool pilots) {
    if (s2ModcodIsS2x(modcod)) return modcod << 1 | (pilots ? 1 : 0);
    return (modcod & 31) << 2 | (shortFrame ? 2 : 0) | (pilots ? 1 : 0);
}

void s2PlsSplit(int code, int& modcod, bool& shortFrame, bool& pilots) {
    code &= 255;
    pilots = code & 1;
    if (code >= 128) {
        modcod = code >> 1;
        const S2xModcod* m = s2xModcodByPls(code);
        shortFrame = m && m->shortFrame;
    } else {
        modcod = code >> 2;
        shortFrame = (code >> 1) & 1;
    }
}

int s2PlsKind(int code) {
    if (code < 0 || code > 255) return -1;
    if (code >= 128) return s2xModcodByPls(code) ? 0 : s2xSpecialFrameSymbols(code) > 0 ? 2 : -1;
    int mc, mod, rate; bool sh, pil;
    s2PlsSplit(code, mc, sh, pil);
    if (mc == 0) return 1;
    return s2ModcodSplit(mc, mod, rate) && s2Dims(mod, rate, sh).ok ? 0 : -1;
}

int s2PlsFrameSymbols(int code) {
    const int kind = s2PlsKind(code);
    if (kind < 0) return 0;
    if (kind == 1) return 90 + 36 * 90;
    if (kind == 2) return s2xSpecialFrameSymbols(code);
    int mc, mod, rate; bool sh, pil;
    s2PlsSplit(code, mc, sh, pil);
    s2ModcodSplit(mc, mod, rate);
    return s2FrameSymbols(s2Dims(mod, rate, sh), pil);
}

uint64_t s2PlsCode(int modcod, bool shortFrame, bool pilots) {
    // EN 302 307-2 clause 5.5.2.4: b0 .. b6 through the (32,7) code (S2: b0 = 0, b1..b5 = MODCOD, b6 = short FECFRAME), b7 (pilots) is the XOR of
    // every second bit
    const int v = s2PlsValue(modcod, shortFrame, pilots);
    uint32_t c = 0;
    for (int i = 0; i < 7; i++) if ((v >> (7 - i)) & 1) c ^= kPlsG[i];
    uint64_t w = 0;
    for (int k = 0; k < 32; k++) {
        const uint64_t y = (c >> (31 - k)) & 1;
        w = (w << 1) | y;
        w = (w << 1) | (y ^ (uint64_t)(v & 1));
    }
    return w ^ kPlsScramble;
}

const cf32* s2SofSymbols() {
    static const std::vector<cf32> s = [] {
        std::vector<cf32> v(26);
        for (int j = 0; j < 26; j++) v[j] = s2Bpsk(j, (int)((kSof >> (25 - j)) & 1));
        return v;
    }();
    return s.data();
}

void s2PlHeader(int modcod, bool shortFrame, bool pilots, cf32* out) {
    const cf32* sof = s2SofSymbols();
    for (int j = 0; j < 26; j++) out[j] = sof[j];
    const uint64_t w = s2PlsCode(modcod, shortFrame, pilots);
    const int v = s2PlsValue(modcod, shortFrame, pilots);
    for (int k = 0; k < 64; k++) out[26 + k] = plsSymbol(v, k, (int)((w >> (63 - k)) & 1));
}

PlsResult s2PlsDecode(const cf32* sym) {
    const PlsTables& T = plsTables();
    float best = -1e30f, second = -1e30f;
    int bi = 0;
    for (int p = 0; p < 256; p++) {
        float s = 0;
        for (int k = 0; k < 64; k++) s += sym[k].real() * T.sym[p][k].real() + sym[k].imag() * T.sym[p][k].imag();
        s /= 64.f;
        if (s > best) { second = best; best = s; bi = p; }
        else if (s > second) second = s;
    }
    PlsResult r;
    s2PlsSplit(bi, r.modcod, r.shortFrame, r.pilots);
    r.code = bi; r.score = best; r.second = second;
    return r;
}

float s2PlsScore(const cf32* sym, int modcod, bool shortFrame, bool pilots) {
    const PlsTables& T = plsTables();
    const int p = s2PlsValue(modcod, shortFrame, pilots);
    float s = 0;
    for (int k = 0; k < 64; k++) s += sym[k].real() * T.sym[p][k].real() + sym[k].imag() * T.sym[p][k].imag();
    return s / 64.f;
}

// ============================================================================ PL scrambling
const std::vector<uint8_t>& s2ScramblingRn(int n) {
    static std::mutex mu;
    static std::map<int, std::vector<uint8_t>*> cache;   // never freed: callers keep references
    static std::vector<uint8_t> xs, ys;
    std::lock_guard<std::mutex> lk(mu);
    const int N = (1 << 18) - 1;
    if (xs.empty()) {
        xs.assign(N, 0); ys.assign(N, 0);
        xs[0] = 1;                                           // x(0) = 1, x(1..17) = 0
        for (int i = 0; i < 18; i++) ys[i] = 1;              // y(0..17) = 1
        for (int i = 0; i + 18 < N; i++) {
            xs[i + 18] = xs[i + 7] ^ xs[i];
            ys[i + 18] = ys[i + 10] ^ ys[i + 7] ^ ys[i + 5] ^ ys[i];
        }
    }
    auto it = cache.find(n);
    if (it != cache.end()) return *it->second;
    auto* v = new std::vector<uint8_t>(66420);
    auto z = [&](long i) { return (uint8_t)(xs[(size_t)((i + n) % N)] ^ ys[(size_t)(i % N)]); };
    for (long i = 0; i < 66420; i++) (*v)[i] = (uint8_t)(2 * z((i + 131072) % N) + z(i));
    cache[n] = v;
    return *v;
}

} // namespace dvbs
} // namespace dect2
