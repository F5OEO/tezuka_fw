// The NEON code below uses AArch64-only intrinsics (vdivq, vpaddq, vmaxvq, vcvtaq): 32-bit ARM takes the plain C++ path
#if defined(__ARM_NEON) && !defined(__aarch64__) && !defined(DECT2_NO_SIMD)
#define DECT2_NO_SIMD 1
#endif
#include "dect2/dvbt.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#if defined(__SSE2__) && !defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <emmintrin.h>
#if defined(__GNUC__) && defined(__x86_64__)
#include <immintrin.h>
#define DECT2_VIT_AVX2 1
#endif
#endif
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#endif

namespace dect2 {
namespace dvbt {

const char* guardName(int g) { static const char* n[] = {"1/32", "1/16", "1/8", "1/4"}; return g >= 0 && g < 4 ? n[g] : "?"; }
const char* modName(int m) { static const char* n[] = {"QPSK", "16-QAM", "64-QAM"}; return m >= 0 && m < 3 ? n[m] : "?"; }
const char* rateName(int r) { static const char* n[] = {"1/2", "2/3", "3/4", "5/6", "7/8"}; return r >= 0 && r < 5 ? n[r] : "?"; }

// ============================================================================ carrier tables (EN 300 744 4.5)
static const int kCp2k[45] = {0, 48, 54, 87, 141, 156, 192, 201, 255, 279, 282, 333, 432, 450, 483, 525, 531, 618, 636, 714, 759, 765, 780, 804, 873, 888, 918, 939, 942, 969, 984, 1050, 1101, 1107, 1110, 1137, 1140, 1146, 1206, 1269, 1323, 1377, 1491, 1683, 1704};
static const int kCp8k[177] = {
    0, 48, 54, 87, 141, 156, 192, 201, 255, 279, 282, 333, 432, 450, 483, 525, 531, 618, 636, 714, 759, 765, 780, 804, 873, 888, 918, 939, 942, 969, 984, 1050, 1101, 1107, 1110, 1137, 1140, 1146, 1206,
    1269, 1323, 1377, 1491, 1683, 1704, 1752, 1758, 1791, 1845, 1860, 1896, 1905, 1959, 1983, 1986, 2037, 2136, 2154, 2187, 2229, 2235, 2322, 2340, 2418, 2463, 2469, 2484, 2508, 2577, 2592, 2622, 2643,
    2646, 2673, 2688, 2754, 2805, 2811, 2814, 2841, 2844, 2850, 2910, 2973, 3027, 3081, 3195, 3387, 3408, 3456, 3462, 3495, 3549, 3564, 3600, 3609, 3663, 3687, 3690, 3741, 3840, 3858, 3891, 3933, 3939,
    4026, 4044, 4122, 4167, 4173, 4188, 4212, 4281, 4296, 4326, 4347, 4350, 4377, 4392, 4458, 4509, 4515, 4518, 4545, 4548, 4554, 4614, 4677, 4731, 4785, 4899, 5091, 5112, 5160, 5166, 5199, 5253, 5268,
    5304, 5313, 5367, 5391, 5394, 5445, 5544, 5562, 5595, 5637, 5643, 5730, 5748, 5826, 5871, 5877, 5892, 5916, 5985, 6000, 6030, 6051, 6054, 6081, 6096, 6162, 6213, 6219, 6222, 6249, 6252, 6258, 6318,
    6381, 6435, 6489, 6603, 6795, 6816};
static const int kTps2k[17] = {34, 50, 209, 346, 413, 569, 595, 688, 790, 901, 1073, 1219, 1262, 1286, 1469, 1594, 1687};
static const int kTps8k[68] = {34, 50, 209, 346, 413, 569, 595, 688, 790, 901, 1073, 1219, 1262, 1286, 1469, 1594, 1687, 1738, 1754, 1913, 2050, 2117, 2273, 2299, 2392, 2494, 2605, 2777, 2923, 2966, 2990, 3173, 3298, 3391, 3442, 3458, 3617, 3754, 3821, 3977, 4003, 4096, 4198, 4309, 4481, 4627, 4670, 4694, 4877, 5002, 5095, 5146, 5162, 5321, 5458, 5525, 5681, 5707, 5800, 5902, 6013, 6185, 6331, 6374, 6398, 6581, 6706, 6799};

const std::vector<int>& continualPilots(int mode) {
    static const std::vector<int> a(kCp2k, kCp2k + 45), b(kCp8k, kCp8k + 177);
    return mode == k8K ? b : a;
}
const std::vector<int>& tpsCarriers(int mode) {
    static const std::vector<int> a(kTps2k, kTps2k + 17), b(kTps8k, kTps8k + 68);
    return mode == k8K ? b : a;
}
const std::vector<uint8_t>& prbsW() {
    static std::vector<uint8_t> w;
    static std::once_flag once;
    std::call_once(once, [] {
        w.resize(6817);
        unsigned reg = 0x7FF;
        for (auto& v : w) { v = reg & 1; const unsigned nb = ((reg >> 2) ^ reg) & 1; reg = (reg >> 1) | (nb << 10); }
    });
    return w;
}

void carrierRoles(int mode, int symIdx, std::vector<uint8_t>& roles) {
    const int K = carriersK(mode);
    roles.assign(K, 0);
    for (int k = 3 * (symIdx % 4); k < K; k += 12) roles[k] = 1;
    for (int k : continualPilots(mode)) roles[k] = 2;
    for (int k : tpsCarriers(mode)) roles[k] = 3;
}

// ============================================================================ TPS (4.6)
static void putBits(std::array<uint8_t, 68>& s, int first, int last, unsigned v) { for (int i = last; i >= first; i--) { s[i] = v & 1; v >>= 1; } }

static void tpsBch(std::array<uint8_t, 68>& s) {
    unsigned reg = 0;
    uint8_t in[113];
    memset(in, 0, 60);
    memcpy(in + 60, &s[1], 53);
    for (int i = 0; i < 113; i++) {
        const unsigned fb = (in[i] ^ reg) & 1;
        reg >>= 1;
        reg |= fb << 13;
        reg ^= (fb << 12) ^ (fb << 11) ^ (fb << 9) ^ (fb << 8) ^ (fb << 7) ^ (fb << 5) ^ (fb << 4);
    }
    for (int i = 0; i < 14; i++) s[54 + i] = (reg >> i) & 1;
}

std::array<uint8_t, 68> tpsBits(const Params& p, int frameIdx) {
    std::array<uint8_t, 68> s{};
    s[0] = prbsW()[0];
    putBits(s, 1, 16, (frameIdx & 1) ? 0xCA11 : 0x35EE);
    putBits(s, 17, 22, p.cellIdLength ? 0x1F : 0x17);
    putBits(s, 23, 24, frameIdx & 3);
    putBits(s, 25, 26, p.mod);
    putBits(s, 27, 29, p.hier);
    putBits(s, 30, 32, p.crHp);
    putBits(s, 33, 35, p.crLp);
    putBits(s, 36, 37, p.guard);
    putBits(s, 38, 39, p.mode);
    putBits(s, 40, 47, (frameIdx & 1) ? (p.cellId & 0xFF) : ((p.cellId >> 8) & 0xFF));
    putBits(s, 48, 53, 0);
    tpsBch(s);
    return s;
}

bool tpsSync(const uint8_t* b, bool& odd) {
    unsigned v = 0;
    for (int i = 0; i < 16; i++) v = (v << 1) | (b[i] & 1);
    if (v == 0x35EE) { odd = false; return true; }
    if (v == 0xCA11) { odd = true; return true; }
    return false;
}

bool tpsDecode(const uint8_t bits[68], Params& p, int& frameIdx, bool& odd, int maxFix) {
    std::array<uint8_t, 68> s;
    for (int i = 0; i < 68; i++) s[i] = bits[i] & 1;
    auto valid = [&]() {
        if (!tpsSync(s.data() + 1, odd)) return false;
        std::array<uint8_t, 68> chk = s;
        tpsBch(chk);
        for (int i = 54; i < 68; i++) if (chk[i] != s[i]) return false;
        return true;
    };
    // The BCH code (67,53) corrects two errors: when the block does not check, look for the codeword within maxFix flips of
    // s1..s67 (s0 is only the differential reference). By brute force: at most 2278 candidates once per frame.
    bool ok = valid();
    for (int i = 1; !ok && maxFix >= 1 && i < 68; i++) {
        s[i] ^= 1;
        ok = valid();
        for (int j = i + 1; !ok && maxFix >= 2 && j < 68; j++) { s[j] ^= 1; ok = valid(); if (!ok) s[j] ^= 1; }
        if (!ok) s[i] ^= 1;
    }
    if (!ok) return false;
    auto get = [&](int a, int b) { unsigned v = 0; for (int i = a; i <= b; i++) v = (v << 1) | s[i]; return (int)v; };
    // The length indicator is 0x17, or 0x1F with the cell id (and the DVB-H bits); it says nothing the receiver needs, and the BCH code has
    // already vouched for the block, so another value is taken as it comes
    const int len = get(17, 22);
    p.cellIdLength = len == 0x1F;
    frameIdx = get(23, 24);
    if (odd != ((frameIdx & 1) != 0)) return false;
    p.mod = get(25, 26);
    p.hier = get(27, 29);
    p.crHp = get(30, 32);
    p.crLp = get(33, 35);
    p.guard = get(36, 37);
    p.mode = get(38, 39);
    // Without hierarchy there is no LP stream, and its code rate field may hold anything (some transmitters leave it at a reserved value):
    // read it as the HP rate, or one strange field would hide a multiplex that decodes perfectly well
    if (p.hier == 0 && p.crLp > 4) p.crLp = p.crHp;
    if (p.mod > 2 || p.hier > 3 || p.crHp > 4 || p.crLp > 4 || p.mode > 1) return false;
    return true;
}

// ============================================================================ constellation (4.3.5)
void constellation(int mod, int hier, std::vector<cf32>& pts) {
    const int m = bitsPerCell(mod);
    const int size = 1 << m, alpha = alphaOf(hier);
    float norm;
    if (m == 2) norm = 1.f / std::sqrt(2.f);
    else if (m == 4) norm = alpha == 1 ? 1.f / std::sqrt(10.f) : alpha == 2 ? 1.f / std::sqrt(20.f) : 1.f / std::sqrt(52.f);
    else norm = alpha == 1 ? 1.f / std::sqrt(42.f) : alpha == 2 ? 1.f / std::sqrt(60.f) : 1.f / std::sqrt(108.f);
    const int bitsPerAxis = m / 2, stepsPerAxis = (int)std::lround(std::sqrt((double)size)) / 2 - 1, step = 2;
    pts.assign(size, cf32(0, 0));
    for (int i = 0; i < size; i++) {
        const int q = (i >> (2 * (bitsPerAxis - 1))) & 3;
        const int sign0 = (q >> 1) ? -1 : 1, sign1 = (q & 1) ? -1 : 1;
        int x = (i >> (bitsPerAxis - 1)) & ((1 << (bitsPerAxis - 1)) - 1);
        int y = i & ((1 << (bitsPerAxis - 1)) - 1);
        const int xval = alpha + (stepsPerAxis - x) * step, yval = alpha + (stepsPerAxis - y) * step;
        int val = ((x >> 1) ^ x) << (bitsPerAxis - 1) | ((y >> 1) ^ y);
        x = 0; y = 0;
        for (int j = 0; j < bitsPerAxis - 1; j++) { x += ((val >> (1 + 2 * j)) & 1) << j; y += ((val >> (2 * j)) & 1) << j; }
        val = (q << (2 * (bitsPerAxis - 1))) + (x << (bitsPerAxis - 1)) + y;
        pts[val] = norm * cf32((float)(sign0 * xval), (float)(sign1 * yval));
    }
}

// ============================================================================ transmit chain
namespace {
// the energy dispersal sequence: byte k (1..187) of packet g (0..7) of a group of eight
struct DispersalSeq {
    uint8_t b[8][188];
    DispersalSeq() {
        unsigned reg = 0xA9;
        auto clock8 = [&] {
            unsigned res = 0;
            for (int i = 0; i < 8; i++) {
                const unsigned fb = ((reg >> 13) ^ (reg >> 14)) & 1;
                reg = ((reg << 1) | fb) & 0x7FFF;
                res = (res << 1) | fb;
            }
            return res;
        };
        for (int g = 0; g < 8; g++) {
            b[g][0] = 0;
            for (int k = 1; k < 188; k++) b[g][k] = (uint8_t)clock8();
            clock8();   // the PRBS runs over the sync byte of the next packet
        }
    }
};
const DispersalSeq& dispersalSeq() { static const DispersalSeq d; return d; }
}

void scramble(const uint8_t* ts, size_t packets, uint8_t* out) {
    const DispersalSeq& D = dispersalSeq();
    for (size_t p = 0; p < packets; p++) {
        const uint8_t* in = ts + p * 188;
        uint8_t* o = out + p * 188;
        const uint8_t* q = D.b[p % 8];
        o[0] = (p % 8 == 0) ? 0xB8 : 0x47;
        for (int k = 1; k < 188; k++) o[k] = in[k] ^ q[k];
    }
}

namespace {
struct Gf {
    uint8_t exp[512], log[256];
    Gf() {
        unsigned x = 1;
        for (int i = 0; i < 255; i++) { exp[i] = (uint8_t)x; log[x] = (uint8_t)i; x <<= 1; if (x & 0x100) x ^= 0x11D; }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    uint8_t inv(uint8_t a) const { return exp[255 - log[a]]; }
};
const Gf& gf() { static Gf g; return g; }
// g(x) = prod_{i=0}^{15} (x - alpha^i): coefficients g[0..15] (x^16 implicit)
const std::array<uint8_t, 16>& rsGen() {
    static std::array<uint8_t, 16> g = [] {
        std::array<uint8_t, 17> p{};
        p[0] = 1;
        const Gf& f = gf();
        for (int i = 0; i < 16; i++) {          // multiply by (x + alpha^i)
            std::array<uint8_t, 17> n{};
            for (int j = 0; j <= i; j++) { n[j + 1] ^= p[j]; n[j] ^= f.mul(p[j], f.exp[i]); }
            p = n;
        }
        std::array<uint8_t, 16> r;
        for (int i = 0; i < 16; i++) r[i] = p[i];
        return r;
    }();
    return g;
}
} // namespace

namespace {
// fb * generator for every feedback byte: one row of 16 bytes per value, so that one division step is two 64-bit exclusive ors
struct RsRows {
    alignas(8) uint8_t row[256][16];
    RsRows() {
        const Gf& f = gf();
        const auto& G = rsGen();
        for (int v = 0; v < 256; v++) {
            for (int j = 0; j < 15; j++) row[v][j] = f.mul((uint8_t)v, G[15 - j]);
            row[v][15] = f.mul((uint8_t)v, G[0]);
        }
    }
};
const RsRows& rsRows() { static const RsRows r; return r; }
}

void rsEncode(const uint8_t* in, uint8_t* out) {
    const RsRows& R = rsRows();
    uint8_t b[188 + 16 + 8] = {};   // the message, then the remainder builds up in the bytes behind it
    memcpy(b, in, 188);
    for (int i = 0; i < 188; i++) {
        const uint8_t fb = b[i];
        if (!fb) continue;
        uint64_t x[2], y[2];
        memcpy(x, b + i + 1, 16); memcpy(y, R.row[fb], 16);
        x[0] ^= y[0]; x[1] ^= y[1];
        memcpy(b + i + 1, x, 16);
    }
    memcpy(out, in, 188);
    memcpy(out + 188, b + 188, 16);
}

ConvInterleaver::ConvInterleaver(bool inverse) : inverse_(inverse) {
    fifo_.resize(12);
    pos_.assign(12, 0);
    for (int j = 0; j < 12; j++) fifo_[j].assign((size_t)17 * (inverse ? 11 - j : j), 0);
}
void ConvInterleaver::process(const uint8_t* in, uint8_t* out, size_t n) {
    // every branch is a delay line over every twelfth byte: output k is the oldest of the stored bytes while there are any, then the input
    // that came D inputs earlier; the line then holds the last D inputs
    std::vector<uint8_t> copy;
    if (in == out) { copy.assign(in, in + n); in = copy.data(); }   // the branches read inputs that an in-place run would already have overwritten
    for (int j = 0; j < 12; j++) {
        auto& f = fifo_[j];
        const size_t D = f.size();
        const size_t nj = n > (size_t)j ? (n - (size_t)j + 11) / 12 : 0;
        if (!D) { for (size_t k = 0, i = (size_t)j; k < nj; k++, i += 12) out[i] = in[i]; continue; }
        // stored bytes in order, oldest first
        uint8_t stored[17 * 11];
        for (size_t k = 0; k < D; k++) stored[k] = f[(pos_[j] + k) % D];
        size_t i = (size_t)j;
        for (size_t k = 0; k < nj; k++, i += 12) out[i] = k < D ? stored[k] : in[i - D * 12];
        if (nj >= D) for (size_t k = 0; k < D; k++) f[k] = in[(size_t)j + (nj - D + k) * 12];
        else {
            for (size_t k = nj; k < D; k++) f[k - nj] = stored[k];
            for (size_t k = 0; k < nj; k++) f[D - nj + k] = in[(size_t)j + k * 12];
        }
        pos_[j] = 0;
    }
}

namespace {
// (x, y) code bits for every register state, bit 0 = x
struct InnerTab {
    uint8_t xy[128];
    InnerTab() { for (unsigned r = 0; r < 128; r++) xy[r] = (uint8_t)(__builtin_parity(r & 0x79) | (__builtin_parity(r & 0x5B) << 1)); }
};
}

void InnerEncoder::encode(const std::vector<uint8_t>& bits, std::vector<uint8_t>& coded) {
    // pattern per input step: bit0 = X present, bit1 = Y present
    static const int kPat[5][7] = {{3, 0, 0, 0, 0, 0, 0}, {3, 2, 0, 0, 0, 0, 0}, {3, 2, 1, 0, 0, 0, 0}, {3, 2, 1, 2, 1, 0, 0}, {3, 2, 2, 2, 1, 2, 1}};
    static const int kLen[5] = {1, 2, 3, 5, 7};
    static const InnerTab tab;
    const int k = kLen[rate_];
    const size_t n = bits.size();
    coded.resize(2 * n + 1);
    uint8_t* o = coded.data();
    size_t w = 0;
    unsigned reg = reg_;
    int ph = (int)(step_ % (size_t)k);
    for (size_t i = 0; i < n; i++) {
        reg |= (unsigned)(bits[i] & 1) << 7;
        reg >>= 1;
        const unsigned v = tab.xy[reg & 127];
        const int pat = kPat[rate_][ph];
        if (++ph == k) ph = 0;
        // x always written, kept when the pattern has it; then y the same way (a dropped value is overwritten by the next one)
        o[w] = (uint8_t)(v & 1); w += (size_t)(pat & 1);
        o[w] = (uint8_t)(v >> 1); w += (size_t)((pat >> 1) & 1);
    }
    reg_ = reg;
    step_ += n;
    coded.resize(w);
}

namespace {
inline int hCol(int w, int e) { static const int off[6] = {0, 63, 105, 42, 21, 84}; return (w + off[e]) % 126; }
}

void bitInterleave(const std::vector<uint8_t>& coded, int mod, std::vector<uint8_t>& words) {
    const int v = bitsPerCell(mod);
    const size_t nw = coded.size() / v;
    // output word w, bit e (MSB first) is bit k(e) of input word hCol(w, e) of the block: a fixed table per modulation
    static uint16_t src[3][126 * 6];
    static uint8_t shiftTab[3][6];
    static std::once_flag once[3];
    std::call_once(once[mod], [&] {
        for (int e = 0; e < v; e++) {
            int kk = 0;
            for (int k = 0; k < v; k++) if ((k / (v / 2)) + 2 * (k % (v / 2)) == e) kk = k;
            shiftTab[mod][e] = (uint8_t)(v - kk - 1);
        }
        for (int w = 0; w < 126; w++) for (int e = 0; e < v; e++) src[mod][w * v + e] = (uint16_t)hCol(w, e);
    });
    std::vector<uint8_t> w0(nw);
    for (size_t i = 0; i < nw; i++) { unsigned c = 0; for (int j = 0; j < v; j++) c = (c << 1) | coded[i * v + j]; w0[i] = (uint8_t)c; }
    words.assign(nw, 0);
    for (size_t b = 0; b + 126 <= nw; b += 126) {
        const uint8_t* in = &w0[b];
        for (int w = 0; w < 126; w++) {
            unsigned val = 0;
            for (int e = 0; e < v; e++) val = (val << 1) | ((in[src[mod][w * v + e]] >> shiftTab[mod][e]) & 1u);
            words[b + w] = (uint8_t)val;
        }
    }
}

const std::vector<int>& symbolPermutation(int mode) {
    static std::vector<int> h[2];
    static std::once_flag once[2];
    std::call_once(once[mode], [mode] {
        static const int perm2k[10] = {4, 3, 9, 6, 2, 8, 1, 5, 7, 0};
        static const int perm8k[12] = {7, 1, 4, 2, 9, 6, 8, 10, 0, 3, 11, 5};
        const int Mmax = fftN(mode), Nmax = dataCarriers(mode);
        const int Nr = mode == k8K ? 13 : 11;
        const int* bp = mode == k8K ? perm8k : perm2k;
        std::vector<int>& H = h[mode];
        H.assign(Mmax, 0);
        unsigned reg = 1; // state for i >= 2 (state_2 = 1)
        const unsigned mask = (1u << Nr) - 1;
        int q = 0;
        for (int i = 0; i < Mmax; i++) {
            unsigned r = 0;
            if (i >= 2) {
                if (i > 2) {
                    const unsigned nb = mode == k8K ? ((reg ^ (reg >> 1) ^ (reg >> 4) ^ (reg >> 6)) & 1) : ((reg ^ (reg >> 3)) & 1);
                    reg = ((reg >> 1) | (nb << (Nr - 2))) & mask;
                }
                r = reg;
            }
            unsigned nr = 0;
            for (int k = 0; k < Nr - 1; k++) nr |= ((r >> k) & 1u) << bp[k];
            H[q] = (int)(((unsigned)(i % 2) << (Nr - 1)) + nr);
            if (H[q] < Nmax) q++;
        }
        H.resize(Nmax);
    });
    return h[mode];
}

void mapSymbol(const std::vector<uint8_t>& words, int mod, int hier, std::vector<cf32>& cells) {
    std::vector<cf32> pts;
    constellation(mod, hier, pts);
    cells.resize(words.size());
    for (size_t i = 0; i < words.size(); i++) cells[i] = pts[words[i]];
}

// ============================================================================ receive chain
namespace {
struct AxisTables {
    static constexpr int G = 1024;
    float R = 0, scale = 0;
    int nbits = 0;
    // for label bit j: which axis (0 I, 1 Q) and the grid of f(y) = min(bit=1) - min(bit=0) over axis levels
    int axis[6];
    std::vector<float> lut[6];
    // the same function as a sum of ramps, f(y) = f0 + s0 (y - y0) + sum_i ds_i max(0, y - bp_i): exact, and four cells at a time in
    // SIMD registers instead of a table lookup per bit (kMaxKinks breakpoints at most; `ramps` false when a bit needs more)
    static constexpr int kMaxKinks = 8;
    bool ramps = true;
    float y0[6], f0[6], s0[6];
    int nk[6];
    float bp[6][kMaxKinks], ds[6][kMaxKinks];
    float yMax = 0;   // largest input the table covers (the demapper clamps to [-R, yMax], like the table lookup does)
};
const AxisTables& tables(int mod, int hier) {
    static AxisTables t[3][4];
    static std::once_flag once[3][4];
    std::call_once(once[mod][hier], [mod, hier] {
        AxisTables& T = t[mod][hier];
        std::vector<cf32> pts;
        constellation(mod, hier, pts);
        const int m = bitsPerCell(mod);
        T.nbits = m;
        float mx = 0;
        for (auto& p : pts) mx = std::max(mx, std::max(std::fabs(p.real()), std::fabs(p.imag())));
        T.R = mx + 1.2f;
        T.scale = (float)AxisTables::G / (2 * T.R);
        for (int j = 0; j < m; j++) {
            const int sh = m - 1 - j;
            bool movesRe = false;
            for (int i = 0; i < (1 << m); i++) if (std::fabs(pts[i].real() - pts[i ^ (1 << sh)].real()) > 1e-6f) movesRe = true;
            T.axis[j] = movesRe ? 0 : 1;
            T.lut[j].resize(AxisTables::G + 2);
            for (int g = 0; g <= AxisTables::G + 1; g++) {
                const float y = -T.R + (float)g / T.scale;
                float m0 = 1e30f, m1 = 1e30f;
                for (int i = 0; i < (1 << m); i++) {
                    const float c = movesRe ? pts[i].real() : pts[i].imag();
                    const float e = (y - c) * (y - c);
                    if ((i >> sh) & 1) m1 = std::min(m1, e); else m0 = std::min(m0, e);
                }
                T.lut[j][g] = m1 - m0;
            }
            // ramp form from the exact function: the breakpoints can only lie halfway between two levels of the axis
            std::vector<float> lev;
            for (int i = 0; i < (1 << m); i++) lev.push_back(movesRe ? pts[i].real() : pts[i].imag());
            std::sort(lev.begin(), lev.end());
            lev.erase(std::unique(lev.begin(), lev.end(), [](float a, float b) { return std::fabs(a - b) < 1e-6f; }), lev.end());
            std::vector<float> cand;
            for (size_t a = 0; a < lev.size(); a++) for (size_t b = a + 1; b < lev.size(); b++) cand.push_back(0.5f * (lev[a] + lev[b]));
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end(), [](float a, float b) { return std::fabs(a - b) < 1e-6f; }), cand.end());
            auto exact = [&](float y) {
                float m0 = 1e30f, m1 = 1e30f;
                for (int i = 0; i < (1 << m); i++) {
                    const float c = movesRe ? pts[i].real() : pts[i].imag();
                    const float e = (y - c) * (y - c);
                    if ((i >> sh) & 1) m1 = std::min(m1, e); else m0 = std::min(m0, e);
                }
                return m1 - m0;
            };
            // slope in each interval between breakpoints, from two points well inside it
            std::vector<float> edges;
            edges.push_back(-T.R - 1.f);
            for (float c : cand) edges.push_back(c);
            edges.push_back(T.R + 1.f);
            std::vector<float> slope;
            for (size_t i = 0; i + 1 < edges.size(); i++) {
                const float lo = edges[i], hi = edges[i + 1], w = hi - lo;
                slope.push_back((exact(lo + 0.75f * w) - exact(lo + 0.25f * w)) / (0.5f * w));
            }
            T.y0[j] = -T.R - 1.f; T.f0[j] = exact(T.y0[j]); T.s0[j] = slope[0];
            T.nk[j] = 0;
            for (size_t i = 1; i < slope.size(); i++) {
                const float d = slope[i] - slope[i - 1];
                if (std::fabs(d) < 1e-4f) continue;
                if (T.nk[j] >= AxisTables::kMaxKinks) { T.ramps = false; break; }
                T.bp[j][T.nk[j]] = cand[i - 1]; T.ds[j][T.nk[j]] = d; T.nk[j]++;
            }
        }
        T.yMax = -T.R + ((float)AxisTables::G - 0.001f) / T.scale;
    });
    return t[mod][hier];
}
} // namespace

#if (defined(__ARM_NEON) || defined(__SSE2__)) && !defined(DECT2_NO_SIMD)
#define DECT2_DEMAP_SIMD 1
namespace {
#if defined(__ARM_NEON)
using F4 = float32x4_t;
inline F4 f4set(float x) { return vdupq_n_f32(x); }
inline F4 f4add(F4 a, F4 b) { return vaddq_f32(a, b); }
inline F4 f4sub(F4 a, F4 b) { return vsubq_f32(a, b); }
inline F4 f4mul(F4 a, F4 b) { return vmulq_f32(a, b); }
inline F4 f4max(F4 a, F4 b) { return vmaxq_f32(a, b); }
inline F4 f4min(F4 a, F4 b) { return vminq_f32(a, b); }
inline F4 f4div(F4 a, F4 b) { return vdivq_f32(a, b); }
inline void f4load2(const float* p, F4& re, F4& im) { const float32x4x2_t v = vld2q_f32(p); re = v.val[0]; im = v.val[1]; }
inline F4 f4load(const float* p) { return vld1q_f32(p); }
// bits j and j+1 of four cells -> (c0 j j+1)(c1 ..)(c2 ..)(c3 ..) as two registers
inline void f4zip(F4 a, F4 b, F4& lo, F4& hi) { const float32x4x2_t z = vzipq_f32(a, b); lo = z.val[0]; hi = z.val[1]; }
inline void f4store2(float* p, F4 v, int half) { vst1_f32(p, half ? vget_high_f32(v) : vget_low_f32(v)); }
#else
using F4 = __m128;
inline F4 f4set(float x) { return _mm_set1_ps(x); }
inline F4 f4add(F4 a, F4 b) { return _mm_add_ps(a, b); }
inline F4 f4sub(F4 a, F4 b) { return _mm_sub_ps(a, b); }
inline F4 f4mul(F4 a, F4 b) { return _mm_mul_ps(a, b); }
inline F4 f4max(F4 a, F4 b) { return _mm_max_ps(a, b); }
inline F4 f4min(F4 a, F4 b) { return _mm_min_ps(a, b); }
inline F4 f4div(F4 a, F4 b) { return _mm_div_ps(a, b); }
inline void f4load2(const float* p, F4& re, F4& im) {
    const __m128 a = _mm_loadu_ps(p), b = _mm_loadu_ps(p + 4);
    re = _mm_shuffle_ps(a, b, _MM_SHUFFLE(2, 0, 2, 0)); im = _mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 1, 3, 1));
}
inline F4 f4load(const float* p) { return _mm_loadu_ps(p); }
inline void f4zip(F4 a, F4 b, F4& lo, F4& hi) { lo = _mm_unpacklo_ps(a, b); hi = _mm_unpackhi_ps(a, b); }
inline void f4store2(float* p, F4 v, int half) { if (half) _mm_storeh_pi((__m64*)p, v); else _mm_storel_pi((__m64*)p, v); }
#endif
}   // namespace

// four cells per step, the LLR of every bit from its ramp sum; llr[c * m + j] as before
static void demapRamps(const AxisTables& T, const cf32* cells, const float* n0, int count, float* llr) {
    const int m = T.nbits;
    const F4 lo = f4set(-T.R), hi = f4set(T.yMax), tiny = f4set(1e-9f), two = f4set(2.f), zero = f4set(0.f), one = f4set(1.f);
    int c = 0;
    for (; c + 4 <= count; c += 4) {
        F4 y[2];
        f4load2(reinterpret_cast<const float*>(cells + c), y[0], y[1]);
        const F4 w = f4div(one, f4max(tiny, f4mul(two, f4load(n0 + c))));
        F4 v[6];
        for (int j = 0; j < m; j++) {
            const F4 yy = f4min(f4max(y[T.axis[j]], lo), hi);
            F4 f = f4add(f4set(T.f0[j]), f4mul(f4set(T.s0[j]), f4sub(yy, f4set(T.y0[j]))));
            for (int i = 0; i < T.nk[j]; i++) f = f4add(f, f4mul(f4set(T.ds[j][i]), f4max(zero, f4sub(yy, f4set(T.bp[j][i])))));
            v[j] = f4mul(w, f);
        }
        for (int p = 0; p < m / 2; p++) {
            F4 za, zb;
            f4zip(v[2 * p], v[2 * p + 1], za, zb);
            for (int cc = 0; cc < 4; cc++) f4store2(llr + (size_t)(c + cc) * m + 2 * p, cc < 2 ? za : zb, cc & 1);
        }
    }
    // the last few cells, the same arithmetic one at a time
    for (; c < count; c++) {
        const float w = 1.f / std::max(1e-9f, 2 * n0[c]);
        const float yv[2] = {cells[c].real(), cells[c].imag()};
        for (int j = 0; j < m; j++) {
            const float yy = std::min(std::max(yv[T.axis[j]], -T.R), T.yMax);
            float f = T.f0[j] + T.s0[j] * (yy - T.y0[j]);
            for (int i = 0; i < T.nk[j]; i++) f += T.ds[j][i] * std::max(0.f, yy - T.bp[j][i]);
            llr[(size_t)c * m + j] = w * f;
        }
    }
}
#endif

void demap(const cf32* cells, const float* n0, int count, int mod, int hier, float* llr) {
    const AxisTables& T = tables(mod, hier);
    const int m = T.nbits;
#ifdef DECT2_DEMAP_SIMD
    if (T.ramps && m % 2 == 0) { demapRamps(T, cells, n0, count, llr); return; }
#endif
    for (int c = 0; c < count; c++) {
        const float w = 1.f / std::max(1e-9f, 2 * n0[c]);
        const float y[2] = {cells[c].real(), cells[c].imag()};
        for (int j = 0; j < m; j++) {
            float t = (y[T.axis[j]] + T.R) * T.scale;
            t = std::min(std::max(t, 0.f), (float)AxisTables::G - 0.001f);
            const int i = (int)t;
            const float fr = t - (float)i;
            llr[(size_t)c * m + j] = w * (T.lut[j][i] + fr * (T.lut[j][i + 1] - T.lut[j][i]));
        }
    }
}

void symbolDeinterleave(int mode, int symIdx, const cf32* in, const float* n0in, cf32* out, float* n0out) {
    const auto& H = symbolPermutation(mode);
    const int N = dataCarriers(mode);
    if (symIdx % 2) { for (int q = 0; q < N; q++) { out[H[q]] = in[q]; n0out[H[q]] = n0in[q]; } }
    else { for (int q = 0; q < N; q++) { out[q] = in[H[q]]; n0out[q] = n0in[H[q]]; } }
}

void bitDeinterleave(const float* in, int mod, int words, float* out) {
    const int v = bitsPerCell(mod);
    // output element i*v+k of a 126-word block comes from input element w*v+e with e the interleaver branch of bit k and
    // w the word whose column in that branch is i: a fixed permutation, built once
    static std::vector<uint16_t> perm[3];
    static std::once_flag once[3];
    std::call_once(once[mod], [&] {
        perm[mod].resize((size_t)126 * v);
        for (int i = 0; i < 126; i++)
            for (int k = 0; k < v; k++) {
                const int e = (k / (v / 2)) + 2 * (k % (v / 2));
                static const int off[6] = {0, 63, 105, 42, 21, 84};
                perm[mod][(size_t)i * v + k] = (uint16_t)(((i - off[e] + 126) % 126) * v + e);
            }
    });
    const uint16_t* P = perm[mod].data();
    const int blockLen = 126 * v;
    for (int b = 0; b + 126 <= words; b += 126) {
        const float* src = in + (size_t)b * v;
        float* dst = out + (size_t)b * v;
        for (int j = 0; j < blockLen; j++) dst[j] = src[P[j]];
    }
}

namespace {
const int kBranchOff[6] = {0, 63, 105, 42, 21, 84};   // H_e(w) = (w + off_e) mod 126
int lpBranch(int j, int v) { const int u = v - 2; return (j % u) / (u / 2) + 2 * (j % (u / 2)) + 2; }
}

void bitInterleaveHier(const std::vector<uint8_t>& hp, const std::vector<uint8_t>& lp, int mod, std::vector<uint8_t>& words) {
    const int v = bitsPerCell(mod), u = v - 2;
    const size_t blocks = std::min(hp.size() / 252, lp.size() / ((size_t)126 * u));
    words.assign(blocks * 126, 0);
    uint8_t br[6][126];
    for (size_t b = 0; b < blocks; b++) {
        for (int i = 0; i < 126; i++) {
            for (int k = 0; k < 2; k++) br[k][i] = hp[(b * 126 + i) * 2 + k];
            for (int j = 0; j < u; j++) br[lpBranch(j, v)][i] = lp[(b * 126 + i) * u + j];
        }
        for (int w = 0; w < 126; w++) {
            unsigned val = 0;
            for (int e = 0; e < v; e++) val = (val << 1) | br[e][(w + kBranchOff[e]) % 126];
            words[b * 126 + w] = (uint8_t)val;
        }
    }
}

void bitDeinterleaveLp(const float* in, int mod, int words, float* out) {
    const int v = bitsPerCell(mod), u = v - 2;
    for (int b = 0; b + 126 <= words; b += 126)
        for (int i = 0; i < 126; i++)
            for (int j = 0; j < u; j++) {
                const int e = lpBranch(j, v);
                out[(size_t)(b + i) * u + j] = in[(size_t)(b + (i - kBranchOff[e] + 126) % 126) * v + e];
            }
}

// llr * scale, rounded to nearest and limited to +-127
static void quantise(const float* llr, size_t n, float scale, int8_t* q) {
    size_t j = 0;
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
    const float32x4_t sc = vdupq_n_f32(scale);
    for (; j + 8 <= n; j += 8) {
        const int32x4_t a = vcvtaq_s32_f32(vmulq_f32(vld1q_f32(llr + j), sc)), b = vcvtaq_s32_f32(vmulq_f32(vld1q_f32(llr + j + 4), sc));
        vst1_s8(q + j, vqmovn_s16(vcombine_s16(vqmovn_s32(a), vqmovn_s32(b))));
    }
#elif defined(__SSE2__) && !defined(DECT2_NO_SIMD)
    const __m128 sc = _mm_set1_ps(scale);
    for (; j + 8 <= n; j += 8) {
        const __m128i a = _mm_cvtps_epi32(_mm_mul_ps(_mm_loadu_ps(llr + j), sc)), b = _mm_cvtps_epi32(_mm_mul_ps(_mm_loadu_ps(llr + j + 4), sc));
        _mm_storel_epi64((__m128i*)(q + j), _mm_packs_epi16(_mm_packs_epi32(a, b), _mm_setzero_si128()));
    }
#endif
    for (; j < n; j++) {
        const float v = std::max(-127.f, std::min(127.f, llr[j] * scale));
        q[j] = (int8_t)(int)(v + (v < 0 ? -0.5f : 0.5f));
    }
}

void Viterbi::depuncture(const float* llr, size_t n, int rate, int phase, std::vector<int8_t>& soft, float scale) {
    static const int kPat[5][7] = {{3, 0, 0, 0, 0, 0, 0}, {3, 2, 0, 0, 0, 0, 0}, {3, 2, 1, 0, 0, 0, 0}, {3, 2, 1, 2, 1, 0, 0}, {3, 2, 2, 2, 1, 2, 1}};
    static const int kLen[5] = {1, 2, 3, 5, 7};
    const int k = kLen[rate];
    // quantise everything first (a plain loop the compiler vectorises), then spread the values over the two outputs of each step
    static thread_local std::vector<int8_t> q;
    q.resize(n);
    quantise(llr, n, scale, q.data());
    int ones = 0;
    for (int j = 0; j < k; j++) ones += (kPat[rate][j] & 1) + ((kPat[rate][j] >> 1) & 1);
    // whole puncturing periods: the position in the output period (2k values) of each of the `ones` input values, from this phase
    int pos[16], np = 0;
    for (int j = 0; j < k; j++) {
        const int pat = kPat[rate][(phase % k + j) % k];
        if (pat & 1) pos[np++] = 2 * j;
        if (pat & 2) pos[np++] = 2 * j + 1;
    }
    const size_t periods = n / ones;
    soft.assign((periods + 1) * k * 2, 0);
    int8_t* o = soft.data();
    for (size_t p = 0; p < periods; p++) {
        int8_t* op = o + p * 2 * k;
        const int8_t* qp = q.data() + p * ones;
        for (int j = 0; j < ones; j++) op[pos[j]] = qp[j];
    }
    // what is left after the last whole period: the same step by step walk, which also decides what happens to an incomplete last step
    size_t i = periods * ones;
    int step = phase % k;
    size_t out = periods * 2 * k;
    while (i < n) {
        const int pat = kPat[rate][step];
        int8_t x = 0, y = 0;
        if (pat & 1) { if (i >= n) break; x = q[i++]; }
        if (pat & 2) { if (i >= n) break; y = q[i++]; }
        o[out++] = x;
        o[out++] = y;
        if (++step == k) step = 0;
    }
    soft.resize(out);
}

namespace {
struct Trellis {
    // for each new state ns and predecessor bit b: expected code bits (x,y) as +1 / -1 signs
    int8_t sx[64][2], sy[64][2];
    Trellis() {
        for (int ns = 0; ns < 64; ns++) {
            const int u = ns >> 5;
            for (int b = 0; b < 2; b++) {
                const int s = ((ns & 31) << 1) | b;           // predecessor state
                const unsigned reg = ((unsigned)u << 6) | (unsigned)s;
                sx[ns][b] = __builtin_parity(reg & 0x79) ? -1 : 1;
                sy[ns][b] = __builtin_parity(reg & 0x5B) ? -1 : 1;
            }
        }
    }
};
const Trellis& trellis() { static Trellis t; return t; }

#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#include <array>
#include <cstdint>
#include <vector>
// NEON path. States 2j and 2j+1 feed new states j and j+32; both branch outputs flip when either the new bit or the oldest
// state bit flips (both generator polynomials have those taps), so one metric M_j = +/-sx +/- sy per butterfly is enough:
//   new[j]    = max(old[2j] + M, old[2j+1] - M)      new[j+32] = max(old[2j] - M, old[2j+1] + M)
struct NeonTables {
    int16_t sigX[32], sigY[32];
    NeonTables() {
        for (int j = 0; j < 32; j++) {
            const unsigned reg = (unsigned)(2 * j);
            sigX[j] = __builtin_parity(reg & 0x79) ? -1 : 1;
            sigY[j] = __builtin_parity(reg & 0x5B) ? -1 : 1;
        }
    }
};
const NeonTables& neonTables() { static NeonTables t; return t; }


void viterbiRange(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const NeonTables& T = neonTables();
    std::vector<uint64_t> dec(steps);
    long off = 0; // total subtracted by the renormalisation: the absolute path metric is off + pm
    alignas(16) int16_t pm[64] = {};
    int16x8_t sigX[4], sigY[4];
    for (int q = 0; q < 4; q++) { sigX[q] = vld1q_s16(T.sigX + 8 * q); sigY[q] = vld1q_s16(T.sigY + 8 * q); }
    // The 64 path metrics live in eight registers for the whole run (no store and reload per step: that round trip made the
    // chain of steps latency-bound). N[q] holds states 8q..8q+7.
    int16x8_t N[8];
    for (int q = 0; q < 8; q++) N[q] = vdupq_n_s16(0);
    static const uint8_t w8[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t wt = vld1q_u8(w8);
    for (size_t t = 0; t < steps; t++) {
        const int16_t a = soft[2 * t], b = soft[2 * t + 1];
        int16x8_t nn[8];
        uint16x8_t lo[4], hi[4];
        for (int q = 0; q < 4; q++) {
            const int16x8x2_t o = vuzpq_s16(N[2 * q], N[2 * q + 1]);   // even states, odd states
            const int16x8_t M = vmlaq_n_s16(vmulq_n_s16(sigX[q], a), sigY[q], b);
            const int16x8_t A = o.val[0], B = o.val[1];
            const int16x8_t p0 = vqaddq_s16(A, M), p1 = vqsubq_s16(B, M);
            const int16x8_t r0 = vqsubq_s16(A, M), r1 = vqaddq_s16(B, M);
            nn[q] = vmaxq_s16(p0, p1);
            nn[4 + q] = vmaxq_s16(r0, r1);
            lo[q] = vcgtq_s16(p1, p0);
            hi[q] = vcgtq_s16(r1, r0);
        }
        // the 64 decision bits (lane i of the 64 comparison results -> bit i): narrow to bytes, weight by 1,2,4..128 and add
        // neighbours three times, which leaves each group of eight lanes as one byte
        const uint8x16_t c0 = vandq_u8(vcombine_u8(vmovn_u16(lo[0]), vmovn_u16(lo[1])), wt);
        const uint8x16_t c1 = vandq_u8(vcombine_u8(vmovn_u16(lo[2]), vmovn_u16(lo[3])), wt);
        const uint8x16_t c2 = vandq_u8(vcombine_u8(vmovn_u16(hi[0]), vmovn_u16(hi[1])), wt);
        const uint8x16_t c3 = vandq_u8(vcombine_u8(vmovn_u16(hi[2]), vmovn_u16(hi[3])), wt);
        uint8x16_t r = vpaddq_u8(vpaddq_u8(c0, c1), vpaddq_u8(c2, c3));
        r = vpaddq_u8(r, r);
        dec[t] = vgetq_lane_u64(vreinterpretq_u64_u8(r), 0);
        // renormalise every 4 steps so int16 never saturates
        if ((t & 3) == 3) {
            int16x8_t mx = nn[0];
            for (int q = 1; q < 8; q++) mx = vmaxq_s16(mx, nn[q]);
            const int16_t top = vmaxvq_s16(mx);
            off += top;
            const int16x8_t m = vdupq_n_s16(top);
            for (int q = 0; q < 8; q++) N[q] = vsubq_s16(nn[q], m);
        } else for (int q = 0; q < 8; q++) N[q] = nn[q];
    }
    for (int q = 0; q < 8; q++) vst1q_s16(pm + 8 * q, N[q]);
    int state = 0;
    int bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#elif defined(__SSE2__) && !defined(DECT2_NO_SIMD)
// SSE2 path (every x86-64 CPU): the same butterflies and int16 metrics as the NEON path above, eight states per register.
struct SseTables {
    alignas(16) int16_t sigX[32], sigY[32];
    SseTables() {
        for (int j = 0; j < 32; j++) {
            const unsigned reg = (unsigned)(2 * j);
            sigX[j] = __builtin_parity(reg & 0x79) ? -1 : 1;
            sigY[j] = __builtin_parity(reg & 0x5B) ? -1 : 1;
        }
    }
};
const SseTables& sseTables() { static SseTables t; return t; }

// states 16q..16q+15: the even ones (old[2j]) to A, the odd ones (old[2j+1]) to B, like NEON's vld2q_s16
inline void deinterleave(const __m128i x0, const __m128i x1, __m128i& A, __m128i& B) {
    A = _mm_packs_epi32(_mm_srai_epi32(_mm_slli_epi32(x0, 16), 16), _mm_srai_epi32(_mm_slli_epi32(x1, 16), 16));
    B = _mm_packs_epi32(_mm_srai_epi32(x0, 16), _mm_srai_epi32(x1, 16));
}
// one bit per lane of a comparison result, lane i to bit i
inline uint8_t maskBits(__m128i m) { return (uint8_t)_mm_movemask_epi8(_mm_packs_epi16(m, _mm_setzero_si128())); }
inline int16_t maxLane(__m128i v) {
    v = _mm_max_epi16(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
    v = _mm_max_epi16(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1)));
    v = _mm_max_epi16(v, _mm_shufflelo_epi16(v, _MM_SHUFFLE(2, 3, 0, 1)));
    return (int16_t)_mm_cvtsi128_si32(v);
}

void viterbiRangeSse(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const SseTables& T = sseTables();
    std::vector<uint64_t> dec(steps);
    long off = 0; // total subtracted by the renormalisation: the absolute path metric is off + pm
    alignas(16) int16_t pm[64] = {};
    __m128i sigX[4], sigY[4];
    for (int q = 0; q < 4; q++) { sigX[q] = _mm_load_si128((const __m128i*)(T.sigX + 8 * q)); sigY[q] = _mm_load_si128((const __m128i*)(T.sigY + 8 * q)); }
    // the 64 path metrics stay in eight registers (N[q] = states 8q..8q+7) instead of a store and reload per step
    __m128i N[8];
    for (int q = 0; q < 8; q++) N[q] = _mm_setzero_si128();
    for (size_t t = 0; t < steps; t++) {
        const __m128i a = _mm_set1_epi16(soft[2 * t]), b = _mm_set1_epi16(soft[2 * t + 1]);
        __m128i nn[8];
        uint64_t d = 0;
        for (int q = 0; q < 4; q++) {
            __m128i A, B;
            deinterleave(N[2 * q], N[2 * q + 1], A, B);
            const __m128i M = _mm_add_epi16(_mm_mullo_epi16(sigX[q], a), _mm_mullo_epi16(sigY[q], b));
            const __m128i p0 = _mm_adds_epi16(A, M), p1 = _mm_subs_epi16(B, M);
            const __m128i r0 = _mm_subs_epi16(A, M), r1 = _mm_adds_epi16(B, M);
            nn[q] = _mm_max_epi16(p0, p1);
            nn[4 + q] = _mm_max_epi16(r0, r1);
            d |= (uint64_t)maskBits(_mm_cmpgt_epi16(p1, p0)) << (8 * q);
            d |= (uint64_t)maskBits(_mm_cmpgt_epi16(r1, r0)) << (32 + 8 * q);
        }
        dec[t] = d;
        // renormalise every 4 steps so int16 never saturates
        if ((t & 3) == 3) {
            __m128i mx = nn[0];
            for (int q = 1; q < 8; q++) mx = _mm_max_epi16(mx, nn[q]);
            const int16_t top = maxLane(mx);
            off += top;
            const __m128i m = _mm_set1_epi16(top);
            for (int q = 0; q < 8; q++) N[q] = _mm_sub_epi16(nn[q], m);
        } else for (int q = 0; q < 8; q++) N[q] = nn[q];
    }
    for (int q = 0; q < 8; q++) _mm_store_si128((__m128i*)(pm + 8 * q), N[q]);
    int state = 0;
    int bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#ifdef DECT2_VIT_AVX2
// The same trellis with 256-bit registers (16 states each): identical decisions and metrics, half the instructions.
__attribute__((target("avx2")))
inline void deinterleave256(const __m256i x0, const __m256i x1, __m256i& A, __m256i& B) {
    // packs works within the 128-bit halves, so the quarters come out as 0 2 1 3 and are put back in order
    const __m256i a = _mm256_packs_epi32(_mm256_srai_epi32(_mm256_slli_epi32(x0, 16), 16), _mm256_srai_epi32(_mm256_slli_epi32(x1, 16), 16));
    const __m256i b = _mm256_packs_epi32(_mm256_srai_epi32(x0, 16), _mm256_srai_epi32(x1, 16));
    A = _mm256_permute4x64_epi64(a, 0xD8);
    B = _mm256_permute4x64_epi64(b, 0xD8);
}
// the comparison results of two registers (32 lanes) as 32 bits, lane i to bit i
__attribute__((target("avx2")))
inline uint32_t maskBits256(__m256i m0, __m256i m1) {
    return (uint32_t)_mm256_movemask_epi8(_mm256_permute4x64_epi64(_mm256_packs_epi16(m0, m1), 0xD8));
}

__attribute__((target("avx2")))
void viterbiRangeAvx2(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const SseTables& T = sseTables();
    std::vector<uint64_t> dec(steps);
    long off = 0;
    alignas(32) int16_t pm[64] = {};
    const __m256i sigX0 = _mm256_loadu_si256((const __m256i*)T.sigX), sigX1 = _mm256_loadu_si256((const __m256i*)(T.sigX + 16));
    const __m256i sigY0 = _mm256_loadu_si256((const __m256i*)T.sigY), sigY1 = _mm256_loadu_si256((const __m256i*)(T.sigY + 16));
    __m256i N0 = _mm256_setzero_si256(), N1 = N0, N2 = N0, N3 = N0;   // states 0-15, 16-31, 32-47, 48-63
    for (size_t t = 0; t < steps; t++) {
        const __m256i a = _mm256_set1_epi16(soft[2 * t]), b = _mm256_set1_epi16(soft[2 * t + 1]);
        __m256i A0, B0, A1, B1;
        deinterleave256(N0, N1, A0, B0);   // old states 0-31: new states 0-15 and 32-47
        deinterleave256(N2, N3, A1, B1);   // old states 32-63: new states 16-31 and 48-63
        const __m256i M0 = _mm256_add_epi16(_mm256_mullo_epi16(sigX0, a), _mm256_mullo_epi16(sigY0, b));
        const __m256i M1 = _mm256_add_epi16(_mm256_mullo_epi16(sigX1, a), _mm256_mullo_epi16(sigY1, b));
        const __m256i p00 = _mm256_adds_epi16(A0, M0), p10 = _mm256_subs_epi16(B0, M0);
        const __m256i r00 = _mm256_subs_epi16(A0, M0), r10 = _mm256_adds_epi16(B0, M0);
        const __m256i p01 = _mm256_adds_epi16(A1, M1), p11 = _mm256_subs_epi16(B1, M1);
        const __m256i r01 = _mm256_subs_epi16(A1, M1), r11 = _mm256_adds_epi16(B1, M1);
        __m256i n0 = _mm256_max_epi16(p00, p10), n1 = _mm256_max_epi16(p01, p11);
        __m256i n2 = _mm256_max_epi16(r00, r10), n3 = _mm256_max_epi16(r01, r11);
        dec[t] = (uint64_t)maskBits256(_mm256_cmpgt_epi16(p10, p00), _mm256_cmpgt_epi16(p11, p01))
               | (uint64_t)maskBits256(_mm256_cmpgt_epi16(r10, r00), _mm256_cmpgt_epi16(r11, r01)) << 32;
        if ((t & 3) == 3) {
            __m256i mx = _mm256_max_epi16(_mm256_max_epi16(n0, n1), _mm256_max_epi16(n2, n3));
            const int16_t top = maxLane(_mm_max_epi16(_mm256_castsi256_si128(mx), _mm256_extracti128_si256(mx, 1)));
            off += top;
            const __m256i m = _mm256_set1_epi16(top);
            n0 = _mm256_sub_epi16(n0, m); n1 = _mm256_sub_epi16(n1, m); n2 = _mm256_sub_epi16(n2, m); n3 = _mm256_sub_epi16(n3, m);
        }
        N0 = n0; N1 = n1; N2 = n2; N3 = n3;
    }
    _mm256_store_si256((__m256i*)pm, N0); _mm256_store_si256((__m256i*)(pm + 16), N1);
    _mm256_store_si256((__m256i*)(pm + 32), N2); _mm256_store_si256((__m256i*)(pm + 48), N3);
    int state = 0;
    int bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#endif

void viterbiRange(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
#ifdef DECT2_VIT_AVX2
    static const bool avx2 = __builtin_cpu_supports("avx2");
    if (avx2) return viterbiRangeAvx2(soft, steps, bits, metricOut);
#endif
    viterbiRangeSse(soft, steps, bits, metricOut);
}
#else
void viterbiRange(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const Trellis& T = trellis();
    std::vector<uint64_t> dec(steps);
    long off = 0;
    int32_t pm[64] = {}, nm[64];
    for (size_t t = 0; t < steps; t++) {
        const int sx = soft[2 * t], sy = soft[2 * t + 1];
        uint64_t d = 0;
        for (int ns = 0; ns < 64; ns++) {
            const int p0 = ((ns & 31) << 1), p1 = p0 | 1;
            const int m0 = pm[p0] + T.sx[ns][0] * sx + T.sy[ns][0] * sy;
            const int m1 = pm[p1] + T.sx[ns][1] * sx + T.sy[ns][1] * sy;
            if (m1 > m0) { nm[ns] = m1; d |= 1ull << ns; } else nm[ns] = m0;
        }
        int best = nm[0];
        for (int s = 1; s < 64; s++) best = std::max(best, nm[s]);
        off += best;
        for (int s = 0; s < 64; s++) pm[s] = nm[s] - best;
        dec[t] = d;
    }
    int state = 0, bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#endif
} // namespace

void Viterbi::decode(const std::vector<int8_t>& soft, std::vector<uint8_t>& out, int threads, long* metric) {
    const size_t steps = soft.size() / 2;
    out.assign(steps, 0);
    const size_t C = 16384, L = 192;
    const size_t chunks = (steps + C - 1) / C;
    std::vector<long> metrics(chunks, 0);
    auto work = [&](size_t ci) {
        const size_t a = ci * C, b = std::min(steps, a + C);
        const size_t lo = a > L ? a - L : 0, hi = std::min(steps, b + L);
        std::vector<uint8_t> bits;
        long mt = 0;
        viterbiRange(soft.data() + 2 * lo, hi - lo, bits, &mt);
        metrics[ci] = mt;
        for (size_t t = a; t < b; t++) out[t] = bits[t - lo];
    };
    if (threads <= 1 || chunks <= 1) { for (size_t c = 0; c < chunks; c++) work(c); }
    else {
        std::atomic<size_t> next{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < std::min<size_t>(threads, chunks); t++) ts.emplace_back([&] { for (size_t c; (c = next++) < chunks;) work(c); });
        for (auto& t : ts) t.join();
    }
    if (metric) { long s = 0; for (long m : metrics) s += m; *metric = s; }
}

namespace {
// products with alpha^i for every byte, so that a syndrome is one table lookup and one exclusive or per byte
struct RsMulTab {
    uint8_t t[16][256];
    RsMulTab() { const Gf& f = gf(); for (int i = 0; i < 16; i++) for (int v = 0; v < 256; v++) t[i][v] = f.mul((uint8_t)v, f.exp[i]); }
};
const RsMulTab& rsMulTab() { static RsMulTab m; return m; }
inline bool rsSyndromes(const uint8_t* r, uint8_t* S) {
    // the remainder of r(x) modulo g(x) is zero exactly when every syndrome is, and S_i = rem(alpha^i)
    const RsRows& R = rsRows();
    const RsMulTab& m = rsMulTab();
    uint8_t b[204 + 16 + 8] = {};
    memcpy(b, r, 204);
    for (int i = 0; i < 188; i++) {
        const uint8_t fb = b[i];
        if (!fb) continue;
        uint64_t x[2], y[2];
        memcpy(x, b + i + 1, 16); memcpy(y, R.row[fb], 16);
        x[0] ^= y[0]; x[1] ^= y[1];
        memcpy(b + i + 1, x, 16);
    }
    const uint8_t* rem = b + 188;
    uint8_t any = 0;
    for (int j = 0; j < 16; j++) any |= rem[j];
    if (!any) { memset(S, 0, 16); return false; }
    for (int i = 0; i < 16; i++) { uint8_t s = 0; for (int j = 0; j < 16; j++) s = m.t[i][s] ^ rem[j]; S[i] = s; }
    return true;
}
}

int rsDecode(uint8_t* r) {
    const Gf& f = gf();
    // syndromes S_i = r(alpha^i), r as polynomial with byte 0 the highest degree (203)
    uint8_t S[16];
    if (!rsSyndromes(r, S)) return 0;
    // Berlekamp-Massey
    uint8_t C[17] = {1}, B[17] = {1};
    int L = 0, m = 1;
    uint8_t b = 1;
    for (int n = 0; n < 16; n++) {
        uint8_t d = S[n];
        for (int i = 1; i <= L; i++) d ^= f.mul(C[i], S[n - i]);
        if (d == 0) { m++; continue; }
        uint8_t T[17];
        memcpy(T, C, 17);
        const uint8_t coef = f.mul(d, f.inv(b));
        for (int i = 0; i + m < 17; i++) C[i + m] ^= f.mul(coef, B[i]);
        if (2 * L <= n) { L = n + 1 - L; memcpy(B, T, 17); b = d; m = 1; } else m++;
    }
    if (L > 8) return -1;
    // Chien search: error at degree p (position j = 203 - p) when C(alpha^{-p}) = 0
    int pos[8], cnt = 0;
    for (int p = 0; p < 204; p++) {
        const uint8_t x = f.exp[(255 - p) % 255];       // alpha^{-p}
        uint8_t v = 0;
        for (int i = L; i >= 0; i--) v = f.mul(v, x) ^ C[i];
        if (v == 0) { if (cnt >= 8) return -1; pos[cnt++] = p; }
    }
    if (cnt != L) return -1;
    // error evaluator Omega(x) = S(x) C(x) mod x^16, Forney with fcr = 0: e = X * Omega(X^-1) / C'(X^-1)
    uint8_t om[16] = {};
    for (int i = 0; i < 16; i++) { uint8_t s = 0; for (int j = 0; j <= std::min(i, L); j++) s ^= f.mul(C[j], S[i - j]); om[i] = s; }
    for (int k = 0; k < cnt; k++) {
        const int p = pos[k];
        const uint8_t X = f.exp[p % 255], xi = f.exp[(255 - p) % 255];
        uint8_t num = 0;
        for (int i = 15; i >= 0; i--) num = f.mul(num, xi) ^ om[i];
        uint8_t den = 0;                                  // C'(x) = sum over odd i of C[i] x^{i-1}
        for (int i = 1; i <= L; i += 2) den ^= f.mul(C[i], f.exp[((255 - p) * (i - 1)) % 255]);
        if (den == 0) return -1;
        const uint8_t e = f.mul(f.mul(num, X), f.inv(den));
        r[203 - p] ^= e;
    }
    // verify
    uint8_t S2[16];
    if (rsSyndromes(r, S2)) return -1;
    return cnt;
}

void descramble(uint8_t* packets, size_t count, int first) {
    const DispersalSeq& D = dispersalSeq();
    bool synced = false;   // the generator register starts at zero, which gives a zero sequence until the first packet of a group of eight
    for (size_t p = 0; p < count; p++) {
        const int g = (int)((first + p) % 8);
        if (g == 0) synced = true;
        uint8_t* b = packets + p * 188;
        b[0] = 0x47;
        if (synced) { const uint8_t* q = D.b[g]; for (int k = 1; k < 188; k++) b[k] ^= q[k]; }
    }
}

} // namespace dvbt
} // namespace dect2
