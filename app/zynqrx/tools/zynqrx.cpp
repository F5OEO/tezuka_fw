// zynqrx: DVB-S, DVB-S2 and DVB-S2X test signals and offline reception.
//
//   zynqrx gen out.cs8 [--secs 10] [--rate 10] [--std s|s2] [--mod qpsk|8psk|16apsk|32apsk] [--fec 2/3] [--rs 5] [--roll 0.35] [--short] [--pilots]
//                        [--inv] [--snr 30] [--cfo 0] [--ppm 0] [--vcm] [--pn 0|1|2|3] [--dc 0] [--iq-db 0] [--iq-deg 0] [--seed 1] [--prefiltered]
//                        [--ts in.ts] [--format cs8|cf32]
//       Writes the L-band IF of a satellite transponder as complex baseband: `--rate` is the sample rate in Msps, `--rs` the symbol rate in Msym/s,
//       `--snr` is Es/N0 in dB. The payload is the built-in test programme, or the packets of `--ts` (repeated). Nothing is transmitted.
//       --pn 1..3 adds LNB phase noise (EN 302 307-1 H.8 typical and critical masks, M.2 non DTH mask), --dc and --iq-* receiver faults.
//   zynqrx rx recording [--rate 10] [--format cs8|cu8|cf32] [--rs 0] [--std 0|1|2] [--roll 0] [--prefiltered] [--spec-secs 0.25] [--center Hz] [--isi -1] [--pls -1] [--ts out.ts] [--secs 0]
//       Runs the receiver on a recording (`--rate` in Msps) and prints what it found. `--rs` forces the symbol rate (Msym/s, 0 = from the spectrum),
//       `--std` 1 or 2 limits the search to DVB-S or DVB-S2.
//       --prefiltered: the recording was already root raised cosine filtered and decimated to 2 samples per symbol (by the radio's FPGA): the
//       receiver skips its resampler and matched filter (--rate must be twice the symbol rate; give --roll, the spectrum cannot measure it then).
//       --center Hz (with --rs): the carrier's offset from the centre of the recording is known, so the receiver starts at once and never runs
//       the spectrum search or its FFTs.
//   zynqrx rx --iio [uri] --freq MHz --rate Msps [--bw MHz] [--gain dB] [--rs ..] [--std ..] [--ts out.ts] [--secs 0]
//       Live input from the radio's AD936x through libiio instead of a recording. `uri` is optional (default: the local context on the board; or ip:..., usb:...).
//       --freq is the RF centre frequency in MHz, --gain a fixed RX gain in dB (default: the radio's slow attack AGC), --bw the analog bandwidth (default = rate).
//       --prefiltered (needs --rs; --rate defaults to 2 * --rs, --roll to 0.35, --center to 0): the radio's FIR does the matched filtering: an AD9361 root raised cosine
//       that decimates by 4 (the converter runs at 8 samples per symbol), so the receiver takes 2 samples per symbol and skips its resampler and filter.
//       --max-lag secs (default 4, 0 = off): when the receiver falls this far behind the radio (a weak signal can need more than real time) the oldest
//       samples are dropped instead of letting the delay grow; the receiver finds its lock again afterwards.
//       Runs until --secs or Ctrl-C, then prints the summary. Frames are dropped when the decoder threads are behind. Needs a build with IIO=1.
//   zynqrx rx ... --udp addr:port [--ttl 16] [--udp-if localaddr]
//       Also sends the transport stream over UDP as it is decoded: 7 packets (1316 bytes) per datagram, to a unicast or a multicast (224.0.0.0/4) address.
//       --ttl is the multicast hop limit, --udp-if the local interface address the multicast leaves from. A partial datagram is sent at the end.
//   zynqrx rx ... --ws port
//       Also serves the transport stream to WebSocket clients (ws://board:port/stream.ts) in parallel with --udp: binary messages of up to 7 packets
//       (1316 bytes). A new client first gets the latest PAT and PMT and then the stream as it is (no keyframe detection). --ws-keyframe makes it
//       wait for the next keyframe instead (random access indicator, or an IDR / VPS / SPS / sequence header in the video PES start), so a player
//       can decode from its first packet; it waits 5 s at most for one.
//       A client that falls 4 MB behind is dropped; the receiver never waits for a client.
//   zynqrx rx ... --fast-timing on|off|auto   (also bench; default auto)
//       Cheaper symbol timing once the timing loop has settled: 8 symbols share one row of the interpolator and the timing error detector looks at
//       four of them, so about a quarter less CPU in the timer (and so on the thread that also does the mixer and the hand-over of the symbols).
//       The sampling instants are a little less accurate: about 0.1 dB near the decoding threshold on a signal with phase noise, nothing at 7.5 dB and
//       above in the tests (QPSK 2/3 at 1.5 Msym/s). auto: on above 0.5 Msym/s, when the symbol rate is given.
//   zynqrx rx ... --ldpc8   (also bench)
//       The 8-bit CPU LDPC decoder at every code rate, not only from 2/3 up. Much cheaper on the board; a frame it cannot finish still goes to the float
//       decoder, but low code rates lose sensitivity (about 2 dB at QPSK 2/5 on the test signals), and it is not faster on the board for a weak signal. DVB-S2 only.
//   zynqrx rx --iio --mqtt broker[:port] [--freq MHz --rs Msym] [--udp addr:port] ...
//       Control and status over MQTT in the scheme of longmynd (F5OEO/longmynd, mymqtt.c): the scripts and dashboards made for that receiver work with it.
//       Commands (cmd/zynqrx/...): frequency (kHz, 70000 - 6000000), sr (kS, 33 - 7680), tsip (IPv4 of the transport stream output, the port stays that
//       of --udp, else 1234), swport and polar (accepted and reported back, no such hardware). A new frequency or symbol rate restarts the receiver
//       (a few seconds); tsip takes effect at once. Without --freq/--rs the receiver waits for frequency and sr.
//       Process control: without --start the receiver is stopped at launch (only MQTT and the --ws/--udp outputs are up, the radio is not opened);
//       --start begins receiving at once. cmd/zynqrx/process start|stop starts and stops it at run time (frequency and sr sent while stopped are kept
//       for the next start); dt/zynqrx/process is start or stop, also sent at once on a command.
//       Status (dt/zynqrx/...), every second, not retained, QoS 2: process (start, stop), rx_state (Init, Hunting, found header, demod_s, demod_s2), set/sr|frequency|swport|
//       tsip, lna_gain, agc2, puncrate, carrier_frequency (kHz), lnb_supply, polarisation, symbolrate (kS), viterbi_error, ber, mer (dB), margin_db,
//       bch_uncorect, bch_errors, ldpc_errors, service_name, provider_name, ts_null (%), es_pid/es_type, modulation, fec, short_frame, pilots,
//       rolloff, matype1, matype2. Not sent: poweri/powerq, constel_i/q, agc1, videobuffer/audiobuffer/transdelay (no such measurements here).
//       ber and ldpc_errors are estimates from the pre-FEC bit error rate; bch_errors is 0 (the number of corrected bits is not kept).
//       mer is sent from "found header" on (it is measured on the header and pilot symbols); ber, ldpc_errors and margin_db only once the transport stream is locked.
//       From 261 kS the radio's RRC filter is used (2 samples per symbol); below, the radio runs at 2.1 Msps and the receiver filters in software.
//   zynqrx rx --iio ... --nice N   (default -10 for live radio input and --mqtt, else 0; needs root)
//       Scheduling priority of the receiver. The firmware's own processes use part of the board's two cores; the receiver has the deadlines.
//   zynqrx rx ... --ccm
//       Constant coding and modulation: the stream keeps the MODCOD it was first found with (not VCM or ACM). After a first verified lock, the search for the
//       signal (it comes back after a loss, e.g. a satellite beacon that goes and comes) looks for that code word and frame length only, far cheaper than the
//       256 code words and every frame length; a full search runs after every 1.5 s of signal, so a MODCOD that changed is found too. The spectrum sense is remembered
//       after any verified lock, with or without --ccm.
//   zynqrx rx ... --mods qpsk,8psk
//       The search for a signal looks for these DVB-S2 modulations only (qpsk, 8psk, 16apsk, 32apsk; default all, S2X too). With QPSK and 8PSK there are about
//       nine frame lengths to test instead of forty and fewer code words to fit: the search costs a fraction, which matters when the signal is often absent.
//   zynqrx rx --iio --prefiltered ... --presence | --no-presence
//       Presence gate: a signal is looked for only while a cheap test (the line at the symbol rate in the input at two samples per symbol) sees one, plus a look of
//       0.6 s every 4 s for signals too weak for it. Without a signal the receiver then costs almost nothing instead of a whole processor. Default on with --mqtt.
//   zynqrx bench [gen options] [--prefiltered] [--center Hz] [--blocking] [--hint] [--realtime]
//       Makes the test signal in memory, runs the receiver on it the way the engine does (frames are dropped when the decoder threads are behind) and
//       reports the CPU time of the receiver thread and of all threads per second of signal.
//   DECT2_COMPUTE=cpu|gpu|auto selects where the error correction runs; DECT2_DVBS_REF=1 the DVB-S chain as it was before the speed-up.
//   zynqrx ranges
//       The symbol rates the receiver accepts at each input sample rate.
#include "dect2/dvbs_gen.h"
#include "dect2/dvbs_rx.h"
#include "dvbs_s1.h"
#include "dvbs_carrier.h"
#include "iio_source.h"
#include "udp_out.h"
#include "ws_out.h"
#include "datv_mqtt.h"
#include "ts_psi.h"
#include <csignal>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <deque>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#include <arpa/inet.h>
#endif
#include <time.h>

using namespace dect2;
using namespace dect2::dvbs;

namespace {

int parseRateDvbs(const std::string& s) {
    static const char* n[5] = {"1/2", "2/3", "3/4", "5/6", "7/8"};
    for (int i = 0; i < 5; i++) if (s == n[i]) return i;
    return -1;
}
int parseRateDvbs2(const std::string& s) {
    for (int i = 0; i < kS2Rates; i++) if (s == s2RateName(i)) return i;
    return -1;
}

void writeSamples(std::ofstream& f, const cf32* x, size_t n, bool cf32out) {
    if (cf32out) { f.write((const char*)x, (std::streamsize)(n * sizeof(cf32))); return; }
    std::vector<int8_t> b(2 * n);
    for (size_t i = 0; i < n; i++) {
        b[2 * i] = (int8_t)std::max(-128.f, std::min(127.f, std::round(x[i].real() * 128.f)));
        b[2 * i + 1] = (int8_t)std::max(-128.f, std::min(127.f, std::round(x[i].imag() * 128.f)));
    }
    f.write((const char*)b.data(), (std::streamsize)b.size());
}

struct GenOptions {
    std::string out, tsIn, format;
    double secs = 10, rate = 10, rs = 0;
    DvbsSignalConfig c;
    std::string fecName = "2/3";
    bool prefiltered = false, blocking = false, hint = false, realtime = false, ldpc8 = false;
    int fastTiming = 2;   // 0 off, 1 on, 2 auto (on above 0.5 Msym/s)
    double specSecs = 0.25, center = 0;
    bool haveCenter = false;
};

// returns 0 when the options are fine
int parseGen(int argc, char** argv, int first, GenOptions& g, bool needOut) {
    DvbsSignalConfig& c = g.c;
    c.tx.standard = 2;
    for (int i = first; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--secs") g.secs = atof(next().c_str());
        else if (a == "--rate") g.rate = atof(next().c_str());
        else if (a == "--std") { const std::string v = next(); c.tx.standard = v == "s" || v == "1" ? 1 : 2; }
        else if (a == "--mod") { const std::string v = next(); c.tx.mod = v == "8psk" ? k8psk : v == "16apsk" ? k16apsk : v == "32apsk" ? k32apsk : kQpsk; }
        else if (a == "--fec") g.fecName = next();
        else if (a == "--rs") g.rs = atof(next().c_str());
        else if (a == "--roll") c.tx.rollOff = atof(next().c_str());
        else if (a == "--short") c.tx.shortFrame = true;
        else if (a == "--prefiltered") g.prefiltered = true;
        else if (a == "--blocking") g.blocking = true;
        else if (a == "--hint") g.hint = true;
        else if (a == "--ldpc8") g.ldpc8 = true;
        else if (a == "--fast-timing") { const std::string v = next(); g.fastTiming = v == "on" ? 1 : v == "off" ? 0 : 2; }
        else if (a == "--realtime") g.realtime = true;
        else if (a == "--spec-secs") g.specSecs = atof(next().c_str());
        else if (a == "--center") { g.center = atof(next().c_str()); g.haveCenter = true; }
        else if (a == "--pilots") c.tx.pilots = true;
        else if (a == "--inv") c.inverted = true;
        else if (a == "--snr") c.snrDb = atof(next().c_str());
        else if (a == "--cfo") c.cfoHz = atof(next().c_str());
        else if (a == "--ppm") c.clockPpm = atof(next().c_str());
        else if (a == "--vcm") c.tx.vcm = true;
        else if (a == "--pn") c.phaseNoise = atoi(next().c_str());
        else if (a == "--dc") c.dcOffset = atof(next().c_str());
        else if (a == "--iq-db") c.iqGainDb = atof(next().c_str());
        else if (a == "--iq-deg") c.iqPhaseDeg = atof(next().c_str());
        else if (a == "--seed") c.seed = (uint32_t)atoi(next().c_str());
        else if (a == "--ts") g.tsIn = next();
        else if (a == "--format") g.format = next();
        else if (g.out.empty() && a[0] != '-' && needOut) g.out = a;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
    }
    const int fec = c.tx.standard == 1 ? parseRateDvbs(g.fecName) : parseRateDvbs2(g.fecName);
    if (fec < 0) { fprintf(stderr, "code rate %s does not exist in this standard\n", g.fecName.c_str()); return 1; }
    c.tx.rate = fec;
    if (c.tx.standard == 1) c.tx.mod = kQpsk;
    c.sampleRate = g.rate * 1e6;
    c.tx.symbolRate = g.rs > 0 ? g.rs * 1e6 : std::min(5e6, dvbsMaxSymbolRate(c.sampleRate, c.tx.rollOff));
    if (c.tx.standard == 2 && !s2Dims(c.tx.mod, c.tx.rate, c.tx.shortFrame).ok) { fprintf(stderr, "%s %s does not exist as a %s frame\n", s2ModName(c.tx.mod), g.fecName.c_str(), c.tx.shortFrame ? "short" : "normal"); return 1; }
    if (c.tx.symbolRate > dvbsMaxSymbolRate(c.sampleRate, c.tx.rollOff) * 1.001)
        fprintf(stderr, "warning: %.3f Msym/s needs more than %.1f Msps (the receiver takes up to %.3f Msym/s there)\n", c.tx.symbolRate / 1e6, g.rate, dvbsMaxSymbolRate(c.sampleRate, c.tx.rollOff) / 1e6);
    return 0;
}

int gCompute = -1;   // DECT2_COMPUTE=cpu|gpu|auto

double threadCpu() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec; }
#ifdef _WIN32
double processCpu() {   // user plus kernel time of the whole process, in 100 ns units
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    auto s = [](const FILETIME& f) { return (double)(((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime) * 1e-7; };
    return s(k) + s(u);
}
#else
double processCpu() { rusage r; getrusage(RUSAGE_SELF, &r); return (double)r.ru_utime.tv_sec + 1e-6 * (double)r.ru_utime.tv_usec + (double)r.ru_stime.tv_sec + 1e-6 * (double)r.ru_stime.tv_usec; }
#endif

// What the radio's FPGA would do: the receive root raised cosine filter at two samples per symbol (needs --rate = 2 * --rs); the power is kept
bool rxRrcFilter(std::vector<cf32>& x, const GenOptions& g) {
    if (std::fabs(g.c.sampleRate - 2 * g.c.tx.symbolRate) > 1e-3 * g.c.sampleRate) { fprintf(stderr, "--prefiltered needs --rate = 2 * --rs\n"); return false; }
    const size_t total = x.size();
    std::vector<float> h;
    for (int k = -24; k <= 24; k++) h.push_back((float)rrcAt(0.5 * k, g.c.tx.rollOff));
    std::vector<cf32> y(total);
    double pin = 0, pout = 0;
    for (size_t i = 0; i < total; i++) {
        cf32 acc(0, 0);
        for (int k = -24; k <= 24; k++) { const long j = (long)i + k; if (j >= 0 && j < (long)total) acc += x[(size_t)j] * h[(size_t)(k + 24)]; }
        y[i] = acc; pin += std::norm(x[i]); pout += std::norm(acc);
    }
    const float sc = pout > 0 ? (float)std::sqrt(pin / pout) : 1.f;
    for (size_t i = 0; i < total; i++) x[i] = y[i] * sc;
    return true;
}

int cmdBench(int argc, char** argv) {
    GenOptions g;
    g.secs = 4;
    if (parseGen(argc, argv, 2, g, false)) return 1;
    DvbsSignal sig(g.c);
    const size_t total = (size_t)(g.secs * g.c.sampleRate);
    std::vector<cf32> x(total);
    for (size_t i = 0; i < total; i += 65536) sig.generate(x.data() + i, std::min<size_t>(65536, total - i));
    if (g.prefiltered && !rxRrcFilter(x, g)) return 1;
    for (auto& v : x) { v = cf32(std::round(std::min(127.f, std::max(-128.f, v.real() * 128.f))) / 128.f, std::round(std::min(127.f, std::max(-128.f, v.imag() * 128.f))) / 128.f); }
    DvbsReceiver rx;
    if (gCompute >= 0) rx.setComputeMode(gCompute);
    rx.configure(g.c.sampleRate);
    rx.setBlocking(g.blocking);   // default: frames are dropped when the decoder threads are behind, as in the engine
    rx.setPrefiltered(g.prefiltered);
    rx.setLdpc8(g.ldpc8);
    rx.setFastTiming(g.fastTiming == 1 || (g.fastTiming == 2 && g.c.tx.symbolRate > 0.5e6));
    if (g.hint) rx.setStandardHint(g.c.tx.standard);   // search only for the standard that was generated (skips the DVB-S Viterbi attempts)
    rx.setSpectrumInterval(g.specSecs);
    if (g.haveCenter) { rx.setCenter(true, g.center); if (!g.prefiltered) rx.setSymbolRate(g.c.tx.symbolRate); }
    if (g.prefiltered) { rx.setSymbolRate(g.c.tx.symbolRate); rx.setRollOff(g.c.tx.rollOff); }
    uint64_t packets = 0;
    rx.setPacketCallback([&](const uint8_t*, size_t n, double) { packets += n; });
    const double c0 = processCpu(), t0 = threadCpu();
    const auto w0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < total; i += 65536) {
        rx.feed(x.data() + i, std::min<size_t>(65536, total - i));
        // --realtime: hand the samples over at the rate they would arrive, so frames dropped mean the machine cannot keep up
        if (g.realtime) std::this_thread::sleep_until(w0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>((double)std::min(i + 65536, total) / g.c.sampleRate)));
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
    const double tc = threadCpu() - t0;
    rx.flush();                                  // the decoder threads may still be working on the last frames
    const double pc = processCpu() - c0;
    DvbsTelemetry t;
    rx.telemetry(t, 0);
    printf("%s\n", dvbsSummary(t).c_str());
    printf("%.1f s of signal at %.1f Msps, %.3f Msym/s: receiver thread %.2f s CPU (%.1fx real time), all threads %.2f s CPU (%.1fx), wall %.2f s; frames/packets ok %llu bad %llu; %llu transport stream packets\n", g.secs, g.rate, g.c.tx.symbolRate / 1e6, tc, g.secs / tc,
           pc, g.secs / pc, wall, (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, (unsigned long long)packets);
    return 0;
}

int cmdGen(int argc, char** argv) {
    GenOptions g;
    if (parseGen(argc, argv, 2, g, true)) return 1;
    if (g.out.empty()) { fprintf(stderr, "usage: zynqrx gen out.cs8 [options] (see the top of tools/zynqrx.cpp)\n"); return 1; }
    DvbsSignalConfig& c = g.c;
    const std::string& out = g.out;
    const std::string& tsIn = g.tsIn;
    const double secs = g.secs, rate = g.rate;
    const std::string& fecName = g.fecName;
    std::string format = g.format;
    if (format.empty()) format = out.size() > 5 && out.substr(out.size() - 5) == ".cf32" ? "cf32" : "cs8";
    std::vector<uint8_t> tsData;
    if (!tsIn.empty()) {
        std::ifstream t(tsIn, std::ios::binary);
        if (!t) { fprintf(stderr, "cannot open %s\n", tsIn.c_str()); return 1; }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        for (size_t o = 0; o + 188 <= raw.size(); o += 188) if (raw[o] == 0x47) tsData.insert(tsData.end(), raw.begin() + (std::ptrdiff_t)o, raw.begin() + (std::ptrdiff_t)o + 188);
        if (tsData.empty()) { fprintf(stderr, "%s holds no transport stream packets\n", tsIn.c_str()); return 1; }
        size_t pos = 0;
        c.ts = [&tsData, pos](uint8_t* p) mutable { memcpy(p, &tsData[pos], 188); pos = (pos + 188) % tsData.size(); };
    }
    DvbsSignal sig(c);
    std::ofstream f(out, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    const size_t total = (size_t)(secs * c.sampleRate);
    std::vector<cf32> buf(1 << 16);
    const auto t0 = std::chrono::steady_clock::now();
    if (g.prefiltered) {
        std::vector<cf32> all(total);
        for (size_t done = 0; done < total; done += buf.size()) sig.generate(all.data() + done, std::min(buf.size(), total - done));
        if (!rxRrcFilter(all, g)) return 1;
        writeSamples(f, all.data(), total, format == "cf32");
    } else
    for (size_t done = 0; done < total; done += buf.size()) {
        const size_t m = std::min(buf.size(), total - done);
        sig.generate(buf.data(), m);
        writeSamples(f, buf.data(), m, format == "cf32");
    }
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (c.tx.standard == 1) printf("DVB-S QPSK %s", fecName.c_str());
    else printf("DVB-S2 %s %s%s%s", s2ModName(c.tx.mod), fecName.c_str(), c.tx.shortFrame ? " short" : "", c.tx.pilots ? " pilots" : "");
    printf(", %.3f Msym/s, roll-off %.2f, %.1f Msps, Es/N0 %.1f dB%s%s: %.1f s written to %s (%s, %.1fx real time), net bit rate %.3f Mbit/s\n", c.tx.symbolRate / 1e6, c.tx.rollOff, rate, c.snrDb,
           c.inverted ? ", spectrum inverted" : "", c.tx.vcm ? ", VCM" : "", secs, out.c_str(), format.c_str(), secs / std::max(1e-9, dt), dvbsNetBitrate(c.tx) / 1e6);
    return 0;
}

volatile std::sig_atomic_t gStop = 0;
void onSigint(int) { gStop = 1; }

// The options of "rx"
struct RxOpts {
    std::string path, tsOut, format, iioUri, udpDest, udpIf, mqttHost;
    double rate = 10, rs = 0, roll = 0, secsMax = 0, specSecs = 0.25, center = 0, freqMHz = 0, bwMHz = 0, gainDb = -1, maxLag = 4;
    int stdHint = 0, isi = -1, pls = -1, udpTtl = 16, mqttPort = 1883, niceValue = 99, wsPort = 0; bool wsKeyframe = false;   // niceValue 99: not given
    int modsMask = 0;
    bool presence = false;
    bool prefilt = false, ldpc8 = false, ccm = false, haveCenter = false, useIio = false, rateGiven = false;
    int fastTiming = 2;                       // --fast-timing: 0 off, 1 on, 2 auto (on above 0.5 Msym/s)
    bool start = false;                       // --start: with --mqtt, begin receiving at once instead of waiting for cmd/zynqrx/process start
};

// Shared between a receive session and the MQTT thread
struct RxCtl {
    std::atomic<bool> stop{false};            // ends the running session
    std::atomic<bool> locked{false};          // the running session has had a transport stream lock (the lag limit applies from then on)
    std::mutex mu;                            // guards everything below
    std::function<void()> cancelRead;         // wakes the reader thread of the session
    bool restart = false;                     // a new frequency or symbol rate wants a new session
    bool enabled = false;                     // the receiver process is started (--start, or cmd/zynqrx/process start); stopped, the radio is not opened
    long freqKHz = 0, srKS = 0, swport = 0;
    int lnbSupply = 0, lnbHorizontal = 0;
    std::string tsip;
    std::mutex udpMu;                         // the transport stream output moves to a new address while packets are being sent
    UdpOut udp;
    WsOut ws;                                 // the same stream for WebSocket clients (own locking)
    int udpPort = 1234, udpTtl = 16;
    std::string udpIf;
    MqttLink* mq = nullptr;
    void requestStop() { stop = true; std::lock_guard<std::mutex> lk(mu); if (cancelRead) cancelRead(); }
};

// One receiver session: opens the input, runs until the input ends, --secs, a signal or ctl.stop, prints the summary. Returns 0 when it ran.
int runSession(RxOpts& o, RxCtl& ctl) {
    ctl.locked = false;
    std::string& path = o.path; std::string& tsOut = o.tsOut; std::string format = o.format;
    double& rate = o.rate; double& rs = o.rs; double& roll = o.roll;
    const bool useIio = o.useIio, prefilt = o.prefilt;
    std::ifstream f;
    IioSource iio;
    if (useIio) { if (!iio.open(o.iioUri, rate * 1e6, o.freqMHz * 1e6, o.bwMHz * 1e6, o.gainDb, prefilt ? roll : 0)) return 1; }
    else { f.open(path, std::ios::binary); if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return 1; } }
    // what the receiver's callbacks use is declared before the receiver, so that it is destroyed after it (the receiver's threads may still call them
    // until its destructor has joined them, which is the case when a session is ended without flushing)
    std::vector<uint8_t> ts;
    uint64_t packets = 0, bad = 0, nulls = 0, ccErrors = 0;
    std::atomic<uint64_t> winPackets{0}, winNulls{0};     // for the null percentage in the status
    std::map<int, uint64_t> pids;
    std::map<int, int> lastCc;
    TsPsi psi;
    double signalSecs = 0;
    DvbsReceiver rx;
    if (gCompute >= 0) rx.setComputeMode(gCompute);
    rx.configure(rate * 1e6);
    rx.setBlocking(true);                     // wait for the decoder threads instead of dropping frames (live input is queued by a reader thread)
    if (rs > 0) rx.setSymbolRate(rs * 1e6);
    if (o.stdHint) rx.setStandardHint(o.stdHint);
    if (roll > 0) rx.setRollOff(roll);
    rx.setPrefiltered(prefilt);
    rx.setLdpc8(o.ldpc8);
    rx.setFastTiming(o.fastTiming == 1 || (o.fastTiming == 2 && o.rs > 0.5));   // auto: only when the symbol rate is given
    rx.setConstantModcod(o.ccm);
    rx.setPresenceGate(o.presence);
    rx.setHuntModulations(o.modsMask);
    rx.setSpectrumInterval(o.specSecs);
    if (o.haveCenter) { if (rs <= 0) { fprintf(stderr, "--center needs --rs\n"); return 1; } rx.setCenter(true, o.center); }
    if (!rx.ready()) { fprintf(stderr, "%.2f Msps is below the %.1f Msps this mode needs\n", rate, dvbsTuning().minSampleRate / 1e6); return 1; }
    if (o.isi >= 0) rx.setIsi(o.isi);
    if (o.pls >= 0) rx.setPlScrambling(o.pls);
    if (useIio) { std::lock_guard<std::mutex> lk(ctl.mu); ctl.cancelRead = [&iio] { iio.cancel(); }; }
    rx.setPacketCallback([&](const uint8_t* p, size_t n, double secs) {
        signalSecs += secs;
        { std::lock_guard<std::mutex> lk(ctl.udpMu); if (ctl.udp.isOpen()) ctl.udp.push(p, n); }
        ctl.ws.push(p, n);
        for (size_t i = 0; i < n; i++, p += 188) {
            packets++;
            winPackets++;
            const int pid = (p[1] & 0x1F) << 8 | p[2];
            if (p[1] & 0x80) { bad++; continue; }
            pids[pid]++;
            if (pid == 0x1FFF) { nulls++; winNulls++; continue; }
            if (ctl.mq && (pid == 0 || pid == 17 || pid > 31)) psi.push(p);
            const int cc = p[3] & 15, af = (p[3] >> 4) & 3;
            auto it = lastCc.find(pid);
            if ((af & 1) && it != lastCc.end() && cc != (it->second + 1) % 16 && !(p[3] & 0x20 && af == 2)) ccErrors++;
            if (af & 1) lastCc[pid] = cc;
        }
        if (!tsOut.empty()) ts.insert(ts.end(), p - n * 188, p);
    });
    rx.setLogCallback([&](const std::string& s) { printf("[%7.2f s] %s\n", signalSecs, s.c_str()); });
    const size_t bps = format == "cf32" ? 8 : 2, block = 1 << 16;
    std::vector<unsigned char> raw(block * bps);
    std::vector<cf32> x(block);
    uint64_t total = 0, seq = 0, nextReport = (uint64_t)(rate * 1e6);
    DvbsTelemetry tel;
    // Live input: a thread reads the radio into a queue (up to 128 MB, about 20 s at 0.7 Msps) so the samples that arrive while the receiver is
    // busy with its lock hunt are kept, and the receiver catches up afterwards. When the queue is full the newest samples are lost, as the kernel would.
    std::deque<std::vector<cf32>> q;
    std::mutex qMu;
    std::condition_variable qCv;
    size_t queued = 0;
    bool readerDone = false;
    uint64_t lost = 0;
    std::atomic<bool> sessionEnd{false};
    auto ending = [&] { return gStop != 0 || ctl.stop.load() || sessionEnd.load(); };
    std::thread reader;
    if (useIio) reader = std::thread([&] {
        std::vector<cf32> b(IioSource::kBlock);
        while (!ending()) {
            const size_t n = iio.read(b.data(), b.size());
            std::lock_guard<std::mutex> lk(qMu);
            if (!n) break;
            if ((queued + n) * sizeof(cf32) > (size_t)128 << 20) { lost += n; continue; }
            q.emplace_back(b.begin(), b.begin() + (std::ptrdiff_t)n);
            queued += n;
            // the receiver cannot keep up (a weak signal makes the error correction work harder): rather than let the delay grow without end, the
            // oldest samples are dropped until half the allowed lag is queued; the receiver loses its frame lock and finds it again on what follows
            // (not before the first lock: the search for it needs the samples in a row, and the backlog it builds is caught up afterwards)
            if (o.maxLag > 0 && ctl.locked.load() && (double)queued > o.maxLag * rate * 1e6) {
                size_t dropped = 0;
                while (!q.empty() && (double)queued > 0.5 * o.maxLag * rate * 1e6) { dropped += q.front().size(); queued -= q.front().size(); q.pop_front(); }
                lost += dropped;
                fprintf(stderr, "live input: the receiver is %.1f s behind, dropped %.1f s of signal\n", o.maxLag, (double)dropped / (rate * 1e6));
            }
            qCv.notify_one();
        }
        std::lock_guard<std::mutex> lk(qMu);
        readerDone = true;
        qCv.notify_one();
    });
    // status for MQTT, once a second
    auto nextStatus = std::chrono::steady_clock::now();
    uint64_t lastBchBad = 0;
    auto publishStatus = [&] {
        DatvStatus st;
        { std::lock_guard<std::mutex> lk(ctl.mu); st.freqKHz = ctl.freqKHz; st.srKS = ctl.srKS; st.swport = ctl.swport; st.tsip = ctl.tsip; st.lnbSupply = ctl.lnbSupply; st.lnbHorizontal = ctl.lnbHorizontal; }
        st.running = true;
        st.tel = tel; st.haveTel = tel.seq != 0;
        st.state = !st.haveTel ? 0 : tel.state >= 2 ? (tel.standard == 1 ? 3 : 4) : (tel.state == 1 && tel.lockFrame) ? 2 : 1;   // 1 Hunting, 2 found header
        const uint64_t pk = winPackets.exchange(0), nl = winNulls.exchange(0);
        st.nullPercent = pk ? (int)((nl * 100 + pk / 2) / pk) : 0;
        st.bchBadSince = tel.bchBad > lastBchBad ? tel.bchBad - lastBchBad : 0;
        lastBchBad = tel.bchBad;
        st.gainDb = useIio ? iio.gainDb() : -1;
        st.matype1 = tel.matype1; st.matype2 = tel.matype2;
        psi.get(st.serviceName, st.providerName, st.es);
        datvPublish(*ctl.mq, st);
    };
    const auto t0 = std::chrono::steady_clock::now();
    while (useIio || f) {
        size_t got;
        if (useIio) {
            std::unique_lock<std::mutex> lk(qMu);
            qCv.wait_for(lk, std::chrono::milliseconds(200), [&] { return !q.empty() || readerDone; });
            if (q.empty()) {
                lk.unlock();
                if (readerDone || ending()) break;
                if (ctl.mq && std::chrono::steady_clock::now() >= nextStatus) { nextStatus += std::chrono::seconds(1); publishStatus(); }
                continue;
            }
            if (ending()) break;
            x = std::move(q.front()); q.pop_front(); queued -= x.size();
            got = x.size();
            x.resize(block);
        } else {
            f.read((char*)raw.data(), (std::streamsize)raw.size());
            got = (size_t)f.gcount() / bps;
        }
        if (!got) break;
        for (size_t i = 0; i < got && !useIio; i++) {
            if (format == "cf32") { float re, im; memcpy(&re, &raw[i * 8], 4); memcpy(&im, &raw[i * 8 + 4], 4); x[i] = cf32(re, im); }
            else if (format == "cu8") x[i] = cf32(((int)raw[2 * i] - 127.5f) / 127.5f, ((int)raw[2 * i + 1] - 127.5f) / 127.5f);
            else x[i] = cf32((int8_t)raw[2 * i] / 128.f, (int8_t)raw[2 * i + 1] / 128.f);
        }
        rx.feed(x.data(), got);
        total += got;
        DvbsTelemetry t;
        if (rx.telemetry(t, seq)) { seq = t.seq; tel = t; if (tel.tsLock) ctl.locked = true; }
        if (total >= nextReport) { nextReport += (uint64_t)(rate * 1e6); printf("[%7.2f s] %s\n", (double)total / (rate * 1e6), dvbsSummary(tel).c_str()); }
        if (ctl.mq && std::chrono::steady_clock::now() >= nextStatus) { nextStatus += std::chrono::seconds(1); publishStatus(); }
        if (o.secsMax > 0 && (double)total / (rate * 1e6) >= o.secsMax) break;
    }
    sessionEnd = true;
    if (useIio) iio.cancel();
    if (reader.joinable()) reader.join();
    { std::lock_guard<std::mutex> lk(ctl.mu); ctl.cancelRead = nullptr; }
    if (lost) fprintf(stderr, "live input: %.2f s of signal lost while the receiver was busy\n", (double)lost / (rate * 1e6));
    if (ctl.stop.load() || gStop) { std::lock_guard<std::mutex> lk(qMu); q.clear(); }   // a new session or the end: the backlog is of no use
    else rx.flush();
    { std::lock_guard<std::mutex> lk(ctl.udpMu); ctl.udp.flush(); }
    ctl.ws.flush();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    DvbsTelemetry t;
    if (rx.telemetry(t, 0)) tel = t;
    printf("\nread %.2f s of signal in %.1f s (%.1fx real time)\n", (double)total / (rate * 1e6), wall, (double)total / (rate * 1e6) / std::max(1e-9, wall));
    printf("%s\n", dvbsSummary(tel).c_str());
    if (tel.standard) {
        printf("  symbol rate %.4f Msym/s (spectrum %.4f), roll-off %.2f (%s), carrier %+.1f kHz, spectrum %s\n", tel.symbolRate / 1e6, tel.symbolRateSpectrum / 1e6, tel.rollOff,
               tel.rollOffSource == 2 ? "signalled" : tel.rollOffSource == 1 ? "measured" : "assumed", tel.cfoHz / 1e3, tel.inverted ? "inverted" : "normal");
        printf("  Es/N0 %.1f dB (MER)  pre-FEC BER %s  frames/packets ok %llu bad %llu", tel.merDb, tel.preFecBer < 0 ? "?" : (std::to_string(tel.preFecBer)).c_str(), (unsigned long long)tel.blocksOk, (unsigned long long)tel.blocksBad);
        if (tel.standard >= 2) printf("  LDPC iterations %.1f  BCH bad %llu  user packet CRC errors %llu  dummy frames %llu", tel.ldpcIterAvg, (unsigned long long)tel.bchBad, (unsigned long long)tel.crcErrors, (unsigned long long)tel.framesDummy);
        else printf("  RS clean %llu corrected %llu failed %llu", (unsigned long long)tel.rsClean, (unsigned long long)tel.rsCorrected, (unsigned long long)tel.rsFailed);
        printf("\n");
        if (!tel.signalNote.empty()) printf("  note: %s\n", tel.signalNote.c_str());
    }
    printf("transport stream: %llu packets (%llu with the error indicator, %llu null, %llu continuity errors), signal time %.2f s", (unsigned long long)packets, (unsigned long long)bad, (unsigned long long)nulls, (unsigned long long)ccErrors, signalSecs);
    if (signalSecs > 0) printf(", %.3f Mbit/s", (double)packets * 188 * 8 / signalSecs / 1e6);
    printf("\n");
    int shown = 0;
    for (const auto& kv : pids) { if (shown++ >= 8) break; printf("  PID %4d (0x%04X): %llu packets\n", kv.first, kv.first, (unsigned long long)kv.second); }
    if (!tsOut.empty()) { std::ofstream of(tsOut, std::ios::binary); of.write((const char*)ts.data(), (std::streamsize)ts.size()); printf("written to %s\n", tsOut.c_str()); }
    return packets == 0 ? 2 : 0;
}

// The receiver for the session of the longmynd way: frequency in kHz and symbol rate in kS from the command line or from MQTT
bool datvSessionOpts(RxOpts& o, long freqKHz, long srKS) {
    o.freqMHz = (double)freqKHz / 1000.0;
    o.rs = (double)srKS / 1000.0;
    o.secsMax = 0;
    o.useIio = true;
    o.haveCenter = true;               // the carrier is taken to be on the radio's frequency: the receiver then needs no spectrum search
    o.center = 0;
    if (o.rs * 2 >= 2.083 / 4 - 1e-4) {
        // the radio's own RRC filter, 2 samples per symbol (0.52 - 15.36 Msps): 261 kS and up
        o.prefilt = true;
        o.rate = 2 * o.rs;
        if (o.roll <= 0) o.roll = 0.35;
    } else {
        // below that the converter cannot run at 4 times the output rate: the plain 2.1 Msps stream and the receiver's own matched filter
        o.prefilt = false;
        o.rate = 2.1;
    }
    return true;
}

// Runs sessions under MQTT control: cmd/zynqrx/{frequency,sr,tsip,swport,polar} in, dt/zynqrx/... out
int runDaemon(RxOpts& o) {
    RxCtl ctl;
    MqttLink mq;
    ctl.mq = &mq;
    ctl.freqKHz = std::lround(o.freqMHz * 1000.0);
    ctl.srKS = std::lround(o.rs * 1000.0);
    ctl.udpTtl = o.udpTtl;
    ctl.udpIf = o.udpIf;
    ctl.enabled = o.start;
    if (!o.udpDest.empty()) {
        const size_t c = o.udpDest.rfind(':');
        ctl.tsip = o.udpDest.substr(0, c);
        if (c != std::string::npos) ctl.udpPort = atoi(o.udpDest.c_str() + c + 1);
        if (!ctl.udp.open(o.udpDest, o.udpTtl, o.udpIf)) return 1;
    }
    if (o.wsPort > 0) { ctl.ws.waitForKeyframe(o.wsKeyframe); if (!ctl.ws.open(o.wsPort)) return 1; }
    auto onCommand = [&ctl](const std::string& key, const std::string& val) {
        char* end = nullptr;
        if (key == "frequency" || key == "sr") {
            const long v = strtol(val.c_str(), &end, 10);
            const bool freq = key == "frequency";
            if (val.empty() || *end || v < (freq ? 70000L : 33L) || v > (freq ? 6000000L : 7680L)) {
                fprintf(stderr, "mqtt: cmd/zynqrx/%s \"%s\" ignored (%s)\n", key.c_str(), val.c_str(), freq ? "kHz, 70000 - 6000000" : "kS, 33 - 7680");
                return;
            }
            std::lock_guard<std::mutex> lk(ctl.mu);
            long& cur = freq ? ctl.freqKHz : ctl.srKS;
            if (cur == v) return;
            cur = v;
            if (!ctl.enabled) { fprintf(stderr, "mqtt: %s = %ld (receiver stopped, used at the next start)\n", freq ? "frequency (kHz)" : "symbol rate (kS)", v); return; }
            ctl.restart = true;
            fprintf(stderr, "mqtt: %s = %ld, restarting the receiver\n", freq ? "frequency (kHz)" : "symbol rate (kS)", v);
            ctl.stop = true;
            if (ctl.cancelRead) ctl.cancelRead();
        } else if (key == "process") {
            const bool on = val == "start";
            if (!on && val != "stop") { fprintf(stderr, "mqtt: cmd/zynqrx/process \"%s\" ignored (start or stop)\n", val.c_str()); return; }
            std::lock_guard<std::mutex> lk(ctl.mu);
            if (ctl.enabled != on) {
                ctl.enabled = on;
                fprintf(stderr, "mqtt: receiver %s\n", on ? "start" : "stop");
                if (!on) { ctl.stop = true; if (ctl.cancelRead) ctl.cancelRead(); }   // the running session ends; the loop then idles
            }
            if (ctl.mq) ctl.mq->publish("dt/zynqrx/process", on ? "start" : "stop");   // answer at once, the status loop repeats it every second
        } else if (key == "tsip") {
            in_addr a;
            if (inet_pton(AF_INET, val.c_str(), &a) != 1) { fprintf(stderr, "mqtt: cmd/zynqrx/tsip \"%s\" ignored (an IPv4 address)\n", val.c_str()); return; }
            { std::lock_guard<std::mutex> lk(ctl.mu); ctl.tsip = val; }
            std::lock_guard<std::mutex> lk(ctl.udpMu);
            if (ctl.udp.open(val + ":" + std::to_string(ctl.udpPort), ctl.udpTtl, ctl.udpIf)) fprintf(stderr, "mqtt: transport stream to %s:%d\n", val.c_str(), ctl.udpPort);
        } else if (key == "swport") {
            std::lock_guard<std::mutex> lk(ctl.mu);
            ctl.swport = atoi(val.c_str()) ? 1 : 0;           // no input switch on this hardware: only reported back
        } else if (key == "polar") {
            std::lock_guard<std::mutex> lk(ctl.mu);           // no LNB supply on this hardware: only reported back
            ctl.lnbSupply = val == "h" || val == "v";
            ctl.lnbHorizontal = val == "h";
        }
    };
    mq.start(o.mqttHost, o.mqttPort, onCommand);
    fprintf(stderr, "mqtt: broker %s:%d%s%s\n", o.mqttHost.c_str(), o.mqttPort,
            ctl.freqKHz && ctl.srKS ? "" : ", waiting for cmd/zynqrx/frequency and cmd/zynqrx/sr",
            o.start ? "" : "; receiver stopped (no --start), send cmd/zynqrx/process start");
    auto idleStatus = [&] {
        DatvStatus st;
        std::lock_guard<std::mutex> lk(ctl.mu);
        st.freqKHz = ctl.freqKHz; st.srKS = ctl.srKS; st.swport = ctl.swport; st.tsip = ctl.tsip; st.lnbSupply = ctl.lnbSupply; st.lnbHorizontal = ctl.lnbHorizontal;
        st.running = ctl.enabled;
        datvPublish(mq, st);
    };
    while (!gStop) {
        long fk, sk;
        bool on;
        {   // take the settings and clear the flags in one step: a command that comes after this sets them again
            std::lock_guard<std::mutex> lk(ctl.mu);
            fk = ctl.freqKHz; sk = ctl.srKS; on = ctl.enabled;
            ctl.restart = false; ctl.stop = false;
        }
        if (!on || !fk || !sk) { idleStatus(); std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        RxOpts so = o;
        datvSessionOpts(so, fk, sk);
        fprintf(stderr, "receiver: %.3f MHz, %ld kS (%s)\n", (double)fk / 1000.0, sk, so.prefilt ? "radio RRC filter" : "software matched filter");
        const int rc = runSession(so, ctl);
        if (gStop) break;
        bool again, stopped;
        { std::lock_guard<std::mutex> lk(ctl.mu); again = ctl.restart; stopped = !ctl.enabled; }
        if (stopped) { fprintf(stderr, "receiver stopped\n"); continue; }   // cmd/zynqrx/process stop: idle until the next start
        if (again) continue;
        // the session ended by itself (the radio refused the settings, or the stream stopped): report Init and try again in a few seconds, or at the next command
        fprintf(stderr, "receiver stopped (%d), trying again in 5 s\n", rc);
        for (int i = 0; i < 5 && !gStop; i++) {
            idleStatus();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lk(ctl.mu);
            if (ctl.restart || !ctl.enabled) break;
        }
    }
    return 0;
}

int cmdRx(int argc, char** argv) {
    RxOpts o;
    bool presenceGiven = false;
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--iio") { o.useIio = true; if (i + 1 < argc && argv[i + 1][0] != '-') o.iioUri = argv[++i]; }
        else if (a == "--udp") o.udpDest = next();
        else if (a == "--ws") o.wsPort = atoi(next().c_str());
        else if (a == "--ws-keyframe") o.wsKeyframe = true;
        else if (a == "--start") o.start = true;
        else if (a == "--ttl") o.udpTtl = atoi(next().c_str());
        else if (a == "--udp-if") o.udpIf = next();
        else if (a == "--mqtt") {
            const std::string v = next();
            const size_t c = v.rfind(':');
            o.mqttHost = c == std::string::npos ? v : v.substr(0, c);
            if (c != std::string::npos) o.mqttPort = atoi(v.c_str() + c + 1);
        }
        else if (a == "--max-lag") o.maxLag = atof(next().c_str());
        else if (a == "--nice") o.niceValue = atoi(next().c_str());
        else if (a == "--freq") o.freqMHz = atof(next().c_str());
        else if (a == "--bw") o.bwMHz = atof(next().c_str());
        else if (a == "--gain") o.gainDb = atof(next().c_str());
        else if (a == "--rate") { o.rate = atof(next().c_str()); o.rateGiven = true; }
        else if (a == "--format") o.format = next();
        else if (a == "--rs") o.rs = atof(next().c_str());
        else if (a == "--std") o.stdHint = atoi(next().c_str());
        else if (a == "--roll") o.roll = atof(next().c_str());
        else if (a == "--isi") o.isi = atoi(next().c_str());
        else if (a == "--pls") o.pls = atoi(next().c_str());
        else if (a == "--prefiltered") o.prefilt = true;
        else if (a == "--ldpc8") o.ldpc8 = true;
        else if (a == "--fast-timing") { const std::string v = next(); o.fastTiming = v == "on" ? 1 : v == "off" ? 0 : 2; }
        else if (a == "--ccm") o.ccm = true;
        else if (a == "--presence") { o.presence = true; presenceGiven = true; }
        else if (a == "--no-presence") { o.presence = false; presenceGiven = true; }
        else if (a == "--mods") {
            const std::string v = next();
            o.modsMask = 0;
            size_t pos = 0;
            while (pos <= v.size()) {
                const size_t c = v.find(',', pos);
                const std::string m = v.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
                if (m == "qpsk") o.modsMask |= 1; else if (m == "8psk") o.modsMask |= 2; else if (m == "16apsk") o.modsMask |= 4; else if (m == "32apsk") o.modsMask |= 8;
                else if (!m.empty()) { fprintf(stderr, "--mods: %s is not qpsk, 8psk, 16apsk or 32apsk\n", m.c_str()); return 1; }
                if (c == std::string::npos) break;
                pos = c + 1;
            }
        }
        else if (a == "--spec-secs") o.specSecs = atof(next().c_str());
        else if (a == "--center") { o.center = atof(next().c_str()); o.haveCenter = true; }
        else if (a == "--ts") o.tsOut = next();
        else if (a == "--secs") o.secsMax = atof(next().c_str());
        else if (o.path.empty() && a[0] != '-') o.path = a;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
    }
    // Priority: the receiver has deadlines, the firmware's own state publishing and web services do not (they use about 0.4 of the board's two cores even when
    // idle, and raising this priority took 15% off the time a recording needs). Live radio input defaults to -10; needs root, silently left alone otherwise. It is
    // done here, before any thread exists: on Linux the value belongs to the thread, and new threads inherit it.
    {
        const int nv = o.niceValue != 99 ? o.niceValue : (o.useIio || !o.mqttHost.empty()) ? -10 : 0;
        if (nv != 0 && setpriority(PRIO_PROCESS, 0, nv) != 0 && o.niceValue != 99) fprintf(stderr, "--nice %d: not allowed (run as root)\n", nv);
    }
    if (!o.mqttHost.empty()) {
        // longmynd's way: --freq and --rs are optional here (the receiver waits for MQTT commands without them), the radio's RRC filter is chosen by the symbol rate
        if (!o.path.empty()) { fprintf(stderr, "--mqtt works with the radio (--iio), not a recording\n"); return 1; }
        o.useIio = true;
        if (!presenceGiven) o.presence = true;   // the signals under MQTT control come and go: the gate is the point
        signal(SIGINT, onSigint); signal(SIGTERM, onSigint);
        return runDaemon(o);
    }
    if (o.useIio && (o.freqMHz <= 0 || !o.path.empty())) { fprintf(stderr, "--iio needs --freq (MHz) and takes no recording\n"); return 1; }
    if (o.useIio && o.prefilt) {   // the radio's FIR does the matched filtering: 2 samples per symbol
        if (o.rs <= 0) { fprintf(stderr, "--iio --prefiltered needs --rs\n"); return 1; }
        if (!o.rateGiven) o.rate = 2 * o.rs;
        if (std::fabs(o.rate - 2 * o.rs) > 1e-3 * o.rate) { fprintf(stderr, "--prefiltered needs --rate = 2 * --rs\n"); return 1; }
        if (o.roll <= 0) o.roll = 0.35;
        o.haveCenter = true;   // below 2 Msps the receiver runs without its spectrum search: the carrier is taken to be on the LO unless --center says otherwise
    }
    if (o.path.empty() && !o.useIio) { fprintf(stderr, "usage: zynqrx rx recording [--rate 10] [--format cs8|cu8|cf32] ... (see the top of tools/zynqrx.cpp)\n"); return 1; }
    if (o.format.empty() && !o.useIio) {
        const size_t d = o.path.rfind('.');
        const std::string ext = d == std::string::npos ? "" : o.path.substr(d + 1);
        o.format = ext == "cf32" ? "cf32" : ext == "cu8" ? "cu8" : "cs8";
    }
    if (o.useIio) { signal(SIGINT, onSigint); signal(SIGTERM, onSigint); }
    RxCtl ctl;
    ctl.udpTtl = o.udpTtl;
    if (!o.udpDest.empty() && !ctl.udp.open(o.udpDest, o.udpTtl, o.udpIf)) return 1;
    if (o.wsPort > 0) { ctl.ws.waitForKeyframe(o.wsKeyframe); if (!ctl.ws.open(o.wsPort)) return 1; }
    return runSession(o, ctl);
}

int cmdRanges() {
    printf("symbol rates the receiver takes (about 1.3 samples per symbol times (1 + roll-off) of bandwidth; the automatic search needs about 40 spectrum bins across the carrier):\n");
    printf("  %8s  %22s  %22s\n", "Msps", "roll-off 0.35: min auto .. max", "roll-off 0.05: min auto .. max");
    for (double fs : {2.0, 4.0, 8.0, 10.0, 12.5, 16.0, 20.0})
        printf("  %8.1f  %9.3f .. %-8.3f Msym/s  %9.3f .. %-8.3f Msym/s\n", fs, dvbsMinAutoSymbolRate(fs * 1e6) / 1e6, dvbsMaxSymbolRate(fs * 1e6, 0.35) / 1e6, dvbsMinAutoSymbolRate(fs * 1e6) / 1e6, dvbsMaxSymbolRate(fs * 1e6, 0.05) / 1e6);
    printf("a manual symbol rate (--rs) works down to about 0.03 Msym/s at any sample rate\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);   // the status lines reach a log file (or a pipe) as they happen
    if (const char* cm = getenv("DECT2_COMPUTE")) gCompute = !strcmp(cm, "cpu") ? 0 : !strcmp(cm, "gpu") ? 1 : 2;
    if (getenv("DECT2_DVBS_REF")) dect2::dvbs::dvbsReferencePath(true);   // the receiver as it was before the speed-up (to compare)
    if (argc >= 2 && !strcmp(argv[1], "gen")) return cmdGen(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "rx")) return cmdRx(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "bench")) return cmdBench(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "ranges")) return cmdRanges();
    fprintf(stderr, "usage: zynqrx gen out.cs8 [options] | zynqrx rx recording --rate Msps [options] | zynqrx bench [gen options] | zynqrx ranges (see the top of tools/zynqrx.cpp)\n");
    return 1;
}
