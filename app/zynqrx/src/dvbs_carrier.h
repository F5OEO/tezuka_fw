// QPSK carrier recovery for DVB-S (and as a building block for DVB-S2 QPSK): a frequency and phase estimate from the fourth power of the
// symbols, and a decision directed phase locked loop. Internal.
#pragma once
#include "dect2/ring.h"
#include <cstddef>

namespace dect2 {
namespace dvbs {

struct QpskEstimate {
    bool ok = false;
    double freq = 0;        // cycles per symbol (the carrier offset divided by the symbol rate), within +-1/8
    double phase = 0;       // radians, at the first symbol, modulo 90 degrees
    double ratio = 0;       // height of the spectral line over the mean of the spectrum
};
// z: timing recovered symbols of unit mean power. n is rounded down to a power of two (at most 16384)
QpskEstimate qpskEstimate(const cf32* z, size_t n);

// the original libm sine, cosine and tanh in the PLL instead of the fast ones (for the test that compares the two; process wide)
void qpskPllReferenceMath(bool on);
void dvbsReferencePath(bool on);                         // the whole DVB-S chain as before the speed-up: libm PLL, scalar paths
bool dvbsReferencePath();

class QpskPll {
public:
    void start(double freq, double phase, double bnT = 0.01, double zeta = 0.707);
    void setBandwidth(double bnT, double zeta = 0.707);
    void process(const cf32* z, size_t n, cf32* out);       // out may be the same as z
    double freq() const { return omega_ / 6.283185307179586; }   // cycles per symbol
    double phase() const { return theta_; }
    float lock() const { return lock_; }                    // about 1 when locked, about 0 when the phase wanders
    float snrDb() const { return snrDb_; }                  // from the moments of the amplitude (works without a carrier lock)
private:
    void processReference(const cf32* z, size_t n, cf32* out);   // the loop one symbol at a time with libm
    double theta_ = 0, omega_ = 0, kp_ = 0, ki_ = 0;
    float lock_ = 0, snrDb_ = 0;
    float sigma2_ = 0.1f, amp_ = 0.70710678f;
    double m2_ = 1, m4_ = 1, c4re_ = 0;
    size_t count_ = 0;
};

} // namespace dvbs
} // namespace dect2
