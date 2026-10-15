// Offline DVB-T2-like signal generator used for testing and the built-in synthetic source.
// Produces genuine P1 symbols (S1/S2 signalling) followed by P2/data symbols whose cells are random QPSK.
// Never transmits anything - it only writes samples to memory/files.
#pragma once
#include "ring.h"
#include "t2.h"
#include "t2pilots.h"
#include "t2l1.h"
#include <memory>
#include <random>
#include <vector>
#include <cstdint>

namespace dect2 {

struct TxParams {
    int s1 = 0;          // 0 = T2-Base SISO
    int s2field1 = 1;    // FFT size code, see FftMode
    bool mixed = false;  // S2 field 2
    int giIdx = 4;       // see guardName()
    bool ext = false;    // extended carrier mode
    int pp = 0;          // pilot pattern 0..7 = PP1..PP8
    bool tr = false;     // tone-reservation PAPR (reserved carriers left empty)
    int dataSymbols = 0; // symbols per frame after P1 (incl. P2); 0 = auto (~60 ms)
    int l1Mod = 1;       // L1-post modulation: 0 BPSK 1 QPSK 2 16QAM 3 64QAM
    bool l1Scrambled = true; // L1_POST_SCRAMBLED bit; the L1-post is only scrambled when t2Version >= 2 (EN 302 755 cl. 7.2.2)
    int t2Version = 2;       // T2_VERSION: 0 = V1.1.1, 1 = V1.2.1, 2 = V1.3.1
    // Carry a real transport stream in PLP 0 (baseband framing, BCH, LDPC, bit/cell/time interleaving, mapping) instead of random cells.
    bool payload = false;
    bool plpShort = false;   // 16200-bit FEC frames (T2-Lite uses these, with the extra code rates 1/3 and 2/5)
    int plpMod = 2;          // 0 QPSK 1 16QAM 2 64QAM 3 256QAM
    int plpCod = 2;          // 0..7 = 1/2 3/5 2/3 3/4 4/5 5/6 1/3 2/5
    bool plpRot = true;      // rotated constellation
    int plpTi = 3;           // time-interleaver blocks per frame (0 = none)
    int cellId = 0x1001, networkId = 0x3085, systemId = 0x8001;
    uint32_t seed = 1;
};

class T2Generator {
public:
    explicit T2Generator(const TxParams& p);
    ~T2Generator();
    const TxParams& params() const { return p_; }
    int fftN() const { return n_; }
    int guard() const { return g_; }
    int carriers() const { return k_; }
    const PilotMap& pilots() const { return *pm_; }
    int dataSymbols() const { return symbols_ - nP2_; }
    const L1Pre& l1pre() const { return pre_; }
    const L1Post& l1post() const { return post_; }
    int symbolsPerFrame() const { return symbols_; }
    size_t frameLength() const { return (size_t)kP1Len + (size_t)symbols_ * (n_ + g_); }
    void nextFrame(std::vector<cf32>& out); // one full frame: P1 + symbols
    // With payload: the baseband frames (BBHEADER + data field + padding, before scrambling) sent in the last frame, one per FEC block.
    const std::vector<std::vector<uint8_t>>& lastBbFrames() const { return lastBb_; }
    int plpBlocks() const { return plpBlocks_; }

    struct Impl;

private:
    Impl* i_;
    TxParams p_;
    std::unique_ptr<PilotMap> pm_;
    L1Pre pre_;
    L1Post post_;
    int frameNo_ = 0;
    int plpBlocks_ = 0;
    std::vector<std::vector<uint8_t>> lastBb_;
    int n_, g_, k_, symbols_, nP2_;
    int s2sig_ = 0;   // the S2 field 1 that P1 and L1-pre signal (6 or 7 for 8K or 32K with GI 1/128, 19/256, 19/128)
};

} // namespace dect2
