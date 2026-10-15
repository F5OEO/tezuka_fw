// DVB-S2 (EN 302 307-1) and DVB-S2X (EN 302 307-2) building blocks shared by the transmitter (test signal), the receiver and the tests: code
// parameters, MODCOD table, BBHEADER, BB scrambler, bit interleaver, constellations, soft demapper, PLHEADER and PL scrambling.
// Clause numbers refer to ETSI EN 302 307 V1.3.1 unless they name EN 302 307-2 (V1.2.1).
#pragma once
#include "ldpc.h"
#include "ring.h"
#include "t2fec.h"
#include <bit>
#include <complex>
#include <cstdint>
#include <vector>

namespace dect2 {
namespace dvbs {

// ---- code rates, in the order of the LDPC tables
constexpr int kS2Rates = 11;                       // 1/4 1/3 2/5 1/2 3/5 2/3 3/4 4/5 5/6 8/9 9/10
// DVB-S2X: every MODCOD of EN 302 307-2 table 17a has a rate index of its own, kS2Rates + its row in that table (QPSK 13/45 normal first, 32APSK
// 32/45 short last). That one index fixes the LDPC code, the frame size, the constellation and the bit interleaver, so the functions below take it
// like a DVB-S2 rate. An S2X rate goes with its own frame size only (s2Dims is not ok with the other one).
constexpr int kS2xModcods = 55;
inline bool s2IsS2x(int rate) { return rate >= kS2Rates && rate < kS2Rates + kS2xModcods; }
const char* s2RateName(int rate);                  // S2X: the canonical code rate of table 17a ("5/9-L", "77/90", ...)
double s2RateValue(int rate);                      // k/n as a number (1/4 ... 9/10)
enum S2Mod { kQpsk = 0, k8psk = 1, k16apsk = 2, k32apsk = 3, k64apsk = 4, k128apsk = 5, k256apsk = 6 };   // 64APSK and up: DVB-S2X only
constexpr int kS2Mods = 7;
const char* s2ModName(int mod);
const char* s2ModNameFor(int mod, int rate);       // the canonical name: "8APSK" for the S2X 8-point MODCODs that are not 8PSK, else s2ModName
inline int s2BitsPerSymbol(int mod) { return mod + 2; }
// S2X MODCODs of one modulation and frame size, in the order of table 17a: how many, and the rate index of the nth (0-based; -1 when there is none)
int s2xRateCount(int mod, bool shortFrame);
int s2xRate(int mod, bool shortFrame, int nth);
const char* s2xCodeName(int rate);                 // the LDPC code identifier of an S2X rate ("100/180", "2/3", ...), "" for an S2 rate
// The rate index that a modulation, a code rate name (s2RateName) and a frame size stand for, S2 first; -1 when there is none
int s2RateFromName(int mod, const char* name, bool shortFrame);

// ---- code dimensions (tables 5a, 5b, 7a, 7b)
struct S2Dims {
    bool ok = false;           // false: this combination does not exist (short 9/10, a modulation without code rates, ...)
    int nldpc = 0, kldpc = 0;  // LDPC code; kldpc = BCH coded block
    int kbch = 0, t = 0;       // BCH uncoded block and its error correction
    int q = 0;
    int bitsPerSym = 0;
    int xfecSymbols = 0;       // symbols of the XFECFRAME (128APSK: 6 bits of padding after the code word and 84 after the interleaver, EN 302 307-2 5.3.3)
    int slots = 0;             // S, table 11 (EN 302 307-2 table 16)
};
S2Dims s2Dims(int mod, int rate, bool shortFrame);
// PLFRAME length in symbols: PLHEADER, slots and pilot blocks (clause 5.5)
int s2FrameSymbols(int mod, bool shortFrame, bool pilots);
inline int s2FrameSymbols(const S2Dims& d, bool pilots) { return 90 * (d.slots + 1) + (pilots ? 36 * ((d.slots - 1) / 16) : 0); }

// Es/N0 in dB for quasi error free reception over AWGN (table 13, ideal demodulator, 50 LDPC iterations); short frames: 0.25 dB more (the standard
// says "an additional degradation of 0,2 dB to 0,3 dB"). S2X: EN 302 307-2 tables 20a and 20c. 99 when the combination does not exist.
double s2QefEsN0(int mod, int rate, bool shortFrame);

// ---- MODCOD (table 12): 1..28 are the modulation and code rate pairs, 0 is a dummy PLFRAME. DVB-S2X (EN 302 307-2 table 17a): 64..127, the PLS
// code value divided by two (66 = PLS code 132 = QPSK 13/45, ..., 124 = PLS code 248 = 32APSK 32/45 short); the frame size is part of the MODCOD.
int s2Modcod(int mod, int rate);                   // -1 when there is no such MODCOD
bool s2ModcodSplit(int modcod, int& mod, int& rate);
inline bool s2ModcodIsS2x(int modcod) { return modcod >= 64 && modcod < 128; }

// ---- codes. The LDPC tables are the DVB-S2 ones (they differ from DVB-T2 for normal 2/3 and short 3/5). The BCH code is shared with DVB-T2.
const LdpcCode& s2Ldpc(int rate, bool shortFrame);
const BchCode& s2Bch(int rate, bool shortFrame);
// Normalisation factor of the min-sum decoder for this code rate. The T2 default (0.78) fails on the low rates (1/4 to 2/5), which is why it
// is chosen per rate: measured with tests/test_dvbs_s2fec.cpp-style sweeps, within about 0.3 dB of the Es/N0 of table 13.
float s2LdpcAlpha(int rate);
// BBFRAME (kbch bits, already scrambled) -> FECFRAME (nldpc bits, before the bit interleaver)
void s2EncodeFec(const std::vector<uint8_t>& bb, int rate, bool shortFrame, std::vector<uint8_t>& fec);
// Hard bits of an LDPC codeword (nldpc) -> BCH decoded BBFRAME (kbch bits). Returns the number of corrected bits, or -1.
int s2BchDecode(const uint8_t* hard, int rate, bool shortFrame, std::vector<uint8_t>& bb);

// ---- bit interleaver (clause 5.3.3), all but QPSK: written by columns, read by rows
// cols[j] = column that carries label bit j (0 = MSB of the label); S2X: the bit interleaver pattern of EN 302 307-2 tables 9a and 9b
void s2InterleavePattern(int mod, int rate, int cols[8]);
// FECFRAME (nldpc bits) -> the bits of the XFECFRAME in symbol order (xfecSymbols * bits per symbol: 128APSK carries its padding)
void s2BitInterleave(const std::vector<uint8_t>& in, int mod, int rate, std::vector<uint8_t>& out);
// LLRs of label bits in symbol order (the whole XFECFRAME) -> LLRs of the n = nldpc code word bits in codeword order
void s2BitDeinterleaveLlr(const float* in, int n, int mod, int rate, float* out);

// ---- constellations (clause 5.4; S2X: EN 302 307-2 clause 5.4). Index = label, bit 0 (MSB) first. Unit average symbol energy.
const cf32* s2Constellation(int mod, int rate);
int s2ConstellationSize(int mod);
// Maps a FECFRAME (after the bit interleaver) to symbols, nbits / bits per symbol of them
void s2MapBits(const uint8_t* bits, int nbits, int mod, int rate, cf32* out);
// Max-log demapper. sigma2 is the noise variance per real dimension. llr[i * m + b] > 0 means bit 0.
void s2Demap(const cf32* sym, int n, int mod, int rate, float sigma2, float* llr);

// ---- BBHEADER (clause 5.1.6)
struct S2BbHeader {
    int tsGs = 3;              // 3 transport stream, 0 generic packetized, 1 generic continuous, 2 reserved
    bool sis = true;           // single input stream
    bool ccm = true;           // constant coding and modulation (false: ACM or VCM)
    bool issyi = false, npd = false;
    int ro = 0;                // 0: 0.35, 1: 0.25, 2: 0.20, 3: reserved
    int isi = 0;
    int upl = 188 * 8, dfl = 0, sync = 0x47, syncd = 0;
    bool crcOk = false;
};
void s2BuildBbHeader(const S2BbHeader& h, uint8_t* bits80);       // one bit per byte
bool s2ParseBbHeader(const uint8_t* bits80, S2BbHeader& h);        // false when the CRC-8 does not match
uint8_t s2Crc8(const uint8_t* bytes, int n);                       // g(X) = X^8+X^7+X^6+X^4+X^2+1, clause 5.1.4
// BB scrambling (clause 5.2.2): XOR in place with the PRBS 1 + X^14 + X^15, which restarts at every BBFRAME
void s2BbScramble(uint8_t* bits, int n);

// ---- physical layer framing (clause 5.5)
constexpr uint32_t kSof = 0x18D2E82;               // 26 bits
// The PLS code value: the 8 signalling bits b0..b7 of EN 302 307-2 clause 5.5.2 as a number, b0 the MSB. DVB-S2 (b0 = 0): MODCOD << 2 | short << 1 |
// pilots. DVB-S2X (b0 = 1): MODCOD (64..127) << 1 | pilots, the frame size follows from the MODCOD. Split is the reverse (S2X: modcod = code >> 1,
// pilots = the LSB, short from table 17a).
int s2PlsValue(int modcod, bool shortFrame, bool pilots);
void s2PlsSplit(int code, int& modcod, bool& shortFrame, bool& pilots);
// What a PLS code value announces: 0 a data frame this receiver decodes, 1 a dummy frame, 2 a frame of known length that is not decoded here (S2X
// VL-SNR frames, the reserved values of table 17b, whose LSB is not a pilot flag), -1 nothing (the reserved S2 MODCODs 29..31, short 9/10)
int s2PlsKind(int code);
// PLFRAME length in symbols that a PLS code value announces (header included), 0 when it announces nothing
int s2PlsFrameSymbols(int code);
// 64 coded and scrambled PLS bits for MODCOD, short frame, pilots. The PLS code is a (64,8) code (EN 302 307-2 clause 5.5.2.4; its b0 = 0 half is
// the (64,7) code of DVB-S2).
uint64_t s2PlsCode(int modcod, bool shortFrame, bool pilots);      // bit 63 is the first transmitted bit
// The 90 PLHEADER symbols. Symbol j uses the pi/2 BPSK rule of clause 5.5.2; after the SOF an S2X header (b0 = 1) is turned by 90 degrees
// (EN 302 307-2 clause 5.5.2)
void s2PlHeader(int modcod, bool shortFrame, bool pilots, cf32* out90);
const cf32* s2SofSymbols();                        // the 26 symbols of the SOF
// Decodes the PLS from 64 symbols that are close to the transmitted ones (carrier phase and frequency already corrected).
// Correlates against all 256 codewords. Returns the best score (1 = perfect) and the runner-up in `second`; `code` is the PLS code value.
struct PlsResult { int modcod = 0; bool shortFrame = false, pilots = false; float score = 0, second = 0; int code = 0; };
PlsResult s2PlsDecode(const cf32* sym64);
// The same correlation for one given code word (modcod, short, pilots), 1 = perfect
float s2PlsScore(const cf32* sym64, int modcod, bool shortFrame, bool pilots);
// pi/2 BPSK symbol for header bit y at position j (0-based) in the header
inline cf32 s2Bpsk(int j, int y) {
    const float a = (1 - 2 * y) * 0.70710678f;
    return (j & 1) ? cf32(-a, a) : cf32(a, a);
}
// Physical layer scrambling sequence (clause 5.5.4), Gold code number n: the symbol i is multiplied by exp(j R(i) pi/2).
// Returns R(i) in 0..3 for i < count.
const std::vector<uint8_t>& s2ScramblingRn(int n);
inline cf32 s2RotateByR(cf32 s, int r) {   // s * exp(j r pi/2), without a branch: r is random, so a jump table here mispredicts most of the time
    r &= 3;
    const float re = s.real(), im = s.imag();
    const bool odd = r & 1;
    const float a = odd ? im : re, b = odd ? re : im;
    // sign flips: real part for r = 1, 2; imaginary part for r = 2, 3
    const uint32_t sr = (uint32_t)(((r + 1) >> 1) & 1) << 31, si = (uint32_t)((r >> 1) & 1) << 31;
    return cf32(std::bit_cast<float>(std::bit_cast<uint32_t>(a) ^ sr), std::bit_cast<float>(std::bit_cast<uint32_t>(b) ^ si));
}

} // namespace dvbs
} // namespace dect2
