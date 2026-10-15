// DVB-T2 (ETSI EN 302 755) constants shared by receiver, generator and UI.
#pragma once
#include <cstdint>

namespace dect2 {

extern const int kP1ActiveCarriers[384];
extern const uint8_t kS1Patterns[8][8];
extern const uint8_t kS2Patterns[16][32];

constexpr int kP1Len = 2048;     // C(542) + A(1024) + B(482)
constexpr int kP1CLen = 542;
constexpr int kP1ALen = 1024;
constexpr int kP1BLen = 482;

// Elementary period T = 7/64 us at 8 MHz  =>  native sample rate 64/7 Msps.
inline double nativeRateHz(double bwMhz) {
    if (bwMhz == 8) return 64e6 / 7;
    if (bwMhz == 7) return 8e6;
    if (bwMhz == 6) return 48e6 / 7;
    if (bwMhz == 5) return 40e6 / 7;
    if (bwMhz == 10) return 80e6 / 7;
    return 131e6 / 71; // 1.7 MHz
}

// S2 field 1 (3 bits) -> FFT size.
struct FftMode {
    int code;        // S2 field 1
    int n;           // FFT size
    int nP2;         // number of P2 symbols
    int kNormal;     // total carriers, normal carrier mode
    int kExt;        // total carriers, extended mode (0 if n/a)
    const char* name;
};
const FftMode* fftModeFromS2(int s2field1); // nullptr if invalid
const FftMode* fftModeFromSize(int n);

constexpr int kNumGi = 7;
// Guard intervals as fractions: 1/32, 1/16, 1/8, 1/4, 1/128, 19/128, 19/256 (L1-pre GI field order)
int guardSamples(int fftN, int giIdx);
const char* guardName(int giIdx);

// S1 field -> text (000 T2-SISO, 001 T2-MISO, 010 non-T2, 011 T2-Lite SISO, 100 T2-Lite MISO)
const char* s1Name(int s1);

} // namespace dect2
