// DVB-T (EN 300 744): parameters, pilot/TPS tables, TPS coding and the channel-coding chain.
// The transmit-side stages exist for the signal generator and for tests against the GNU Radio reference; the receive-side
// stages (demapper, de-interleavers, Viterbi, Reed-Solomon, de-randomiser) are what the receiver uses.
#pragma once
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace dect2 {
namespace dvbt {

using cf32 = std::complex<float>;

enum Mode { k2K = 0, k8K = 1 };
enum Guard { kGi32 = 0, kGi16 = 1, kGi8 = 2, kGi4 = 3 };
enum Mod { kQpsk = 0, k16Qam = 1, k64Qam = 2 };
enum Rate { kR12 = 0, kR23 = 1, kR34 = 2, kR56 = 3, kR78 = 4 };

struct Params {
    int mode = k2K;
    int guard = kGi32;
    int mod = k64Qam;
    int hier = 0;            // 0 non-hierarchical, 1/2/3 = alpha 1/2/4
    int crHp = kR23;
    int crLp = kR23;
    int cellId = 0;
    bool cellIdLength = false; // TPS length indicator 0x1F (cell id present) instead of 0x17
    bool operator==(const Params& o) const { return mode == o.mode && guard == o.guard && mod == o.mod && hier == o.hier && crHp == o.crHp && crLp == o.crLp; }
};

// ---- geometry
inline int fftN(int mode) { return mode == k8K ? 8192 : 2048; }
inline int carriersK(int mode) { return mode == k8K ? 6817 : 1705; }           // Kmax - Kmin + 1
inline int dataCarriers(int mode) { return mode == k8K ? 6048 : 1512; }
inline int guardSamples(int mode, int guard) { return fftN(mode) >> (5 - guard); } // N/32, N/16, N/8, N/4
inline int symbolsPerFrame() { return 68; }
// Useful (MPEG transport stream) bit rate of a configuration, for an OFDM sampling rate fs (Hz)
inline double netBitrate(const Params& p, double fs) {
    static const double rate[5] = {1.0 / 2, 2.0 / 3, 3.0 / 4, 5.0 / 6, 7.0 / 8};
    const double symSec = (double)(fftN(p.mode) + guardSamples(p.mode, p.guard)) / fs;
    return dataCarriers(p.mode) * (2.0 * (p.mod + 1)) * rate[p.crHp] * (188.0 / 204.0) / symSec;
}
inline int bitsPerCell(int mod) { return 2 * (mod + 1); }
inline int alphaOf(int hier) { return hier == 2 ? 2 : hier == 3 ? 4 : 1; }
const char* guardName(int g);
const char* modName(int m);
const char* rateName(int r);

// ---- carrier roles
const std::vector<int>& continualPilots(int mode);
const std::vector<int>& tpsCarriers(int mode);
const std::vector<uint8_t>& prbsW();                   // w_k, k = 0..6816
inline bool isScatteredPilot(int k, int symIdx) { return (k - 3 * (symIdx % 4)) % 12 == 0 && k >= 3 * (symIdx % 4); }
// kind of every carrier of a symbol: 0 data, 1 scattered, 2 continual, 3 TPS
void carrierRoles(int mode, int symIdx, std::vector<uint8_t>& roles);
// pilot value (real) of carrier k: 4/3 * 2 * (1/2 - w_k)
inline float pilotValue(int k) { return (prbsW()[k] ? -1.f : 1.f) * 4.f / 3.f; }

// ---- TPS
// Bits s0..s67 of the TPS block for a frame (frameIdx 0..3 inside the superframe)
std::array<uint8_t, 68> tpsBits(const Params& p, int frameIdx);
// Validates a received 68-bit block (sync word, BCH) and extracts the parameters. `oddFrame` tells which sync word matched.
// maxFix: how many bit errors (0..2) the BCH code may correct. 0 for searching a frame start, 2 once the frame is known.
bool tpsDecode(const uint8_t bits[68], Params& p, int& frameIdx, bool& oddSyncWord, int maxFix = 0);
bool tpsSync(const uint8_t* bits /*s1..s16*/, bool& odd);

// ---- constellation (label -> point) for the given alpha (1, 2, 4); unit average power for alpha = 1, normalised for others
void constellation(int mod, int hier, std::vector<cf32>& points);

// ---- transmit chain (reference semantics)
void scramble(const uint8_t* ts, size_t packets, uint8_t* out);               // energy dispersal, groups of 8 packets, 188 bytes each
void rsEncode(const uint8_t* in188, uint8_t* out204);
class ConvInterleaver {                                                       // Forney, I = 12, M = 17 (de-interleaver when `inverse`)
public:
    explicit ConvInterleaver(bool inverse = false);
    void process(const uint8_t* in, uint8_t* out, size_t n);                  // n a multiple of 12
private:
    bool inverse_;
    std::vector<std::vector<uint8_t>> fifo_;
    std::vector<size_t> pos_;
};
class InnerEncoder {                                                          // convolutional code (171,133) with puncturing
public:
    explicit InnerEncoder(int rate) : rate_(rate) {}
    void setPhase(size_t step) { step_ = step; }   // start inside the puncturing pattern (tests the receiver's phase search)
    // `bits` are info bits (one per byte); returns punctured coded bits (one per byte)
    void encode(const std::vector<uint8_t>& bits, std::vector<uint8_t>& coded);
private:
    int rate_;
    unsigned reg_ = 0;
    size_t step_ = 0;   // position inside the puncturing pattern, kept across calls so the stream is continuous
};
// bit interleaver (126-word blocks) and symbol interleaver permutation, non-hierarchical
void bitInterleave(const std::vector<uint8_t>& codedBits, int mod, std::vector<uint8_t>& words); // words hold v bits (MSB first)
const std::vector<int>& symbolPermutation(int mode);                                              // H(q), q = 0..dataCarriers-1
void mapSymbol(const std::vector<uint8_t>& words, int mod, int hier, std::vector<cf32>& cells);

// ---- receive chain
// Bit LLR demapper for one symbol's data cells (noise variance per cell): llr[cell*v + b], b = 0 is the MSB; llr > 0 means bit 0
void demap(const cf32* cells, const float* n0, int count, int mod, int hier, float* llr);
// Undo the symbol interleaver on cells (and on their noise values)
void symbolDeinterleave(int mode, int symIdx, const cf32* in, const float* n0in, cf32* out, float* n0out);
// Undo the bit interleaver on LLRs of one symbol (1512 / 6048 words of v LLRs each): llr in = demapper order
void bitDeinterleave(const float* llrIn, int mod, int words, float* llrOut);
// Hierarchical modes (4.3.4.1): the HP stream (2 bits a word) goes through branches I0, I1, the LP stream (v - 2 bits a word) through I2..Iv-1
// (16-QAM: x''0 -> I2, x''1 -> I3; 64-QAM: x''0 -> I2, x''1 -> I4, x''2 -> I3, x''3 -> I5)
void bitInterleaveHier(const std::vector<uint8_t>& hpCoded, const std::vector<uint8_t>& lpCoded, int mod, std::vector<uint8_t>& words);
// The LP stream back out of one symbol's demapper LLRs (v per cell): (v - 2) LLRs per word, in coded order
void bitDeinterleaveLp(const float* llrIn, int mod, int words, float* llrOut);

class Viterbi {
public:
    // Decodes a stream of depunctured soft values. Output is one decoded bit per byte.
    // `soft` holds pairs (x, y) per trellis step with 0 for punctured positions.
    static void decode(const std::vector<int8_t>& soft, std::vector<uint8_t>& bits, int threads = 4, long* pathMetric = nullptr);
    // Depuncture LLRs (floats, > 0 = bit 0) for a code rate; `phase` rotates the puncturing pattern.
    static void depuncture(const float* llr, size_t n, int rate, int phase, std::vector<int8_t>& soft, float scale);
};

// Reed-Solomon (204,188,t=8): returns the number of corrected bytes, or -1 if uncorrectable
int rsDecode(uint8_t* block204);
// De-randomise a stream of 204-byte-aligned (after RS) 188-byte packets starting at a packet whose sync byte is 0xB8
void descramble(uint8_t* packets, size_t count, int firstIndexInGroup);

} // namespace dvbt
} // namespace dect2
