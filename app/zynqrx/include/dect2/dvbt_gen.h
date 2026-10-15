// DVB-T signal generator (transmitter): transport stream in, baseband IQ at the native rate (64/7 MHz for 8 MHz) out.
#pragma once
#include "dvbt.h"
#include <deque>
#include <functional>
#include <random>
#include <cstdint>
#include <vector>

namespace dect2 {
class Fft;
namespace dvbt {

class Generator;
std::function<void(uint8_t*)> testTsSource(unsigned seed = 1);

class Generator {
public:
    using PacketSource = std::function<void(uint8_t* pkt188)>;
    explicit Generator(const Params& p, PacketSource src = nullptr, unsigned seed = 1);
    const Params& params() const { return p_; }
    int fftN() const { return N_; }
    int guard() const { return G_; }
    int symbolIndex() const { return sym_; }       // index (0..67) of the next symbol inside its frame
    int frameIndex() const { return frame_; }      // 0..3
    // One OFDM symbol including its guard interval (N + G samples)
    void nextSymbol(std::vector<cf32>& out);
    // Frequency-domain carriers of the last symbol (for tests)
    const std::vector<cf32>& lastCarriers() const { return carriers_; }
    // Hierarchical modes: the low-priority transport stream (default: null packets)
    void setLpSource(PacketSource src) { srcLp_ = std::move(src); }

private:
    void refill();
    Params p_;
    int N_, G_, K_, sym_ = 0, frame_ = 0;
    PacketSource src_;
    std::mt19937 rng_;
    ConvInterleaver ci_{false};
    InnerEncoder enc_;
    std::vector<uint8_t> codedQ_;
    PacketSource srcLp_;
    ConvInterleaver ciLp_{false};
    InnerEncoder encLp_{0};
    std::vector<uint8_t> codedLpQ_;
    void codeGroup(PacketSource& src, ConvInterleaver& ci, InnerEncoder& enc, std::vector<uint8_t>& q);
    std::deque<uint8_t> wordsQ_;
    std::vector<float> tpsVal_;
    std::vector<cf32> carriers_;
    ::dect2::Fft* fft_ = nullptr;
};

} // namespace dvbt
} // namespace dect2
