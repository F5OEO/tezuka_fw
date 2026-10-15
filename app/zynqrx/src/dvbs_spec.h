// DVB-S/S2 receiver: power spectrum of the input, and what it says about the carrier (centre, symbol rate, roll-off). Internal.
#pragma once
#include "dect2/ring.h"
#include <cstdint>
#include <vector>

namespace dect2 {
namespace dvbs {

struct SpectrumResult {
    bool valid = false;
    double centerHz = 0;        // centre of the carrier, relative to the centre of the input
    double rateHz = 0;          // symbol rate: the -3 dB width of a root raised cosine pair is exactly Rs for any roll-off
    double edgeLoHz = 0, edgeHiHz = 0;   // the two -3 dB edges
    double bw20Hz = 0;          // width at -20 dB, 0 when the signal is too weak to measure it
    double rollOff = 0;         // from bw20 and the rate, 0 when unknown
    float plateauDb = -200, floorDb = -200;   // levels in dB (arbitrary reference), plateau of the carrier and noise floor
    float snrDb = 0;            // plateau over floor
    float flatnessDb = 0;       // ripple of the plateau (rms)
    float zscore = 0;           // how far the carrier stands out of the noise, in standard deviations of the averaged spectrum
    float fitRms = 0;           // misfit of the raised cosine model, relative (about 0.1 for a good 64-segment average; 1 or more: not a carrier of this kind)
};

class SpectrumEstimator {
public:
    void configure(double fs, int fftSize = 4096);
    void reset();
    // Takes one segment of fftSize samples out of every `stride` samples (0: every segment, none skipped)
    void push(const cf32* x, size_t n);
    void setStride(size_t stride) { stride_ = stride; }
    int segments() const { return segs_; }
    SpectrumResult analyse() const;
    // Averaged spectrum in dB, `points` bins over the whole input rate, fft shifted (index points/2 = 0 Hz), peak held at 0 dB at the maximum
    void display(std::vector<float>& db, int points) const;
    double fs() const { return fs_; }
    int fftSize() const { return n_; }
    std::vector<double> averaged() const;       // linear power per bin, fft shifted

private:
    double fs_ = 10e6;
    int n_ = 4096, log2n_ = 12;
    std::vector<double> acc_;
    std::vector<float> win_, re_, im_;
    std::vector<cf32> part_;      // a segment being collected
    int segs_ = 0;
    size_t stride_ = 0, skip_ = 0;
};

// The part of analyse() that works on an averaged spectrum (shifted, linear power), exposed for tests
SpectrumResult analysePsd(const std::vector<double>& psd, double fs, int segments = 64);

} // namespace dvbs
} // namespace dect2
