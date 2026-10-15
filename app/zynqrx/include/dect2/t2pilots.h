// DVB-T2 pilot structure: which carrier of which OFDM symbol is a pilot / reserved / data, and the pilot values.
// (ETSI EN 302 755 clause 9.) SISO only for now (MISO needs TX2 inversion and extra P2 pilots).
#pragma once
#include "ring.h"
#include "t2.h"
#include <cstdint>
#include <vector>

namespace dect2 {

enum CellType : uint8_t { kCellData = 0, kCellP2Pilot, kCellP2Papr, kCellTrPapr, kCellScattered, kCellContinual };

struct TrTable { const uint16_t* v; int n; };
struct PilotEntry {
    uint8_t fft, ext, pp, dx, dy;
    float sp, cp, p2;
    const uint16_t* cont;
    int nCont;
};
extern const uint8_t kPilotPn[328];
extern const TrTable kP2PaprTable[6];
extern const TrTable kTrPaprTable[6];
extern const PilotEntry kPilotEntries[];
extern const int kNumPilotEntries;

struct PilotConfig {
    int fftCode = 1;   // S2 field 1
    bool ext = false;  // extended carrier mode
    int pp = 0;        // 0..7 = PP1..PP8
    bool tr = false;   // tone-reservation PAPR in data/FC symbols
    int giIdx = 2;     // needed only to decide whether a frame-closing symbol exists
};

class PilotMap {
public:
    explicit PilotMap(const PilotConfig& c);
    bool valid() const { return valid_; }
    const PilotConfig& config() const { return cfg_; }
    int fftN() const { return n_; }
    int carriers() const { return cps_; }      // K_total
    int kExt() const { return kExt_; }
    int dx() const { return dx_; }
    int dy() const { return dy_; }
    int numP2() const { return nP2_; }
    bool hasFc() const { return hasFc_; }
    int p2DataCells() const { return cP2_; }
    int dataCells() const { return cData_; }
    int fcDataCells() const { return nFc_; }

    // Cell type of every carrier (0..K_total-1) of symbol `l` in a frame with `numSyms` symbols after P1.
    void symbolTypes(int l, int numSyms, std::vector<uint8_t>& t) const;
    // Reference value of a pilot at carrier k of symbol l.
    cf32 pilot(int l, int k, uint8_t type) const;
    bool isP2(int l) const { return l < nP2_; }
    bool isFc(int l, int numSyms) const { return hasFc_ && l == numSyms - 1; }

private:
    PilotConfig cfg_;
    bool valid_ = false;
    int n_ = 0, cps_ = 0, kExt_ = 0, kOff_ = 0, dx_ = 3, dy_ = 4, nP2_ = 1, cP2_ = 0, cData_ = 0, nFc_ = 0;
    bool hasFc_ = false;
    float sp_ = 1, cp_ = 1, p2_ = 1;
    const PilotEntry* e_ = nullptr;
    std::vector<uint8_t> prbs_;
    std::vector<uint8_t> p2Types_, fcTypes_;
    std::vector<uint8_t> contMask_;
};

} // namespace dect2
