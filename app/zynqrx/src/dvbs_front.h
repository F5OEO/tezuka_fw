// DVB-S/S2 receiver front end: input samples at any rate -> matched filter output at one sample per symbol (timing recovered).
#pragma once
#include "dvbs_dsp.h"
#include <memory>

namespace dect2 {
namespace dvbs {

class DvbsFront {
public:
    struct Config {
        double fs = 10e6;
        double symbolRate = 5e6;
        double centerHz = 0;       // the carrier relative to the centre of the input
        double rollOff = 0.35;
        bool prefiltered = false;  // the input is already matched filtered (root raised cosine) at about 2 samples per symbol, e.g. by the radio's FPGA
    };
    void configure(const Config& c);
    void reset();                                           // keeps the configuration, forgets the signal
    void process(const cf32* x, size_t n, std::vector<cf32>& out);   // appends symbols
    const Config& config() const { return c_; }
    SymbolTimer& timer() { return timer_; }
    const SymbolTimer& timer() const { return timer_; }
    int halfbands() const { return (int)hb_.size(); }
    double fs1() const { return fs1_; }
    // the symbol rate the timing loop follows: nominal rate times (1 + offset)
    double symbolRateNow() const { return c_.symbolRate * (1.0 + timer_.rateOffset()); }
    // Folds the loop's rate offset into the resampler so that the loop works around zero again (call once in a while)
    void recentre();
    void setCenter(double hz) { mix_.setFrequency(hz); c_.centerHz = hz; }

private:
    Config c_;
    double fs1_ = 0;
    MixDc mix_;
    std::vector<Halfband> hb_;
    std::vector<SplitBuf> stage_;
    PolyResampler rs_;
    SymbolTimer timer_;
    bool bypass_ = false;     // prefiltered input at 2 samples per symbol: no resampler, no matched filter
    SplitBuf a_, b_, two_;
};

} // namespace dvbs
} // namespace dect2
