// DVB-S/S2 receiver signal path: input samples -> symbols. Mixer, half-band decimators, polyphase resampler to two samples per symbol,
// polyphase root raised cosine matched filter with a Gardner timing loop. Internal to the dvbs_*.cpp files.
#pragma once
#include "dect2/ring.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dect2 {
namespace dvbs {

// complex samples as two float arrays: what the filters are written for (the compiler vectorises the dot products)
struct SplitBuf {
    std::vector<float> re, im;
    size_t size() const { return re.size(); }
    void clear() { re.clear(); im.clear(); }
    void resize(size_t n) { re.resize(n); im.resize(n); }
    void eraseFront(size_t n) {
        re.erase(re.begin(), re.begin() + (std::ptrdiff_t)n);
        im.erase(im.begin(), im.begin() + (std::ptrdiff_t)n);
    }
};

// Multiplies by exp(-j 2 pi f n / fs) and removes the mean (the DC spike of a radio) while converting to split arrays
class MixDc {
public:
    void configure(double fs, double freqHz);
    void setFrequency(double hz) { step_ = 2 * 3.14159265358979323846 * hz / fs_; makeTable(); }
    void reset() { phase_ = 0; dcRe_ = dcIm_ = 0; dcInit_ = false; }
    void process(const cf32* x, size_t n, SplitBuf& out);   // appends
private:
    static constexpr int kBlock = 64;
    void makeTable();
    double fs_ = 1, step_ = 0, phase_ = 0;
    float dcRe_ = 0, dcIm_ = 0;
    bool dcInit_ = false;
    float tc_[kBlock], ts_[kBlock];       // e^{-j k step} for k < kBlock
};

// Decimation by two with a half-band FIR (about 70 dB down from 0.3 to 0.7 of the output rate... see the .cpp)
class Halfband {
public:
    Halfband();
    void reset();
    void process(const SplitBuf& in, size_t n, SplitBuf& out);   // consumes the first n samples of `in` (appends the decimated ones to out)
private:
    std::vector<float> h_;           // taps of the odd offsets: h_[j] for offset 2j+1
    float centre_ = 0.5f;
    SplitBuf hist_;
    size_t phase_ = 0;
};

// Polyphase windowed-sinc resampler with a ratio that may be changed slowly. ratio = input samples per output sample.
class PolyResampler {
public:
    void configure(double ratio);
    void setRatio(double ratio) { ratio_ = ratio; }
    double ratio() const { return ratio_; }
    void reset();
    void process(const SplitBuf& in, size_t n, SplitBuf& out);   // consumes the first n samples of `in`
private:
    void design(double ratio);
    double ratio_ = 1;
    int taps_ = 16, phases_ = 1024;
    std::vector<float> bank_;        // phases x taps, each phase normalised to unit DC gain
    SplitBuf hist_;
    double pos_ = 0;                 // position of the next output relative to hist_[0]
};

// Root raised cosine matched filter at two samples per symbol with a bank of fractional delays, driven by a Gardner timing loop.
// Input: two samples per symbol (any phase). Output: one matched filter output per symbol, unit mean power.
class SymbolTimer {
public:
    // prefiltered: the input is already root raised cosine filtered (by the radio) at about two samples per symbol; the timer then only interpolates
    // (a short windowed sinc) instead of matched filtering, and halfSpanSymbols is the half length of that interpolator
    void configure(double rollOff, int halfSpanSymbols = 10, bool prefiltered = false);
    void reset();
    void setLoop(double bnT, double zeta);
    // consumes all of `in` that can be used, appends symbols to out
    void process(SplitBuf& in, std::vector<cf32>& out);
    void shiftIntegrator(double by) { integ_ -= by; }   // the resampler took `by` of the offset over
    double rateOffset() const { return integ_; }     // relative symbol rate offset the loop has found (+: input symbols faster than the nominal rate)
    // The input can also be written straight into the timer's own buffer (MixDc::process appends to it), then run() makes the symbols: no copy of the block
    SplitBuf& inbuf() { return buf_; }
    void run(std::vector<cf32>& out);
    // Cheaper timing once the loop has settled: 8 symbols share one row of the filter bank (the row of their middle, so the instants drift from it by at
    // most 7 times the loop correction) and the timing error detector looks at the last four of them with twice the gains. The half-symbol filter and the
    // loop arithmetic are then needed for a quarter of the symbols and the per-block work is halved: about 25% less time in the timer, for a
    // little less accuracy of the sampling instants (about 0.1 dB near the decoding threshold on a signal with phase noise). Only used while the
    // correction is below 2e-3 and the loop is narrow, so it never touches acquisition.
    void setFast(bool on) { fast_ = on; }
    bool fast() const { return fast_; }
    float errorRms() const { return errRms_; }       // timing error detector output, rms, normalised by the symbol power
    double timingPhase() const { return t_; }
    uint64_t symbols() const { return count_; }
private:
    void design(double alpha);
    double alpha_ = 0.35;
    int hs_ = 10, m_ = 20, phases_ = 512;
    bool prefilt_ = false;
    std::vector<float> bank_;
    std::vector<float> mfRe_, mfIm_;
    SplitBuf buf_;
    double t_ = 0;
    double integ_ = 0, kp_ = 0, ki_ = 0;
    double zeta_ = 0.7, bnt_ = 0.01;
    bool fast_ = false;
    float prevRe_ = 0, prevIm_ = 0;
    bool havePrev_ = false;
    float power_ = 1, invPower_ = 1, gain_ = 1;
    double corr_ = 0;
    float errRms_ = 0, errMs_ = 0;
    uint64_t count_ = 0;
    double kd_ = 1;
};

} // namespace dvbs
} // namespace dect2
