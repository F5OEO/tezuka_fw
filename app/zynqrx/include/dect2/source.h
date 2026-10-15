// IQ sample sources: HackRF, recorded file, built-in synthetic DVB-T2-like signal.
#pragma once
#include "ring.h"
#include "t2gen.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {

// Synthetic test-signal configuration (never transmitted; samples are generated in memory only).
struct SynthConfig {
    TxParams tx;           // FFT size, guard interval, ...
    double snrDb = 30;
    double cfoHz = 0;
    double sroPpm = 0;
    double echoDb = 0;     // 0 = off; otherwise echo attenuation
    int echoDelay = 300;   // samples
    bool atsc = false;     // generate ATSC 8-VSB (6 MHz channel) instead
    bool dab = false;      // generate a DAB (mode I) ensemble instead
    bool demoTv = false;   // DVB-T / ATSC: carry a looping test-card programme (picture and beep) instead of the small test stream
    bool dvbt = false;     // generate DVB-T (with a small test transport stream) instead of DVB-T2
    int dvbtMode = 1, dvbtGuard = 2, dvbtMod = 2, dvbtRate = 1; // 8K, GI 1/8, 64-QAM, 2/3
    int isdbtBwMhz = 6;    // ISDB-T test signal: channel width 6, 7 or 8 MHz
    double pace = 1.0;     // 1 = real time; below 1 the signal runs in slow motion (tests on slow machines: the receiver gets 1/pace times the time)
    bool gainModel = false; // scale the level with the LNA/VGA/amp gains (for testing AGC): 62 dB total = the nominal level
    int mode = 0;           // 0: the DVB / ATSC / DAB settings above; 8 and up: the test signal of that engine standard (see mode_synth.h)
    int modeOpt[8] = {};    // options of that test signal; every mode says what they mean in its <mode>_gen.h
    double modeVal[4] = {};
};

struct TuneSettings {
    double bandwidthMhz = 8;
    int isdbtBwMhz = 6;                // ISDB-T channel width: 6 (Japan, the Americas), 7, 8 MHz (the Maldives); 0 = automatic (6, 7 and 8 tried)
    SynthConfig synth;
    double centerHz = 522e6;
    double sampleRate = 18285714.2857; // 2x native rate for an 8 MHz channel
    double basebandFilterHz = 0;       // 0 = auto
    double gainDb = 30;                // overall gain of generic (SoapySDR) radios; the three HackRF stages below are not used there
    int lnaDb = 16;                    // 0..40 step 8
    int vgaDb = 20;                    // 0..62 step 2
    bool ampOn = false;
    bool biasTee = false;              // DC power up the antenna input (active antennas, LNAs); only radios with DeviceInfo::hasBiasTee act on it
    // The radio's own settings the user changed (DeviceInfo::settings): key -> value. A key that is not here means the setting's default,
    // which is what the driver did before the setting existed.
    std::map<std::string, std::string> radio;
};

// A hardware setting of a radio beyond its gain and antenna power: frequency correction, direct sampling, notch filters, gain modes, ...
// The radio's entry lists the ones it has (DeviceInfo::settings); the app shows them, keeps the user's choices per radio and hands them to
// the driver in TuneSettings::radio, which applies them on start and on every retune.
struct RadioSetting {
    enum Type { Bool, Choice, Number } type = Bool;
    std::string key;                       // the key in TuneSettings::radio
    std::string label, help;               // what the app shows, and its tooltip
    std::vector<std::string> values;       // Choice: the values the driver understands ...
    std::vector<std::string> names;        // ... and what the app shows for each
    double minV = 0, maxV = 0, step = 0;   // Number
    std::string unit;                      // Number: "ppm", "dB", ...
    std::string def;                       // the default: "1"/"0" for Bool, one of values for Choice, a number
    bool restart = false;                  // takes effect when the radio is opened (the app restarts a running receiver)
};

// The values in TuneSettings::radio as the drivers read them (def when the user did not set the key)
inline std::string radioOption(const TuneSettings& s, const std::string& key, const std::string& def = std::string()) {
    const auto it = s.radio.find(key);
    return it == s.radio.end() ? def : it->second;
}
inline bool radioFlag(const TuneSettings& s, const std::string& key, bool def = false) {
    const auto it = s.radio.find(key);
    return it == s.radio.end() ? def : (it->second == "1" || it->second == "true");
}
inline double radioNumber(const TuneSettings& s, const std::string& key, double def = 0) {
    const auto it = s.radio.find(key);
    if (it == s.radio.end() || it->second.empty()) return def;
    char* end = nullptr;
    const double v = strtod(it->second.c_str(), &end);
    return end && end != it->second.c_str() ? v : def;
}
// The frequency correction every radio offers (key "ppm"): the radio's clock error in parts per million, positive when its clock runs fast
// (a signal then shows up below its real frequency)
inline double radioPpm(const TuneSettings& s) { const double p = radioNumber(s, "ppm", 0); return p > -1000 && p < 1000 ? p : 0; }
// The frequency to ask a radio for so that it really receives hz, for radios whose library has no correction of its own: a clock that runs
// fast by ppm tunes every frequency that much too high
inline double ppmCorrectedHz(double hz, double ppm) { return ppm == 0 ? hz : hz / (1.0 + ppm * 1e-6); }
// The descriptor of that setting (step: the finest the radio's library takes, e.g. whole ppm for the RTL-SDR)
RadioSetting ppmSetting(double step = 0.1);
// Descriptors for the drivers' listings
RadioSetting boolSetting(const std::string& key, const std::string& label, const std::string& help, bool def = false, bool restart = false);
RadioSetting choiceSetting(const std::string& key, const std::string& label, const std::string& help, std::vector<std::string> values,
                           std::vector<std::string> names, const std::string& def, bool restart = false);

struct DeviceInfo {
    enum Kind { Synthetic, File, HackRF, Soapy, Native } kind = Synthetic;   // Native: a radio driven by its own library (experimental), see source_native.cpp
    std::string name;   // display name
    std::string serial; // HackRF serial / file path
    std::string board;  // "HackRF One", "HackRF Pro", "airspy", "sdrplay", ...
    // generic radios (SoapySDR)
    std::string soapyArgs;     // device arguments, e.g. "driver=airspy,serial=..."
    std::string nativeArgs;    // what the radio's own library wants to open it (index, serial, URI, ...)
    double maxRateHz = 0;      // fastest complex sample rate the radio offers (0 = unknown)
    double minRateHz = 0;
    // The rate its link carries without losing samples when that is below maxRateHz (a PlutoSDR on its USB 2 cable: about 4 Msps of 16-bit
    // samples), 0 = whatever the radio offers. linkRateFor() (rate_choice.h) keeps a mode within it when the mode fits, else goes above it.
    double steadyRateHz = 0;
    // The rates the radio offers when its driver lists them: each pair a range (lo == hi: one rate), between them rates it cannot run at (the
    // RTL-SDR's 0.3 to 0.9 Msps). Empty = any rate between minRateHz and maxRateHz (or not known). The driver takes the smallest one at or
    // above what is asked for (rate_choice.h).
    std::vector<std::pair<double, double>> rateRanges;
    double gainMinDb = 0, gainMaxDb = 0;
    double minFreqHz = 0, maxFreqHz = 0;   // tuning range of the radio (0 = unknown)
    bool hasBiasTee = false;               // the radio and its library can power the antenna input (TuneSettings::biasTee)
    std::vector<RadioSetting> settings;    // the radio's own settings the app offers (TuneSettings::radio); empty for files and the test signal
    bool isRadio() const { return kind == HackRF || kind == Soapy || kind == Native; }
    bool isGeneric() const { return kind == Soapy || kind == Native; }   // one overall gain, a sample-rate range reported by the radio
};

class IqSource {
public:
    virtual ~IqSource() = default;
    virtual bool start(const TuneSettings& s, IqRing& ring, std::string& err) = 0;
    virtual void stop() = 0;
    virtual bool retune(const TuneSettings& s, std::string& err) = 0; // live changes
    virtual double sampleRate() const = 0;
    virtual bool realtimeHardware() const { return false; }
};

std::vector<DeviceInfo> listHackrfDevices(std::string& err);
// Every other radio that SoapySDR knows (Airspy, SDRplay, RTL-SDR, PlutoSDR, LimeSDR, BladeRF, USRP, ...). Empty when built without SoapySDR.
std::vector<DeviceInfo> listSoapyDevices(std::string& err);
bool soapySupported();
// Radios driven by their own libraries when those are installed: RTL-SDR, Airspy, BladeRF, LimeSDR, PlutoSDR, USRP, SDRplay (experimental).
std::vector<DeviceInfo> listNativeDevices(std::string& err);
std::unique_ptr<IqSource> makeNativeSource(const DeviceInfo& d);
// Every radio found: HackRF first, then the native ones, then the SoapySDR ones (a radio found natively is not listed again through SoapySDR).
std::vector<DeviceInfo> listRadios(std::string& err);
std::unique_ptr<IqSource> makeSource(const DeviceInfo& d);

// File format helpers (sidecar-free for now): .cs8 interleaved signed 8-bit IQ, .cu8 unsigned, .cf32 float.
enum class FileFormat { CS8, CU8, CF32, CI16, CU16 };   // CI16 / CU16 come from SigMF and WAV files only
FileFormat guessFormat(const std::string& path);
// Sample rate in Hz from a file name such as "capture_10Msps.cs8" or "x_2.5msps.cu8"; 0 if the name does not say.
double guessSampleRate(const std::string& path);
// offsetBytes: where the samples start (SigMF / WAV header), endBytes: where they end (0 = end of file), bigEndian: multi-byte samples.
std::unique_ptr<IqSource> makeFileSource(const std::string& path, FileFormat fmt, double sampleRate, bool loop,
                                         uint64_t offsetBytes = 0, uint64_t endBytes = 0, bool bigEndian = false);

} // namespace dect2
