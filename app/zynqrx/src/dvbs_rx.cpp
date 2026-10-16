// DVB-S/S2 receiver, see dvbs_rx.h.
#include "dect2/dvbs_rx.h"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include "dect2/dvbs_s2.h"
#include "dvbs_presence.h"
#include "dvbs_carrier.h"
#include "dvbs_front.h"
#include "dvbs_s1.h"
#include "dvbs_compute.h"
#include "dvbs_s2hunt.h"
#include "dvbs_s2rx.h"
#include "dvbs_spec.h"
#include "dvbs_symthread.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace dect2 {
using namespace dvbs;

double dvbsMaxSymbolRate(double fs, double rollOff) { return fs / (1.3 * (1.0 + rollOff)); }
double dvbsMinAutoSymbolRate(double fs) { return std::max(60e3, fs * 40.0 / 4096.0 * 2.5); }   // about 40 spectrum bins across the carrier

ModeTuning dvbsTuning() {
    ModeTuning t;
    t.stdMode = 8; t.id = "dvbs"; t.name = "DVB-S/S2";
    // The L-band IF of an LNB is 950 to 2150 MHz (739 MHz for the QO-100 narrow band transponder), but radio amateurs also send DVB-S/S2
    // directly on 50, 70, 144, 437 MHz and 1.2 / 2.4 GHz, often narrow band (issue #30): the radio's own range is the only limit.
    t.minMhz = 10; t.maxMhz = 6000; t.defMhz = 1500.0;
    t.sampleRate = 10000000.0; t.basebandHz = 8000000.0; t.bandwidthMhz = 8;
    t.minSampleRate = 2000000.0;
    return t;
}

namespace {
using clk = std::chrono::steady_clock;
constexpr size_t kHuntWindow = 49152;       // (DVB-S)        // symbols a DVB-S attempt looks at: 16384 for the carrier loop to settle, 32768 to find the code
constexpr size_t kHuntKeep = 294912;        // symbols kept for the frame search of DVB-S2: up to eight long frames, the more the weaker a signal it finds
constexpr size_t kS2Window = 262144;
constexpr size_t kCellsMax = 2048;

double snapRollOff(double a) {
    static const double v[6] = {0.35, 0.25, 0.20, 0.15, 0.10, 0.05};
    double best = v[0];
    for (double x : v) if (std::fabs(x - a) < std::fabs(best - a)) best = x;
    return best;
}
// The symbol rate from the spectral line that the squared envelope of a linearly modulated carrier has at the symbol rate (its cyclostationarity).
// The spectrum's -3 dB width (the first estimate, rs0) is bent by an echo or a tilted cable response, which ripples the spectrum: the line is not.
// x: kCyclicN samples at fs; centre: where the carrier is. Looks between 0.6 and 1.25 times rs0 (clipping and
// spectral regrowth widen the -3 dB width far more than they narrow it); returns 0 when no clear line stands out (small
// roll-offs give a weak one).
constexpr int kCyclicN = 1 << 18;
double cyclicRate(const std::vector<cf32>& x, double fs, double centre, double rs0) {
    if (x.size() < (size_t)kCyclicN) return 0;
    std::vector<cf32> v(x.end() - kCyclicN, x.end());
    Fft fft(kCyclicN);
    fft.forward(v.data());
    // keep the carrier only (up to 1.25 rs0 with a 0.35 roll-off), so that the noise beside it does not bury the line
    const double half = 0.5 * 1.25 * rs0 * 1.35;
    for (int k = 0; k < kCyclicN; k++) {
        // distance to the centre around the circle: a carrier near the band edge continues on the other side
        double df = std::fmod(std::fabs(k * fs / kCyclicN - centre), fs);
        df = std::min(df, fs - df);
        if (df > half) v[(size_t)k] = cf32(0, 0);
    }
    fft.inverse(v.data());
    double mean = 0;
    for (const cf32& z : v) mean += std::norm(z);
    mean /= kCyclicN;
    for (cf32& z : v) z = cf32((float)(std::norm(z) - mean), 0.f);
    fft.forward(v.data());
    // A rate above fs / 2 shows as its alias fs - rate: the squared envelope is real, so bin N - k holds the same as bin k, and the bins up to N
    // stand for the rates up to fs. A rate and its alias look the same; of the two the one nearer rs0 is taken.
    const int k0 = std::max(2, (int)std::floor(0.6 * rs0 * kCyclicN / fs)), k1 = std::min(kCyclicN - 2, (int)std::ceil(1.25 * rs0 * kCyclicN / fs));
    if (k1 <= k0 + 8) return 0;
    const double kr = rs0 * kCyclicN / fs;
    std::vector<float> mag((size_t)(k1 - k0 + 1));
    int best = k0;
    for (int k = k0; k <= k1; k++) {
        mag[(size_t)(k - k0)] = std::abs(v[(size_t)k]);
        const float m = mag[(size_t)(k - k0)], mb = mag[(size_t)(best - k0)];
        if (m > mb * 1.0001f || (m >= mb * 0.9999f && std::fabs(k - kr) < std::fabs(best - kr))) best = k;
    }
    std::vector<float> sorted = mag;
    std::nth_element(sorted.begin(), sorted.begin() + (std::ptrdiff_t)(sorted.size() / 2), sorted.end());
    const float median = sorted[sorted.size() / 2];
    if (!(mag[(size_t)(best - k0)] > 12.f * median) || best <= k0 || best >= k1) return 0;
    // parabolic interpolation of the peak
    const double a = mag[(size_t)(best - k0 - 1)], b = mag[(size_t)(best - k0)], c = mag[(size_t)(best - k0 + 1)];
    const double d = 0.5 * (a - c) / std::min(-1e-30, a - 2 * b + c);
    return (best + std::max(-0.5, std::min(0.5, d))) * fs / kCyclicN;
}

} // namespace

struct DvbsReceiver::Impl {
    enum Mode { kSearch = 0, kHunt = 1, kS1 = 2, kS2 = 3 };

    // ---- settings from other threads
    std::atomic<double> manualRs{0}, rollHint{0};
    std::atomic<int> stdHint{0}, isiSel{-1}, plCode{-1}, computeMode{2};
    ComputeChoice s1Compute{&ComputeChoice::viterbiUsable, false};                  // DVB-S: where the Viterbi decoder runs
    std::atomic<bool> s1OnGpu{false};
    std::atomic<bool> blocking{false}, prefiltered{false}, centerKnown{false};
    std::atomic<double> manualCenter{0};
    bool spectrumOff() const { return centerKnown.load() && manualRs.load() > 0; }   // carrier given: nothing to measure
    std::atomic<double> specInterval{0.25};   // seconds between spectrum analyses (display, carrier telemetry) once locked
    uint64_t specAt = 0;
    std::atomic<bool> resetReq{false}, restartReq{false};
    std::mutex cbMu;
    std::function<void(const std::string&)> logCb;
    std::function<void(const uint8_t*, size_t, double)> pktCb;

    // ---- state of the receiver thread
    double fs = 0;
    Mode mode = kSearch;
    std::atomic<uint64_t> inputCount{0};
    uint64_t reportAt = 0;
    SpectrumEstimator spec;
    SpectrumResult specRes;
    std::vector<float> psdDb;
    int searchTries = 0;
    std::vector<cf32> raw;                      // the latest input while searching, for cyclicRate()
    std::vector<cf32> clean;                    // the input with non-finite samples replaced
    DvbsFront front;
    std::vector<cf32> sym, y;
    uint64_t symTotal = 0;
    bool timingNarrow = false, recentred = false;
    double rsUsed = 0, rollUsed = 0.35;
    int rollSource = 0;
    // hunting
    std::vector<cf32> huntBuf;
    uint64_t huntSinceTry = 0;
    double rsNominal = 0;                       // the rate the spectrum (or the user) gave; hunts that fail try rates a little either side of it
    int huntFails = 0, ditherIdx = 0, huntCycles = 0;     // failed attempts at one rate, the rate in the dither list, complete lists tried
    // DVB-S: the carrier loop and the decoder run on a thread of their own, fed with the symbols of the front end
    QpskPll pll;
    S1Decoder s1;
    SymbolThread s1th;
    std::vector<cf32> s1y;
    std::vector<uint8_t> s1pk;
    uint64_t s1Symbols = 0, s1Mark = 0;      // symbols the thread has processed, and at the last packet delivery
    std::atomic<bool> s1Lost{false};
    std::atomic<double> rateNow{0};          // symbol rate of the front end, kept for the threads
    struct S1Snap {
        bool started = false, synced = false, inverted = false;
        int rate = 0, consecutiveBad = 0;
        float lock = 0, snrDb = 0;
        double freq = 0, byteErrors = 0;
        uint64_t rsClean = 0, rsCorrected = 0, rsFailed = 0;
    } s1Snap;
    std::mutex s1Mu, cellMu;
    uint64_t s2Symbols = 0;
    std::atomic<bool> tsLockFlag{false};
    bool huntCodes[256] = {};          // the PLS codes the hunt looks for when huntRestrict is set (setHuntModulations)
    std::atomic<bool> huntRestrict{false};
    uint64_t huntPenalty = 0;          // symbols to wait on top of the interval after a hunt that found nothing: a hunt that took long is not repeated at once
    // The presence gate (setPresenceGate): with the input prefiltered at two samples per symbol, the search for a signal is only made while there seems to be one
    std::atomic<bool> gate{false};
    CyclicPresence presence;
    uint64_t gateHold = 0, nextLook = 0, lookUntil = 0;
    bool gateRunning = true;
    std::atomic<bool> ccm{false};      // setConstantModcod(): hunts after the first lock look for that MODCOD only
    std::atomic<int> ccmCode{-1};      // the PLS code of the last verified lock
    int lastHuntCode = -1;
    uint64_t sinceFullHunt = 0;        // symbols since the last hunt that looked for every MODCOD
    std::atomic<int> senseKnown{-1};   // -1 not known; 0 / 1: the spectrum is normal / inverted, as the last verified lock found it (it cannot change)
    // DVB-S2
    S2Rx s2;
    // packets and counters
    std::vector<uint8_t> pk;
    std::atomic<uint64_t> packets{0};
    std::atomic<uint64_t> packetsBad{0};
    std::atomic<uint64_t> lockSinceInput{0};
    std::vector<cf32> cells;
    size_t cellPos = 0;
    std::vector<float> merHist;
    uint64_t rateMark = 0, ratePackets = 0;
    double netBitrate = 0;
    // telemetry
    std::mutex tmu;
    DvbsTelemetry tel;
    uint64_t seq = 0;
    uint64_t seqBase = 0;

    void log(const std::string& s) {
        std::function<void(const std::string&)> cb;
        { std::lock_guard<std::mutex> lk(cbMu); cb = logCb; }
        if (cb) cb(s);
    }
    void logf(const char* f, double a = 0, double b = 0, const char* c = "", const char* d = "") {
        char buf[200];
        snprintf(buf, sizeof buf, f, a, b, c, d);
        log(buf);
    }

    void resetState() {
        mode = kSearch;
        inputCount = 0; reportAt = 0;
        spec.reset();
        specRes = SpectrumResult();
        searchTries = 0;
        specAt = 0;
        sym.clear(); y.clear();
        symTotal = 0; timingNarrow = false; recentred = false;
        huntBuf.clear(); huntSinceTry = 0;
        s1th.stop();
        s1.reset(); s1Symbols = 0; s1Mark = 0; s1Lost = false;
        s2.stop();
        packets = 0; packetsBad = 0;
        lockSinceInput = 0; tsLockFlag = false; senseKnown = -1; ccmCode = -1; sinceFullHunt = 0;
        { std::lock_guard<std::mutex> lk(cellMu); cells.clear(); cellPos = 0; }
        merHist.clear();
        rateMark = 0; ratePackets = 0; netBitrate = 0;
        psdDb.clear();
    }

    // ------------------------------------------------------------------------ search: spectrum -> front end
    void doSearch() {
        if (spec.segments() < 48) return;
        specRes = spec.analyse();
        spec.display(psdDb, 512);
        const double manual = manualRs.load();
        const bool carrier = specRes.valid && specRes.zscore >= 8.f && specRes.fitRms < 2.5f;
        double rs = manual > 0 ? manual : specRes.rateHz;
        if (manual <= 0 && carrier) {
            const double rc = cyclicRate(raw, fs, specRes.centerHz, rs);
            if (rc > 0) {
                if (std::fabs(rc / rs - 1) > 0.01) logf("symbol rate %.3f Msym/s from the spectrum's width, %.4f from its cyclic line", rs / 1e6, rc / 1e6);
                rs = rc;
            }
        }
        if (!carrier || rs < dvbsMinAutoSymbolRate(fs) * (manual > 0 ? 0.0 : 1.0) || rs > dvbsMaxSymbolRate(fs, 0.35) * 1.02) {
            spec.reset();
            searchTries++;
            return;
        }
        startFront(specRes, rs);
    }

    void startFront(const SpectrumResult& r, double rs) {
        double roll = 0.35;
        rollSource = 0;
        if (rollHint.load() > 0) { roll = rollHint.load(); rollSource = 1; }
        else if (!prefiltered.load() && r.rollOff > 0 && r.snrDb > 15.0f) { roll = snapRollOff(r.rollOff); rollSource = 1; }
        DvbsFront::Config c;
        c.fs = fs; c.symbolRate = rs; c.centerHz = r.centerHz; c.rollOff = roll; c.prefiltered = prefiltered.load();
        front.configure(c);
        front.timer().setLoop(0.02, 0.707);
        rsUsed = rs; rollUsed = roll;
        symTotal = 0; timingNarrow = false; recentred = false;
        huntBuf.clear(); huntSinceTry = 0;
        rsNominal = rs; huntFails = 0; ditherIdx = 0; huntCycles = 0;
        mode = kHunt;
        logf("carrier found: %.3f Msym/s, %+.0f kHz from the centre", rs / 1e6, r.centerHz / 1e3);
    }

    // ------------------------------------------------------------------------ the symbol path
    void runFront(const cf32* x, size_t n) {
        sym.clear();
        front.process(x, n, sym);
        if (sym.empty()) return;
        symTotal += sym.size();
        rateNow = front.symbolRateNow();
        if (!timingNarrow && symTotal > 40000) { front.timer().setLoop(0.004, 0.707); timingNarrow = true; }
        if (!recentred && symTotal > 200000) { front.recentre(); recentred = true; }
        switch (mode) {
        case kHunt: huntPush(); break;
        case kS1: s1Push(); break;
        case kS2: s2Push(); break;
        default: break;
        }
    }

    void addCells(const cf32* z, size_t n) {
        std::lock_guard<std::mutex> lk(cellMu);
        if (cells.size() < kCellsMax) cells.resize(kCellsMax, cf32());
        for (size_t i = 0; i < n; i += 4) { cells[cellPos] = z[i]; cellPos = (cellPos + 1) % kCellsMax; }   // every fourth symbol keeps the display current
    }

    void huntPush() {
        huntBuf.insert(huntBuf.end(), sym.begin(), sym.end());
        if (huntBuf.size() > 2 * kHuntKeep) huntBuf.erase(huntBuf.begin(), huntBuf.end() - (std::ptrdiff_t)kHuntKeep);
        huntSinceTry += sym.size();
        sinceFullHunt += sym.size();
        // a signal that is not there is looked for less often: the interval doubles after each complete round of rates, up to four times as long
        const uint64_t interval = std::max<uint64_t>(65536, (uint64_t)(0.15 * rsUsed)) * (uint64_t)std::min(4, 1 << std::min(huntCycles, 2));
        if (huntBuf.size() >= kHuntWindow && huntSinceTry >= interval + huntPenalty && symTotal > 20000) {
            huntSinceTry = 0;
            if (tryDvbs2() || tryDvbs()) { huntFails = 0; return; }
            // The spectrum gives the symbol rate to a percent or two at best, and in noise the timing loop cannot pull in much more than that:
            // after a few attempts the rate is moved a little and the front end starts again
            if (++huntFails >= 3 && huntBuf.size() >= 150000) {
                static const double d[6] = {0.012, -0.012, 0.024, -0.024, 0.036, -0.036};
                const double f = ditherIdx < 6 ? d[ditherIdx] : 0.0;
                ditherIdx = (ditherIdx + 1) % 7;
                if (ditherIdx == 0) huntCycles++;
                huntFails = 0;
                DvbsFront::Config c = front.config();
                c.symbolRate = rsNominal * (1.0 + f);
                front.configure(c);
                front.timer().setLoop(0.02, 0.707);
                rsUsed = c.symbolRate;
                symTotal = 0; timingNarrow = false; recentred = false;
                huntBuf.clear(); huntSinceTry = 0;
            }
        }
    }

    bool tryDvbs2() {
        if (stdHint.load() == 1 || huntBuf.size() < 100000) return false;
        size_t n = std::min(huntBuf.size(), kS2Window);
        // With the wanted modulations given, four of their longest frames are enough (the search uses the sum of up to four headers one frame apart): the
        // window is the one thing the cost of a search is proportional to
        if (huntRestrict.load()) {
            int lmax = 0;
            for (int c = 0; c < 256; c++) if (huntCodes[c]) lmax = std::max(lmax, s2PlsFrameSymbols(c));
            if (lmax > 0) n = std::min(n, (size_t)(4 * lmax + 2000));
        }
        const cf32* w = huntBuf.data() + huntBuf.size() - n;
        // a constant MODCOD: after a verified lock the hunt looks for that code word and frame length only. A try without a lock to remember is a full
        // one, and so is one after every 1.5 s of signal, so that a transmitter that changed its MODCOD is found as well
        const bool full = !(ccm.load() && ccmCode.load() >= 0) || (double)sinceFullHunt >= 1.5 * rsUsed;
        if (full) sinceFullHunt = 0;
        const int only = full ? -1 : ccmCode.load();
        const auto th0 = std::chrono::steady_clock::now();
        const S2HuntResult h = s2Hunt(w, n, senseKnown.load(), only, huntRestrict.load() ? huntCodes : nullptr);
        if (!h.ok) {
            // The search costs the same whether or not there is a signal (a Cortex-A9 needs a good part of a second for it). With none there, repeating it as soon as
            // the interval allows would keep the processor busy for nothing: wait for as many symbols as twice its duration, at most three seconds' worth
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - th0).count();
            // (only after a full round of attempts at the dithered rates has found nothing: the first attempts of a weak signal come before the timing loop has
            // settled and each one counts)
            huntPenalty = huntCycles >= 1 ? (uint64_t)std::min(3.0 * rsUsed, 3.0 * secs * rsUsed) : 0;
            return false;
        }
        huntPenalty = 0;
        if (h.modcod != 0) lastHuntCode = s2PlsValue(h.modcod, h.shortFrame, h.pilots);
        s2.setSymbolRate(front.symbolRateNow());
        s2.setPlCode(plCode.load());
        s2.setIsi(isiSel.load());
        s2.setBlocking(blocking.load());
        s2.setPacketCallback([this](const uint8_t* p, size_t np, double secs) {
            std::function<void(const uint8_t*, size_t, double)> cb;
            { std::lock_guard<std::mutex> lk(cbMu); cb = pktCb; }
            packets.fetch_add(np);
            if (!tsLockFlag.exchange(true)) { lockSinceInput = inputCount.load(); senseKnown = s2.inverted() ? 1 : 0; if (lastHuntCode >= 0) ccmCode = lastHuntCode; log("transport stream lock"); }
            if (cb) cb(p, np, secs);
        });
        s2.setLogCallback([this](const std::string& m) { log(m); });
        s2.start(h, w, n);
        front.timer().setLoop(0.001, 0.707);       // the frames carry the timing now: a narrow loop wanders less
        s2Symbols = 0;
        mode = kS2;
        int mod, rate;
        if (h.modcod != 0 && s2ModcodSplit(h.modcod, mod, rate))
            log(std::string(s2ModcodIsS2x(h.modcod) ? "DVB-S2X, " : "DVB-S2, ") + s2ModNameFor(mod, rate) + " " + s2RateName(rate) + (h.shortFrame ? " short" : "") +
                (h.pilots ? " pilots" : "") + (h.inverted ? ", spectrum inverted" : ""));
        else log("DVB-S2, dummy frames");
        return true;
    }

    void s2Push() {
        s2Symbols += sym.size();
        s2.push(sym.data(), sym.size());
        if (s2.lost()) {
            s2.stop();
            tsLockFlag = false;
            mode = kHunt; huntBuf.clear(); huntSinceTry = 0;
        }
    }

    bool tryDvbs() {
        if (stdHint.load() == 2) return false;
        const cf32* w = huntBuf.data() + huntBuf.size() - kHuntWindow;
        const QpskEstimate e = qpskEstimate(w, 16384);
        if (!e.ok) return false;
        std::vector<cf32> yy(kHuntWindow);
        QpskPll p;
        p.start(e.freq, e.phase, 0.02);
        p.process(w, 16384, yy.data());
        p.setBandwidth(0.005);
        p.process(w + 16384, kHuntWindow - 16384, yy.data() + 16384);
        const S1Hypothesis h = s1Search(yy.data() + 16384, kHuntWindow - 16384);
        if (!h.ok) return false;
        pll = p;
        s1.setCompute(&s1Compute);
        s1Compute.autoAllowed = front.symbolRateNow() >= 8e6;      // 8 Msym/s and up: a window of a million steps is under a tenth of a second
        s1.start(h, kHuntWindow - 16384);
        s1Symbols = 0; s1Mark = 0; s1Lost = false;
        snapS1();
        // blocking (a file): a short queue, so the front end cannot run seconds ahead of the decoder; the symbols queued when the decoder
        // loses sync are thrown away, and how many that were depended on the thread scheduling
        s1th.start([this](const cf32* z, size_t m) { s1Work(z, m); }, blocking.load() ? 1u << 16 : 1u << 22, blocking.load());
        mode = kS1;
        static const char* rn[5] = {"1/2", "2/3", "3/4", "5/6", "7/8"};
        log(std::string("DVB-S, QPSK ") + rn[h.rate] + ((h.variant & 2) ? ", spectrum inverted" : ""));
        return true;
    }

    // runs on the DVB-S thread
    void s1Work(const cf32* z, size_t n) {
        if (s1Lost.load()) return;           // sync lost: the receiver thread stops this one and hunts again
        s1y.resize(n);
        pll.process(z, n, s1y.data());
        addCells(s1y.data(), n);
        s1Symbols += n;
        if (s1.started()) {
            s1.push(s1y.data(), n);
            s1OnGpu = s1.lastOnGpu();
            s1pk.clear();
            s1.takePackets(s1pk);
            deliver();
        }
        if (!s1.started()) {                 // the decoder lost its hypothesis: look again
            log("DVB-S: sync lost");
            tsLockFlag = false;
            s1Lost = true;
        } else if (!s1.synced() && s1Symbols > 600000) {
            log("DVB-S: no packet sync, searching again");
            s1.reset();
            s1Lost = true;
        }
        snapS1();
    }

    void snapS1() {
        std::lock_guard<std::mutex> lk(s1Mu);
        s1Snap.started = s1.started(); s1Snap.synced = s1.synced(); s1Snap.inverted = (s1.hypothesis().variant & 2) != 0;
        s1Snap.rate = s1.rate(); s1Snap.consecutiveBad = s1.consecutiveBad();
        s1Snap.lock = pll.lock(); s1Snap.snrDb = pll.snrDb(); s1Snap.freq = pll.freq();
        s1Snap.byteErrors = s1.byteErrorRate();
        s1Snap.rsClean = s1.rsClean; s1Snap.rsCorrected = s1.rsCorrected; s1Snap.rsFailed = s1.rsFailed;
    }

    void s1Push() {
        s1th.push(sym.data(), sym.size());
        if (s1Lost.load()) {
            s1th.stop();
            s1Lost = false;
            mode = kHunt; huntBuf.clear(); huntSinceTry = 0;
        }
    }

    void deliver() {
        if (s1pk.empty()) return;
        const size_t np = s1pk.size() / 188;
        uint64_t bad = 0;
        for (size_t i = 0; i < np; i++) if (s1pk[i * 188 + 1] & 0x80) bad++;
        packets += np; packetsBad += bad;
        const double secs = (double)(s1Symbols - s1Mark) / std::max(1.0, rateNow.load());
        s1Mark = s1Symbols;
        std::function<void(const uint8_t*, size_t, double)> cb;
        { std::lock_guard<std::mutex> lk(cbMu); cb = pktCb; }
        if (cb) cb(s1pk.data(), np, secs);
        if (np > bad && !tsLockFlag.exchange(true)) { lockSinceInput = inputCount.load(); log("transport stream lock"); }
    }

    // ------------------------------------------------------------------------ telemetry
    void publish() {
        DvbsTelemetry t;
        t.computeMode = computeMode.load(); t.gpuAvailable = ComputeChoice::gpuUsable() || ComputeChoice::viterbiUsable();
        const double nowSec = (double)inputCount / fs;
        t.standard = mode == kS1 ? 1 : mode == kS2 ? 2 : 0;
        t.symbolRate = mode == kSearch ? (specRes.valid ? specRes.rateHz : 0) : front.symbolRateNow();
        t.symbolRateManual = manualRs.load() > 0;
        t.symbolRateSpectrum = specRes.valid ? specRes.rateHz : 0;
        t.symbolRateLoop = mode == kSearch ? 0 : front.symbolRateNow();
        t.spectrumSnrDb = specRes.valid ? specRes.snrDb : 0;
        t.spectrumFitRms = specRes.fitRms;
        t.rollOff = (float)rollUsed;
        t.rollOffSource = rollSource;
        t.lockSpectrum = mode != kSearch;
        t.lockTiming = mode != kSearch && timingNarrow && front.timer().errorRms() > 0 && front.timer().errorRms() < 0.6f;
        t.psdDb = psdDb;
        t.psdSpanHz = fs;
        t.carrierLoHz = specRes.valid ? specRes.edgeLoHz : 0;
        t.carrierHiHz = specRes.valid ? specRes.edgeHiHz : 0;
        t.cfoHz = specRes.valid ? specRes.centerHz : 0;
        if (mode == kS1) {
            S1Snap q;
            { std::lock_guard<std::mutex> lk(s1Mu); q = s1Snap; }
            t.modulation = 0; t.modulationName = "QPSK";
            static const char* rn[5] = {"1/2", "2/3", "3/4", "5/6", "7/8"};
            t.codeRate = rn[q.rate];
            t.inverted = q.inverted;
            t.lockCarrier = q.lock > 0.4f;
            t.snrDb = q.snrDb; t.merDb = q.snrDb;
            t.cfoHz = front.config().centerHz + q.freq * front.symbolRateNow();
            t.lockFrame = q.synced;
            t.rsClean = q.rsClean; t.rsCorrected = q.rsCorrected; t.rsFailed = q.rsFailed;
            t.onGpu = s1OnGpu;
            t.blocksOk = q.rsClean + q.rsCorrected; t.blocksBad = q.rsFailed;
            t.lockFec = q.synced && q.consecutiveBad < 8;
            // bit error rate after the Viterbi decoder, from the bytes Reed-Solomon had to change (about 1.5 bit errors in a byte it changes); too low when blocks fail
            t.preFecBer = q.rsClean + q.rsCorrected > 100 ? (float)std::min(0.5, q.byteErrors * 1.5 / 8.0) : -1.f;
        } else if (mode == kS2) {
            const S2Stats s = s2.stats();
            if (s.mod >= 0) { t.modulation = s.mod; t.modulationName = s2ModNameFor(s.mod, s.rate); t.codeRate = s2RateName(s.rate); }
            // an S2X MODCOD makes it DVB-S2X; its number is shown as the PLS code value of EN 302 307-2 table 17a
            if (s2IsS2x(s.rate) || s2ModcodIsS2x(s.modcod) || s.framesUnsupported > 0) t.standard = 3;
            t.modcod = s2ModcodIsS2x(s.modcod) ? s.modcod << 1 : s.modcod;
            t.frameSize = s.shortFrame ? 2 : 1; t.pilots = s.pilots; t.vcm = s.vcm; t.isi = s.isi;
            t.plScramblingCode = plCode.load() < 0 ? 0 : plCode.load();
            t.inverted = s2.inverted();
            t.merDb = (float)s.merDb; t.snrDb = (float)s.merDb;
            t.lockCarrier = s2.carrierLocked();
            t.cfoHz = front.config().centerHz + s.carrierRadPerSym / (2 * 3.14159265358979) * front.symbolRateNow();
            t.lockFrame = s2.tracking() && s.consecutiveHeaderBad == 0 && s.framesSeen > 0;
            t.lockFec = s.consecutiveBad < 3 && s.fecOk > 0;
            t.blocksOk = s.fecOk; t.blocksBad = s.fecBad;
            t.bchOk = s.fecOk; t.bchBad = s.bchBad;
            t.ldpcIterAvg = (float)s.ldpcIterAvg;
            t.matype1 = s.matype1; t.matype2 = s.matype2;
            t.onGpu = s.ldpcOnGpu;
            t.preFecBer = (float)s.berPre;
            t.framesSeen = s.framesSeen; t.framesDummy = s.framesDummy; t.crcErrors = s.crcErrors; t.gseFrames = s.gseFrames;
            t.framesUnsupported = s.framesUnsupported;
            if (s.roSignalled >= 0 && s.roSignalled < 3) { t.rollOff = s.roSignalled == 0 ? 0.35f : s.roSignalled == 1 ? 0.25f : 0.20f; t.rollOffSource = 2; }
            if (s.gseFrames > 0 && s.tsGs != 3) t.signalNote = "generic stream (GSE) seen: not converted to a transport stream";
            if (s.framesUnsupported > 0) {
                const int c = s.unsupportedCode;
                t.signalNote = c == 129 || c == 131 ? std::string("S2X VL-SNR frames seen (set ") + (c == 129 ? "1" : "2") + "): not decoded"
                                                     : "S2X frames with the reserved PLS code " + std::to_string(c) + " seen: not decoded";
            }
            { std::lock_guard<std::mutex> lk(cellMu); s2.cells(cells); }
        } else if (mode == kHunt) {
            t.snrDb = specRes.valid ? specRes.snrDb : 0;
        } else {
            t.snrDb = 0;
        }
        { const S2Stats s = mode == kS2 ? s2.stats() : S2Stats(); t.packetsBad = mode == kS2 ? s.packetsBad : packetsBad.load(); }
        t.packets = packets.load();
        t.tsLock = tsLockFlag.load() && t.lockFec;
        t.dataValid = t.tsLock;
        t.secsSinceLock = t.tsLock ? nowSec - (double)lockSinceInput / fs : 0;
        t.netBitrate = netBitrate;
        t.state = t.dataValid ? 2 : (mode == kSearch ? 0 : 1);
        {
            std::lock_guard<std::mutex> lk(cellMu);
            if (!cells.empty()) {
                const size_t n = std::min(cells.size(), kCellsMax);
                t.cells.assign(cells.end() - (std::ptrdiff_t)n, cells.end());
            }
        }
        // the transport stream bit rate over the last second or so
        if (inputCount.load() - rateMark >= (uint64_t)fs) {
            const double dt = (double)(inputCount.load() - rateMark) / fs;
            const uint64_t pk = packets.load();
            netBitrate = (double)(pk - ratePackets) * 188.0 * 8.0 / dt;
            rateMark = inputCount.load(); ratePackets = pk;
        }
        merHist.push_back(t.merDb);
        if (merHist.size() > 120) merHist.erase(merHist.begin());
        t.merHistory = merHist;
        {
            std::lock_guard<std::mutex> lk(tmu);
            seq++;
            t.seq = seq;
            tel = std::move(t);
        }
    }
};

// ============================================================================ the class
DvbsReceiver::DvbsReceiver() : p_(std::make_unique<Impl>()) {}
DvbsReceiver::~DvbsReceiver() {
    // the threads use the members of Impl: stop them while those are all alive
    p_->s1th.stop();
    p_->s2.stop();
}

void DvbsReceiver::configure(double inputRateHz) {
    p_->fs = inputRateHz;
    p_->spec.configure(inputRateHz, 4096);
    p_->spec.setStride((size_t)std::max(4096.0, inputRateHz * 0.004));
    p_->resetState();
}
// The 2 Msps floor is for the spectrum search; with the carrier given (or the radio's own filtering) a low rate works too
bool DvbsReceiver::ready() const { return p_->fs >= (p_->prefiltered.load() && p_->spectrumOff() ? 1e5 : dvbsTuning().minSampleRate) - 1; }
void DvbsReceiver::reset() { p_->resetReq = true; }
void DvbsReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->cbMu); p_->logCb = std::move(cb); }
void DvbsReceiver::setPacketCallback(std::function<void(const uint8_t*, size_t, double)> cb) { std::lock_guard<std::mutex> lk(p_->cbMu); p_->pktCb = std::move(cb); }
void DvbsReceiver::setSymbolRate(double hz) { if (hz != p_->manualRs.load()) { p_->manualRs = hz; p_->restartReq = true; } }
void DvbsReceiver::setStandardHint(int s) { if (s != p_->stdHint.load()) { p_->stdHint = s; p_->restartReq = true; } }
void DvbsReceiver::setRollOff(double r) { if (r != p_->rollHint.load()) { p_->rollHint = r; p_->restartReq = true; } }
void DvbsReceiver::setPrefiltered(bool on) { if (on != p_->prefiltered.load()) { p_->prefiltered = on; p_->restartReq = true; } }
void DvbsReceiver::setSpectrumInterval(double secs) { p_->specInterval = secs; }
void DvbsReceiver::setCenter(bool known, double hz) { if (known != p_->centerKnown.load() || hz != p_->manualCenter.load()) { p_->manualCenter = hz; p_->centerKnown = known; p_->restartReq = true; } }
void DvbsReceiver::setIsi(int isi) { p_->isiSel = isi; }
void DvbsReceiver::setPlScrambling(int n) { p_->plCode = n; }
void DvbsReceiver::setConstantModcod(bool on) { p_->ccm = on; }
void DvbsReceiver::setPresenceGate(bool on) { p_->gate = on; p_->presence.reset(); }
void DvbsReceiver::setHuntModulations(int mask) {
    if (mask <= 0 || mask >= 15) { p_->huntRestrict = false; return; }   // none given, or all four DVB-S2 modulations
    for (int c = 0; c < 256; c++) {
        p_->huntCodes[c] = false;
        if (c >= 128 || s2PlsKind(c) < 0) continue;       // not the S2X codes
        int mc; bool sh, pil;
        s2PlsSplit(c, mc, sh, pil);
        int md = 0, rt = 0;
        p_->huntCodes[c] = mc == 0 || (s2ModcodSplit(mc, md, rt) && md >= 0 && md < 4 && ((mask >> md) & 1));   // 0: the dummy frame
    }
    p_->huntRestrict = true;
}
void DvbsReceiver::setLdpc8(bool on) { p_->s2.setLdpc8(on); }
void DvbsReceiver::setNoBch(bool on) { p_->s2.setNoBch(on); }
void DvbsReceiver::setFastTiming(bool on) { p_->front.timer().setFast(on); }
void DvbsReceiver::setComputeMode(int m) { p_->computeMode = m; p_->s2.setComputeMode(m); p_->s1Compute.mode = m; }
void DvbsReceiver::setBlocking(bool b) { p_->blocking = b; }
void DvbsReceiver::flush() {
    Impl& I = *p_;
    if (I.mode == Impl::kS1) {
        I.s1th.post([&I] { if (I.s1.started()) { I.s1.flushPending(); I.s1pk.clear(); I.s1.takePackets(I.s1pk); I.deliver(); } });
        I.s1th.sync();
    }
    else if (I.mode == Impl::kS2) I.s2.drain();
    I.publish();
}

bool DvbsReceiver::telemetry(DvbsTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->tmu);
    if (p_->seq <= lastSeq) return false;
    out = p_->tel;
    return true;
}

// True when no sample is NaN or infinite. Four floats at a time with the largest absolute value kept (a NaN stays in a NEON maximum), and a prefetch
// ahead: the block was written by another thread, so it comes from the L2 cache, where a plain loop spends about 30 cycles a sample waiting
static bool allFinite(const cf32* x, size_t n) {
    const float* p = reinterpret_cast<const float*>(x);
    const size_t m = 2 * n;
    size_t i = 0;
#if defined(__ARM_NEON)
    float32x4_t mx = vdupq_n_f32(0.f);
    for (; i + 8 <= m; i += 8) {
        __builtin_prefetch(p + i + 64);
        mx = vmaxq_f32(mx, vabsq_f32(vld1q_f32(p + i)));
        mx = vmaxq_f32(mx, vabsq_f32(vld1q_f32(p + i + 4)));
    }
    const float32x2_t h = vpmax_f32(vget_low_f32(mx), vget_high_f32(mx));
    if (!(std::max(vget_lane_f32(h, 0), vget_lane_f32(h, 1)) <= 3.0e38f)) return false;     // a NaN fails this comparison too
#endif
    for (; i < m; i++) if (!std::isfinite(p[i])) return false;
    return true;
}

void DvbsReceiver::feed(const cf32* x, size_t n) {
    Impl& I = *p_;
    if (n == 0 || !ready()) return;
    if (I.resetReq.exchange(false) || I.restartReq.exchange(false)) {
        const uint64_t keep = I.seq;
        I.resetState();
        I.seq = keep;
    }
    // a NaN or infinite sample (a broken file) would stay in the AGC power of the symbol timer, whose loop then runs away and reads outside its
    // buffer: such samples count as silence
    if (!allFinite(x, n)) {
        I.clean.assign(x, x + n);
        for (cf32& v : I.clean) if (!std::isfinite(v.real() + v.imag())) v = cf32(0, 0);
        x = I.clean.data();
    }
    I.inputCount += n;
    const bool noSpectrum = I.spectrumOff();
    if (noSpectrum && I.mode == Impl::kSearch) {   // centre and symbol rate given: start at once, without the spectrum
        SpectrumResult r;
        r.valid = true; r.centerHz = I.manualCenter.load();
        I.specRes = r;
        I.startFront(r, I.manualRs.load());
        std::vector<cf32>().swap(I.raw);
    }
    // locked: the spectrum only serves the display and telemetry, so its FFTs run just before an analysis is due
    else if (!noSpectrum && (I.mode == Impl::kSearch || I.inputCount + (uint64_t)(0.08 * I.fs) >= I.specAt)) I.spec.push(x, n);
    if (I.mode == Impl::kSearch) {
        I.raw.insert(I.raw.end(), x, x + n);
        if (I.raw.size() > (size_t)kCyclicN) I.raw.erase(I.raw.begin(), I.raw.end() - kCyclicN);
        I.doSearch();
        if (I.mode != Impl::kSearch) std::vector<cf32>().swap(I.raw);
    }
    else if (I.gate.load() && I.prefiltered.load()) {
        I.presence.update(x, n);
        bool run = true;
        if (I.mode == Impl::kHunt) {
            // Hunting for a signal: the timing recovery and the search cost much more than the test above. While it sees none, nothing else is done, except a look of
            // 0.6 s every 4 s (a signal too weak for the test is found by that). A signal seen is followed for at least a second.
            const uint64_t now = I.inputCount;
            if (I.presence.present()) I.gateHold = now + (uint64_t)(1.0 * I.fs);
            if (now >= I.nextLook) { I.lookUntil = now + (uint64_t)(0.6 * I.fs); I.nextLook = now + (uint64_t)(4.0 * I.fs); }
            run = now < I.gateHold || now < I.lookUntil;
            if (run && !I.gateRunning) {
                // the front end stood still: start it again from the nominal rate, as a fresh search does
                DvbsFront::Config c = I.front.config();
                c.symbolRate = I.rsNominal;
                I.front.configure(c);
                I.front.timer().setLoop(0.02, 0.707);
                I.rsUsed = c.symbolRate;
                I.symTotal = 0; I.timingNarrow = false; I.recentred = false;
                I.huntBuf.clear(); I.huntSinceTry = 0; I.huntFails = 0; I.ditherIdx = 0; I.huntCycles = 0; I.huntPenalty = 0;
            }
            I.gateRunning = run;
        } else I.gateRunning = true;
        if (run) I.runFront(x, n);
    }
    else I.runFront(x, n);
    if (I.inputCount >= I.reportAt) {
        I.reportAt = I.inputCount + (uint64_t)(0.25 * I.fs);
        if (!noSpectrum && I.mode != Impl::kSearch && I.inputCount >= I.specAt && I.spec.segments() >= 8) {
            I.specRes = I.spec.analyse();
            I.spec.display(I.psdDb, 512);
            I.spec.reset();
            I.specAt = I.inputCount + (uint64_t)(std::max(0.0, I.specInterval.load() - 0.125) * I.fs);
        }
        I.publish();
    }
}

} // namespace dect2
