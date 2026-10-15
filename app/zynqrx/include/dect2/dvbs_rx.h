// DVB-S/S2 receiver: DVB-S, DVB-S2 and DVB-S2X satellite television (L-band IF after the LNB).
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread (they must be safe while feed() runs).
// The carrier must be near the centre of the input. Its symbol rate, roll-off, modulation and code rate are found by the receiver.
#pragma once
#include "dvbs_tel.h"
#include "mode_tuning.h"
#include "ring.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

class DvbsReceiver {
public:
    DvbsReceiver();
    ~DvbsReceiver();

    void configure(double inputRateHz);
    bool ready() const;                                  // false when the input rate is too low for this mode
    void reset();                                        // after a retune: forget the signal, drop queued output
    void feed(const cf32* x, size_t n);
    bool telemetry(DvbsTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);   // events for the log pane; may be called from the receiver thread
    // The decoded transport stream: packets of 188 bytes, `secs` of signal time since the previous call. Runs on the receiver thread.
    void setPacketCallback(std::function<void(const uint8_t* packets, size_t nPackets, double secs)> cb);

    // ---- controls (any thread)
    void setSymbolRate(double hz);                       // 0 = find it from the spectrum
    void setStandardHint(int standard);                  // 0 automatic, 1 DVB-S only, 2 DVB-S2 only (S2X signalling is reported either way)
    void setRollOff(double rollOff);                     // 0 = automatic: the spectrum, then the stream's own signalling
    void setPrefiltered(bool on);                        // the radio already did the root raised cosine filter and decimation (about 2 samples per symbol): skip them
    void setSpectrumInterval(double secs);               // seconds between spectrum analyses once locked (default 0.25; larger saves CPU on small machines, the display refreshes that slowly)
    void setCenter(bool known, double hz);               // the carrier's offset from the centre of the input; with a symbol rate set too, the spectrum is skipped altogether (no search, no FFT)
    void setIsi(int isi);                                // DVB-S2 multiple input streams: the stream to decode, -1 the first one seen
    void setPlScrambling(int n);                         // DVB-S2 physical layer scrambling code, -1 = 0 (the default for broadcast)
    void setComputeMode(int mode);                       // 0 CPU, 1 GPU, 2 auto: where LDPC (DVB-S2) and Viterbi (DVB-S) run; the output is the same
    void setHuntModulations(int mask);                   // DVB-S2: the search for a signal looks for these modulations only (bit 0 QPSK, 1 8PSK, 2 16APSK, 3 32APSK; 0 or all: every one, S2X too). Fewer frame lengths and code words: much cheaper
    void setPresenceGate(bool on);                       // prefiltered input only: a cheap test for a signal (its symbol rate line) decides whether the timing recovery and the search run while hunting; a short look every 4 s catches signals too weak for the test
    void setConstantModcod(bool on);                     // DVB-S2: the stream keeps the MODCOD it was first found with (CCM): later hunts after a loss of the signal test that code word only (cheap), with a full hunt after every 1.5 s of signal
    void setLdpc8(bool on);                              // DVB-S2: the 8-bit CPU LDPC decoder at every code rate. Much cheaper on a small ARM core; a frame it cannot finish still goes to the float decoder, but low code rates lose sensitivity (about 2 dB at QPSK 2/5)
    void setFastTiming(bool on);                         // cheaper symbol timing once the loop has settled (8 symbols per filter row, timing detector on every second block): about a quarter less time in the timer for a little less accuracy of the sampling instants (about 0.1 dB near the threshold on a signal with phase noise)
    void setBlocking(bool blocking);                     // recordings: wait for the decoder threads instead of dropping frames when behind
    void flush();                                        // waits until everything fed so far is decoded and delivered (recordings, tests; not for the engine)

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning dvbsTuning();

// The highest symbol rate the receiver accepts at an input rate: about 1.3 samples per symbol times (1 + roll-off) of bandwidth
double dvbsMaxSymbolRate(double inputRateHz, double rollOff = 0.35);
// The lowest symbol rate that the spectrum analysis can measure at an input rate (below it a manual symbol rate is needed)
double dvbsMinAutoSymbolRate(double inputRateHz);

} // namespace dect2
