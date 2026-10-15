// DVB-S/S2 test signal: the generator the synthetic source plays and dvbstool writes to a file.
// The L-band IF of a satellite transponder as complex baseband at any sample rate: a DVB-S, DVB-S2 or DVB-S2X transmitter chain (dvbs_tx.h), root raised
// cosine pulse shaping, noise, carrier offset, symbol clock offset, spectral inversion. Nothing is transmitted.
//
// SynthConfig::modeOpt / modeVal (the synthetic source hands them to makeDvbsSynth):
//   modeOpt[0]  standard: 0 DVB-S2 (default), 1 DVB-S, 2 DVB-S2X (the MODCODs of EN 302 307-2 table 17a; VL-SNR and superframes are not sent)
//   modeOpt[1]  modulation (S2): 0 QPSK (default), 1 8PSK, 2 16APSK, 3 32APSK; S2X also 4 64APSK, 5 128APSK, 6 256APSK. DVB-S is always QPSK
//   modeOpt[2]  code rate, 0 = default (2/3). S2: 1 1/4, 2 1/3, 3 2/5, 4 1/2, 5 3/5, 6 2/3, 7 3/4, 8 4/5, 9 5/6, 10 8/9, 11 9/10. S: 1 1/2, 2 2/3, 3 3/4, 4 5/6, 5 7/8
//               S2X, the MODCODs of the modulation and frame size in the order of table 17a (0 = the first):
//                 normal  QPSK: 1 13/45, 2 9/20, 3 11/20
//                         8PSK: 1 8APSK 5/9-L, 2 8APSK 26/45-L, 3 23/36, 4 25/36, 5 13/18
//                         16APSK: 1 1/2-L, 2 8/15-L, 3 5/9-L, 4 26/45, 5 3/5, 6 3/5-L, 7 28/45, 8 23/36, 9 2/3-L, 10 25/36, 11 13/18, 12 7/9, 13 77/90
//                         32APSK: 1 2/3-L, 2 32/45, 3 11/15, 4 7/9
//                         64APSK: 1 32/45-L, 2 11/15, 3 7/9, 4 4/5, 5 5/6
//                         128APSK: 1 3/4, 2 7/9
//                         256APSK: 1 29/45-L, 2 2/3-L, 3 31/45-L, 4 32/45, 5 11/15-L, 6 3/4
//                 short   QPSK: 1 11/45, 2 4/15, 3 14/45, 4 7/15, 5 8/15, 6 32/45
//                         8PSK: 1 7/15, 2 8/15, 3 26/45, 4 32/45
//                         16APSK: 1 7/15, 2 8/15, 3 26/45, 4 3/5, 5 32/45
//                         32APSK: 1 2/3, 2 32/45   (64APSK and up have no short frames: normal is sent)
//   modeOpt[3]  roll-off: 0 0.35 (default), 1 0.25, 2 0.20, 3 0.15, 4 0.10, 5 0.05
//   modeOpt[4]  frame size (S2): 0 normal (default), 1 short
//   modeOpt[5]  pilots (S2): 0 off (default), 1 on
//   modeOpt[6]  spectral inversion: 0 off (default), 1 on (an LNB with a high-side oscillator turns the spectrum around)
//   modeOpt[7]  S2: 1 = VCM demo, the MODCOD changes from frame to frame (S2 only) (2: also the LNB phase noise of EN 302 307-1 H.8 "typical", 3: "critical")
//   modeVal[0]  symbol rate in Hz (0: 5 Msym/s, or less when the sample rate is too low)
//   modeVal[1]  transmitter clock offset in ppm (the symbol rate is higher by this much)
//   SynthConfig::snrDb is Es/N0 in dB (symbol energy over noise density); cfoHz is the carrier offset; sroPpm adds to modeVal[1].
// The payload is the built-in test programme (demoTsSource): a test card and a beep, carried at the net rate of the chosen mode.
#pragma once
#include "dvbs_tx.h"
#include "mode_synth.h"
#include <functional>
#include <memory>
#include <random>

namespace dect2 {

namespace dvbs {

// Phase noise of an LNB and tuner, a random phase that follows one of the masks of ETSI EN 302 307-1 annex H.8 (single side band, dBc/Hz).
// mask 1: "typical" aggregate (-25 dBc/Hz at 100 Hz, -50 at 1 kHz, -73 at 10 kHz, -93 at 100 kHz, -103 at 1 MHz, -114 above 10 MHz),
// mask 2: "critical" (-85 at 100 kHz), mask 3: the "Ku non DTH" mask of annex M (-79 dBc/Hz at 1 kHz, -89 at 10 kHz, -99 at 100 kHz, -109 at 1 MHz).
// The sequence is made once with an FFT, `rate` samples per second, and repeats after about a second.
class PhaseNoise {
public:
    PhaseNoise(int mask, double rate, uint32_t seed);
    double next() { const double v = th_[pos_]; pos_ = pos_ + 1 == th_.size() ? 0 : pos_ + 1; return v; }
    double rmsDegrees() const { return rms_ * 180.0 / 3.14159265358979323846; }
private:
    std::vector<float> th_;
    size_t pos_ = 0;
    double rms_ = 0;
};

struct DvbsSignalConfig {
    DvbsTxConfig tx;
    double sampleRate = 10e6;
    double snrDb = 30;             // Es/N0; 200 = no noise
    double cfoHz = 0;
    double clockPpm = 0;           // the symbol rate is higher than tx.symbolRate by this many ppm
    bool inverted = false;         // spectral inversion
    double level = 0.2;            // rms of the output before the noise is added is level / sqrt(1 + noise power)
    // receiver front end faults, for tests
    int phaseNoise = 0;            // 0 none, 1 to 3: the masks of PhaseNoise (applied to the output samples)
    double dcOffset = 0;           // constant added to I and Q, relative to the rms of the output
    double iqGainDb = 0, iqPhaseDeg = 0;
    uint32_t seed = 1;
    std::function<void(uint8_t*)> ts;   // packet source; null: the demo programme
    int shaperHalfSpan = 20;       // pulse shaping filter length in symbols on each side of the centre (2 * span must be a multiple of 8)
};

class DvbsSignal {
public:
    explicit DvbsSignal(const DvbsSignalConfig& cfg);
    void generate(cf32* out, size_t n);
    double sampleRate() const { return cfg_.sampleRate; }
    uint64_t packetsSent() const { return tx_->packetsSent(); }
    const DvbsSignalConfig& config() const { return cfg_; }

private:
    DvbsSignalConfig cfg_;
    std::unique_ptr<DvbsTxSource> tx_;
    std::vector<float> taps_;      // phases x 2*half span
    int phases_ = 0, half_ = 0;
    std::vector<float> re_, im_;   // symbol history (sliding)
    size_t have_ = 0;              // symbols in the history
    long long base_ = 0;           // index of the first symbol of the history
    double t_ = 0;                 // position of the next output sample in symbols
    double dt_ = 0;                // symbols per output sample
    std::complex<double> rot_{1.0, 0.0}, step_{1.0, 0.0};   // carrier offset oscillator
    uint64_t rotCount_ = 0, noiseCount_ = 0;
    std::unique_ptr<PhaseNoise> pn_;
    float scale_ = 1, noiseAmp_ = 0;
    uint64_t noiseState_ = 1;
    size_t noisePos_ = 0;
    std::vector<cf32> sc_;
};

// Fills the SynthConfig-independent defaults from the mode options above
DvbsSignalConfig dvbsConfigFromSynth(const SynthConfig& sc, double sampleRate);
// Root raised cosine impulse response at time t (in symbol periods), unit energy per symbol period
double rrcAt(double t, double alpha);

} // namespace dvbs

std::unique_ptr<ModeSynth> makeDvbsSynth(const SynthConfig& cfg, double sampleRate);   // nullptr: no test signal for these options

} // namespace dect2
