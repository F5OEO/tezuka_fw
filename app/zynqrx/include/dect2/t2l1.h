// DVB-T2 L1 signalling: L1-pre / L1-post structures, bit packing, FEC chain (CRC-32, BCH, LDPC, puncturing,
// interleaving, mapping) and P2 cell distribution. Encoder and decoder share the same tables.
#pragma once
#include "ring.h"
#include <cstdint>
#include <string>
#include <vector>

namespace dect2 {

struct L1Pre {
    int type = 0;            // 8 bits: 0 = TS, 1 = GS, 2 = TS+GS
    int bwtExt = 0;          // extended carrier mode
    int s1 = 0;              // 3 bits
    int s2 = 0;              // 4 bits (S2 field 1 << 1 | field 2)
    int repetition = 0;
    int guardInterval = 0;   // 3 bits (L1 order: 1/32 1/16 1/8 1/4 1/128 19/128 19/256)
    int papr = 0;            // 4 bits
    int l1Mod = 0;           // 4 bits: 0 BPSK 1 QPSK 2 16QAM 3 64QAM
    int l1Cod = 0;           // 2 bits
    int l1Fec = 0;           // 2 bits
    int postSize = 0;        // 18 bits: L1-post size in cells
    int postInfoSize = 0;    // 18 bits: L1-post info size in bits (without CRC)
    int pilotPattern = 0;    // 4 bits: 0..7 = PP1..PP8
    int txIdAvail = 0;       // 8
    int cellId = 0;          // 16
    int networkId = 0;       // 16
    int systemId = 0;        // 16
    int numFrames = 0;       // 8
    int numDataSyms = 0;     // 12
    int regen = 0;           // 3
    int postExtension = 0;
    int numRf = 1;           // 3
    int curRf = 0;           // 3
    int version = 0;         // 4: T2_VERSION, 0 = V1.1.1, 1 = V1.2.1, 2 = V1.3.1
    int postScrambled = 0;   // L1_POST_SCRAMBLED: only meaningful from T2_VERSION 2 on, see l1PostScrambled()
    int lite = 0;            // T2_BASE_LITE (same caveat)
    int reserved = 0;        // 4
};

struct L1PlpConf {
    int id = 0, type = 0, payloadType = 3, ff = 0, firstRf = 0, firstFrameIdx = 0, groupId = 0;
    int cod = 0, mod = 0, rotation = 0, fecType = 1, numBlocksMax = 0, frameInterval = 1, timeIlLength = 0;
    int timeIlType = 0, inBandA = 0, inBandB = 0, reserved1 = 0, plpMode = 0, staticFlag = 0, staticPad = 0;
};
struct L1PlpDyn {
    int id = 0, start = 0, numBlocks = 0, reserved2 = 0;
};
struct L1Post {
    int subSlices = 1, numPlp = 1, numAux = 0, auxRfu = 0;
    struct Rf { int idx = 0; uint32_t freq = 0; };
    std::vector<Rf> rf;
    int fefType = 0, fefLength = 0, fefInterval = 0; // only if the S2 "mixed" bit is set
    std::vector<L1PlpConf> plps;
    int fefLengthMsb = 0;
    uint32_t reserved2 = 0;
    std::vector<uint32_t> auxConf; // 28 bits each (type in the top 4 are folded: stored as type<<28|conf)
    // dynamic
    int frameIdx = 0, subSliceInterval = 0, type2Start = 0, changeCounter = 0, startRfIdx = 0, dynReserved1 = 0;
    std::vector<L1PlpDyn> dyn;
    int dynReserved3 = 0;
    std::vector<uint64_t> auxDyn;  // 48 bits each
    int extBits = 0;               // number of extension bits skipped
    bool crcOk = false;
};

// ---- bit helpers
// Whether the L1-post is scrambled. EN 302 755 V1.4.1 clause 7.2.2: before V1.3.1 (T2_VERSION '0010') the bits now called
// L1_POST_SCRAMBLED and T2_BASE_LITE were reserved and "may have been set by the bias balancing algorithm", so they "can only be
// assumed to have the correct meaning if the T2_VERSION field is set to '0010' or greater". A V1.2.1 transmitter can send a 1 there
// while its L1-post is plain.
bool l1PostScrambled(const L1Pre& p);
uint32_t crc32Bits(const std::vector<uint8_t>& bits, size_t n); // CRC-32 (poly 0x04C11DB7, init 0xFFFFFFFF), MSB first
void packL1Pre(const L1Pre& p, std::vector<uint8_t>& bits);     // 168 bits (no CRC)
bool unpackL1Pre(const std::vector<uint8_t>& bits, L1Pre& p);   // needs 168 bits
// L1-post info bits (no CRC). `fefPresent` = S2 mixed bit of the L1-pre.
void packL1Post(const L1Pre& pre, const L1Post& p, bool fefPresent, std::vector<uint8_t>& bits);
bool unpackL1Post(const L1Pre& pre, const std::vector<uint8_t>& bits, bool fefPresent, L1Post& p);

// ---- constellations used by L1-post and (later) PLPs
int modBits(int l1Mod);                         // bits per cell: 1, 2, 4, 6
cf32 mapCell(int l1Mod, unsigned bits);          // bits MSB-first
// max-log LLR (positive = bit 0) for one cell; `n0` is the noise variance of the equalised cell
void demapCell(int l1Mod, cf32 y, float n0, float* llr);

// ---- encoders (used by the generator / tests)
// L1-pre -> 1840 BPSK cells
std::vector<cf32> encodeL1Pre(const L1Pre& pre);
// L1-post -> cells; fills pre.postSize / pre.postInfoSize consistently (call before encodeL1Pre)
std::vector<cf32> encodeL1Post(L1Pre& pre, const L1Post& post, int nP2, bool fefPresent);

// ---- decoders
struct L1Result {
    bool ok = false;
    int ldpcIters = 0;
    bool bchOk = false;
    bool crcOk = false;
};
// y: 1840 equalised cells; n0: noise variance per real dimension
L1Result decodeL1Pre(const std::vector<cf32>& y, float n0, L1Pre& out);
L1Result decodeL1Post(const std::vector<cf32>& y, float n0, const L1Pre& pre, int nP2, bool fefPresent, L1Post& out);

// ---- P2 cell distribution
// Stream order: [L1-pre][L1-post][other cells]. Distribute across nP2 symbols of cP2 cells (zig-zag), and back.
void p2Distribute(const std::vector<cf32>& stream, int nP2, int cP2, int nPre, int nPost, std::vector<std::vector<cf32>>& sym);
void p2Gather(const std::vector<std::vector<cf32>>& sym, int nP2, int cP2, int nPre, int nPost, std::vector<cf32>& stream);

} // namespace dect2
