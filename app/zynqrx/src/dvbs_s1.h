// DVB-S (EN 300 421) receive chain: carrier recovery for QPSK, trial of the code rate / puncturing phase / constellation rotation, Viterbi,
// de-interleaver, Reed-Solomon, energy dispersal removal. Internal to the dvbs_*.cpp files.
#pragma once
#include "dect2/dvbt.h"
#include "dect2/ring.h"
#include "dvbs_compute.h"
#include <cstdint>
#include <deque>
#include <vector>

namespace dect2 {
namespace dvbs {

// The four constellation variants that remain after the carrier loop has settled on one of the four quadrants (and ignoring the
// 180 degree turn, which only inverts the decoded bits): identity, +90 degrees, spectral inversion, inversion and +90 degrees.
inline cf32 s1Variant(cf32 z, int v) {
    switch (v & 3) {
    case 0: return z;
    case 1: return cf32(-z.imag(), z.real());
    case 2: return cf32(z.real(), -z.imag());
    default: return cf32(z.imag(), z.real());       // conj, then +90 degrees
    }
}

struct S1Hypothesis {
    bool ok = false;
    int variant = 0;           // s1Variant
    int rate = 0;              // dvbt::Rate index: 1/2 2/3 3/4 5/6 7/8
    int offset = 0;            // first symbol of the stream, in symbols into the puncturing period
    double score = 0;          // sync bytes found at 204 byte spacing in the decoded stream, as a fraction of the packets in the window
    double second = 0;         // the same for the best hypothesis that is not this one
    int packets = 0;           // packets in the window the score is based on
    bool inverted = false;     // the 180 degree turn that the Viterbi decoder cannot see: the decoded bits are inverted
};
// Finds the byte phase and packet position of the stream from the 0x47 / 0xB8 sync bytes, and whether the bits are inverted.
// `bits`: one decoded bit per byte. Returns the number of sync bytes found (0 when there is no periodicity), `blocks`: packets examined.
int s1FindSync(const std::vector<uint8_t>& bits, int& bitOffset, int& bytePos, bool& inverted, int& blocks);
// Looks for the code rate, puncturing phase and constellation variant in `n` symbols (at least 20000 recommended) that are free of carrier
// error: each hypothesis is decoded and the one whose output has the sync bytes of a transport stream wins. rateHint: -1 tries all five rates.
S1Hypothesis s1Search(const cf32* y, size_t n, int rateHint = -1);

// number of symbols in a puncturing period for the rate (and the serial bits per period)
int s1PeriodSymbols(int rate);

// Streaming decoder for one hypothesis. Symbols in (after carrier recovery, unit scale), whole 188-byte packets out.
class S1Decoder {
public:
    // `skipped`: symbols between the first symbol of the window the hypothesis was found in and the first symbol that will be pushed
    void start(const S1Hypothesis& h, uint64_t skipped = 0);
    void reset();
    void push(const cf32* y, size_t n);
    // appends decoded packets; packets that Reed-Solomon could not repair have the transport error indicator set
    void takePackets(std::vector<uint8_t>& out);
    // Where the Viterbi decoder runs (CPU, or the GPU on windows many times larger, see process()); null: always the CPU
    void setCompute(ComputeChoice* c) { compute_ = c; gpuWindow_ = false; }
    bool lastOnGpu() const { return lastGpu_; }
    void flushPending() { if (started_ && !llr_.empty()) process(); }   // decode what is waiting for a full window (the end of a recording)
    bool synced() const { return syncLocked_; }
    bool started() const { return started_; }
    int rate() const { return h_.rate; }
    const S1Hypothesis& hypothesis() const { return h_; }
    // counters since start()
    uint64_t packets = 0, rsClean = 0, rsCorrected = 0, rsFailed = 0, bytesCorrected = 0;
    uint64_t symbols = 0;
    uint64_t syncLosses = 0;
    int consecutiveBad() const { return consecBad_; }
    double viterbiScore() const { return score_; }
    // fraction of bytes the Reed-Solomon decoder had to change over the last blocks: a stand-in for the bit error rate before the outer code
    double byteErrorRate() const { return ber_; }

private:
    void process();
    void findSync();
    ComputeChoice* compute_ = nullptr;
    bool gpuWindow_ = false, lastGpu_ = false;
    S1Hypothesis h_;
    bool started_ = false, syncLocked_ = false, inverted_ = false;
    std::vector<float> llr_;               // serial soft values waiting for the Viterbi decoder
    int skip_ = 0;                         // values to drop at the start so that the first one begins a trellis step
    int phase_ = 0;                        // trellis step (inside the puncturing period) of the first value left after that
    bool first_ = true;
    std::vector<int8_t> carry_;            // last steps of soft values, the history of the next window
    std::vector<uint8_t> bits_;            // decoded information bits
    std::vector<uint8_t> aligned_;         // bytes from the sync position on, before the de-interleaver
    std::vector<uint8_t> after_;           // after the de-interleaver
    struct Held { uint8_t d[188]; int r; };
    std::deque<Held> held_;                   // delivered packets that wait for the verdict of the blocks behind them, see deliver()
    void deliver(uint8_t* pkt, int r);
    void releaseHeld(size_t keep);
    std::vector<uint8_t> out_;
    dvbt::ConvInterleaver deint_{true};
    int warm_ = 0, groupIdx_ = 0;
    bool haveGroup_ = false;
    int consecBad_ = 0, syncMisses_ = 0;
    double score_ = 0, ber_ = 0;
    uint64_t blocks_ = 0;
};

} // namespace dvbs
} // namespace dect2
