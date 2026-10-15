// DVB-S2 frame receiver: follows the PLFRAMEs of a symbol stream (PLHEADER, pilots, physical layer scrambling), recovers the carrier phase
// with a loop that uses the known symbols and decisions, and hands the FECFRAMEs to decoder threads (demapper, LDPC, BCH, BBHEADER) whose
// results are put back in order and turned into transport stream packets. Internal.
#pragma once
#include "dect2/dvbs_s2.h"
#include "dvbs_s2hunt.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {
namespace dvbs {

struct S2Stats {
    uint64_t framesSeen = 0, framesDummy = 0;
    uint64_t framesUnsupported = 0;            // S2X frames followed but not decoded (VL-SNR, reserved PLS codes)
    int unsupportedCode = -1;                  // PLS code value of the last of them
    uint64_t fecOk = 0, fecBad = 0;            // frames that LDPC and BCH decoded / did not decode (including frames dropped because the decoders were behind)
    uint64_t bchBad = 0;
    uint64_t headerBad = 0;                    // PLHEADERs that could not be decoded
    uint64_t dropped = 0;                      // frames skipped because the decoder threads were behind
    uint64_t timingSlips = 0;                  // times the header was found a symbol or more away from where the frame length put it
    uint64_t phaseJumps = 0;                   // times the carrier loop was turned by a header or a pilot block (a slip to another quadrant)
    uint64_t packets = 0, packetsBad = 0, crcErrors = 0, gseFrames = 0, bbHeaderBad = 0, otherIsi = 0;
    double merDb = 0;                          // modulation error ratio of the known symbols (header, pilots), smoothed
    double snrDb = 0;
    double ldpcIterAvg = 0;
    bool ldpcOnGpu = false;                    // the last frame went through the GPU LDPC decoder
    uint64_t ldpcGpuFrames = 0;                // frames the GPU decoded
    double berPre = -1;                        // bit error rate before the LDPC decoder, from the frames that decoded
    double carrierRadPerSym = 0;               // carrier offset still tracked by the loop
    int modcod = -1, mod = -1, rate = -1;
    bool shortFrame = false, pilots = false, vcm = false;
    int isi = -1;
    int matype1 = -1, matype2 = -1;            // the two MATYPE bytes of the last good BBHEADER, -1 not seen
    int roSignalled = -1;                      // RO field of the last good BBHEADER (0: 0.35, 1: 0.25, 2: 0.20), -1 not seen
    bool sis = true;
    int tsGs = -1;
    int consecutiveBad = 0;                    // frames in a row without a good FEC result
    int consecutiveHeaderBad = 0;
};

class S2Rx {
public:
    S2Rx();
    ~S2Rx();
    void setSymbolRate(double hz) { symbolRate_ = hz; }
    void setPlCode(int n) { plCode_ = n; }
    void setIsi(int isi) { isiSel_ = isi; }
    void setBlocking(bool b) { blocking_ = b; }
    void setComputeMode(int m);                    // 0 CPU, 1 GPU, 2 auto: where the LDPC decoder runs
    void setLdpc8(bool on);                        // the 8-bit CPU decoder at every code rate (default: only from 2/3 up; below, a few 8-bit iterations and then the float decoder)
    void setPacketCallback(std::function<void(const uint8_t*, size_t, double)> cb);
    void setLogCallback(std::function<void(const std::string&)> cb);
    // Starts at the header the hunt found in `win` (n symbols; copied). The frame work runs on a thread of its own: start() and push() only queue symbols
    // (push() drops a block when the thread is far behind, unless setBlocking(true)). The first thing the thread does is the frequency search over the
    // frames of the window, then it processes the symbols from the first header on.
    void start(const S2HuntResult& h, const cf32* win, size_t n);
    void push(const cf32* z, size_t n);
    bool tracking() const { return tracking_.load(); }
    bool lost() const { return lost_.load(); }            // header synchronisation was lost: hunt again
    void drain();                                  // waits until everything pushed so far is decoded and delivered (recordings, tests)
    void stop();                                   // goes back to idle; frames in the decoder threads are finished, symbols still queued are dropped
    S2Stats stats() const;
    void cells(std::vector<cf32>& out) const;      // recent data symbols after carrier recovery
    double phaseRms() const;                       // rms of the phase error on known symbols, radians
    bool carrierLocked() const;
    bool inverted() const { return inverted_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    double symbolRate_ = 1e6;
    std::atomic<int> plCode_{-1}, isiSel_{-1};
    std::atomic<bool> blocking_{false};
    std::atomic<bool> tracking_{false}, lost_{false}, inverted_{false};
};

} // namespace dvbs
} // namespace dect2
