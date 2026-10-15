// DVB-T2 data-path FEC and mapping: BCH, LDPC, bit interleaver, constellations (with rotation and cyclic Q delay),
// cell / time interleaver, BB scrambler. Encoder and decoder directions share the same tables.
#pragma once
#include <mutex>
#include "ldpc.h"
#include "ring.h"
#include <cstdint>
#include <vector>

namespace dect2 {

struct PlpFec {
    bool shortFrame = false; // false = 64800 bit frames, true = 16200
    int rate = 2;            // PLP_COD: 0 1/2, 1 3/5, 2 2/3, 3 3/4, 4 4/5, 5 5/6, 6 1/3, 7 2/5
    int mod = 2;             // PLP_MOD: 0 QPSK, 1 16QAM, 2 64QAM, 3 256QAM
    bool rotation = false;   // rotated constellation (with cyclic Q delay)
};

struct FecDims {
    bool ok = false;
    int nLdpc = 0, kLdpc = 0, kBch = 0, t = 0;
    int bitsPerCell = 0, cellsPerBlock = 0, q = 0;
};
FecDims fecDims(const PlpFec& f);
const char* rateName(int rate);

const LdpcCode& ldpcFor(const PlpFec& f);

// ---- BCH (binary, primitive, shortened)
class BchCode {
public:
    BchCode(bool shortFrame, int t);
    int parityBits() const { return deg_; }
    // bits[0..k) hold the message; parity is appended so that bits has k + parityBits() entries
    void encode(std::vector<uint8_t>& bits, int k) const;
    // Corrects up to t errors in place. Returns the number of corrected bits, or -1 if uncorrectable.
    int decode(std::vector<uint8_t>& bits) const;

private:
    int m_, t_, deg_;
    std::vector<uint32_t> exp_, log_;
    std::vector<uint8_t> gen_; // generator polynomial, gen_[i] = coefficient of x^i
    mutable std::vector<uint64_t> tab_; // byte-wise remainder table (built on first use)
    mutable std::mutex tabMu_;
    int fieldMul(int a, int b) const;
    void remainder(const std::vector<uint8_t>& bits, std::vector<uint8_t>& rem) const;
};
const BchCode& bchFor(const PlpFec& f);

// ---- bit interleaver: map[p] = index (in the LDPC codeword) of the bit carried by label bit p
// (p = cell * bitsPerCell + position, position 0 = MSB of the cell label)
const std::vector<int>& bitInterleaverMap(const PlpFec& f);

// ---- constellations
cf32 qamPoint(int mod, bool rotated, unsigned label);       // (possibly rotated) point, unit average power
// Cells for one FEC block from labels (applies the cyclic Q delay when rotated)
void qamMapBlock(const PlpFec& f, const std::vector<uint16_t>& labels, std::vector<cf32>& cells);
// Soft demapping of a FEC block. `n0[i]` is the noise variance per real dimension of equalised cell i.
// Output llr has cells*bitsPerCell entries in label-bit order (positive = bit 0).
void qamDemapBlock(const PlpFec& f, const cf32* cells, const float* n0, int nCells, float* llr);
// Exact (slow) version over the whole constellation, used as a reference in tests.
void qamDemapBlockExact(const PlpFec& f, const cf32* cells, const float* n0, int nCells, float* llr);

// ---- cell / time interleaver (TIME_IL_TYPE 0: one interleaving frame per T2 frame)
// `blocks` FEC blocks of cellsPerBlock cells, split into `tiBlocks` time-interleaver blocks (0 = no time interleaving)
void cellInterleave(const PlpFec& f, int blocks, int tiBlocks, const std::vector<cf32>& in, std::vector<cf32>& out);
// Inverse. Generic over the element type (cells or per-cell noise values).
void cellDeinterleave(const PlpFec& f, int blocks, int tiBlocks, const cf32* in, cf32* out);
void cellDeinterleaveF(const PlpFec& f, int blocks, int tiBlocks, const float* in, float* out);

// ---- baseband framing
extern const uint8_t* bbRandomiser(); // 64800 bits
uint8_t crc8Bits(const uint8_t* bits, int n);

struct BbHeader {
    int tsGs = 3, sisMis = 1, ccmAcm = 1, issyi = 0, npd = 0, ro = 0, isi = 0;
    int upl = 1504, dfl = 0, sync = 0x47, syncd = 0;
    bool crcOk = false;
    bool hem = false;      // high-efficiency mode: CRC-8 is XORed (LSB); UPL/SYNC carry ISSY instead
    unsigned issy = 0;     // 24-bit ISSY (counter) in HEM
};
bool parseBbHeader(const uint8_t* bits80, BbHeader& h); // bits are descrambled, one per byte
void buildBbHeader(const BbHeader& h, uint8_t* bits80);

} // namespace dect2
