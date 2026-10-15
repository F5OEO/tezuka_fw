// DVB-S2X (EN 302 307-2 V1.2.1) MODCODs: what each one of table 17a is made of (LDPC code, frame size, constellation, bit interleaver), and its
// constellation. dvbs_s2.cpp turns these into the rate indices kS2Rates and up. Internal.
#pragma once
#include "dect2/ring.h"
#include <vector>

namespace dect2 {
namespace dvbs {

// Constellations of EN 302 307-2 clause 5.4
enum S2xShape {
    kShS2,          // the DVB-S2 QPSK or 8PSK
    kSh242,         // 2+4+2APSK, tables 10a and 10b
    kSh412,         // 4+12APSK: the DVB-S2 16APSK with the ring ratio of tables 11a and 11b
    kSh88,          // 8+8APSK, tables 11c and 11d
    kSh88Tab18,     // 8+8APSK, the points of table 11e for 18/30
    kSh88Tab20,     // ... for 20/30
    kSh41216rb,     // 4+12+16rbAPSK, tables 12a to 12c
    kSh48416,       // 4+8+4+16APSK, tables 12d and 12e
    kSh16x4,        // 16+16+16+16APSK, tables 13a and 13b
    kSh8162020,     // 8+16+20+20APSK, tables 13c and 13d
    kSh4122028,     // 4+12+20+28APSK, tables 13e and 13f
    kSh128,         // 128APSK, tables 14a and 14b
    kSh256,         // 256APSK, tables 15a to 15c
    kSh256Tab20,    // 256APSK, the points of table 15d for 20/30
    kSh256Tab22,    // ... for 22/30
};

struct S2xModcod {
    int pls;                 // PLS code value of table 17a (pilots off)
    int mod;                 // S2Mod
    const char* modName;     // canonical modulation name ("8APSK" where the MODCOD is not 8PSK)
    const char* rate;        // canonical code rate ("5/9-L")
    const char* code;        // LDPC code identifier (table 17a, "implementation MODCOD name")
    bool shortFrame;
    const char* table;       // annex B / C table of the LDPC code, or null: the DVB-S2 code of rate index s2rate (EN 302 307-1)
    int s2rate;
    const char* il;          // bit interleaver pattern, tables 9a and 9b ("" for QPSK)
    S2xShape shape;
    double g[7];             // radius ratios gamma1.. (R2/R1, R3/R1, ...)
    double esn0;             // ideal Es/N0 for quasi error free reception, tables 20a and 20c
};

// rate index kS2Rates + i -> row i of table 17a (nullptr when the index is not an S2X one)
const S2xModcod* s2xModcod(int rate);
const S2xModcod* s2xModcodByPls(int pls);          // pls with the LSB cleared
// Points of the constellation in label order, unit average energy
void s2xBuildConstellation(const S2xModcod& m, std::vector<cf32>& pts);
// Frame lengths of the S2X PLS codes that are not decoded here: VL-SNR (129, 131) and the reserved values of table 17b. 0 for the others.
int s2xSpecialFrameSymbols(int pls);

} // namespace dvbs
} // namespace dect2
