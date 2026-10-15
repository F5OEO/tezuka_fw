// DVB-S2 receiver: decisions and phase refinement on a whole frame of symbols. The carrier loop runs causally with a narrow bandwidth (so that it
// does not slip on noisy symbols); the frame is then handed over here, where the phase error that the loop left (phase noise of the LNB faster
// than the loop, lag) is measured in blocks against decisions on the same symbols and taken out with a zero lag smoother. Internal.
#pragma once
#include "dect2/ring.h"
#include <cstdint>
#include <vector>

namespace dect2 {
namespace dvbs {

// Nearest constellation point of a symbol from a lookup grid (an approximation at the cell borders is fine for phase estimation)
class NearestPoint {
public:
    void build(const cf32* points, int count);
    // index of the nearest point
    int index(float re, float im) const {
        int ix = (int)((re + lim_) * scale_), iy = (int)((im + lim_) * scale_);
        ix = ix < 0 ? 0 : ix >= grid_ ? grid_ - 1 : ix;
        iy = iy < 0 ? 0 : iy >= grid_ ? grid_ - 1 : iy;
        return idx_[(size_t)iy * (size_t)grid_ + (size_t)ix];
    }
    // indices of the nearest and of the second nearest point (the soft detector of a dense constellation only needs those two)
    void index2(float re, float im, int& i1, int& i2) const {
        int ix = (int)((re + lim_) * scale_), iy = (int)((im + lim_) * scale_);
        ix = ix < 0 ? 0 : ix >= grid_ ? grid_ - 1 : ix;
        iy = iy < 0 ? 0 : iy >= grid_ ? grid_ - 1 : iy;
        i1 = idx_[(size_t)iy * (size_t)grid_ + (size_t)ix];
        i2 = idx2_[(size_t)iy * (size_t)grid_ + (size_t)ix];
    }
    // the two points nearest to point i (the neighbours on the circle of an 8PSK constellation)
    void neighbours(int i, int& a, int& b) const { a = nbr_[(size_t)i * 2]; b = nbr_[(size_t)i * 2 + 1]; }
    const cf32* points() const { return pts_; }
    int count() const { return n_; }
private:
    int grid_ = 128;                 // cells a side: finer for the dense S2X constellations
    float lim_ = 2.f, scale_ = 1.f;
    const cf32* pts_ = nullptr;
    int n_ = 0;
    std::vector<uint8_t> idx_, idx2_, nbr_;
};
// The lookup grid for a constellation (built once, shared)
const NearestPoint& nearestPoint(int mod, int rate);

// Measurement of the carrier phase at a known place in the frame: a pilot block between data symbols `at - 1` and `at`
struct PhaseMark {
    uint32_t at = 0;
    float psi = 0;           // phase error of the carrier loop at the block, radians
    float weight = 0;        // 36 pilots at the signal to noise ratio: the larger the better
};

// Measures and removes the phase error left in `x` (the data symbols of one frame, after the carrier loop). `sigma2` is the noise variance per real
// dimension; `blockSyms` the length of a block. Returns the rms of the correction in radians.
float s2RefinePhase(cf32* x, int n, int mod, int rate, float sigma2, int blockSyms, const std::vector<PhaseMark>& marks, int iterations = 2);

} // namespace dvbs
} // namespace dect2
