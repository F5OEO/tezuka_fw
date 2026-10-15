// DVB-S/S2 test signal, see dvbs_gen.h.
#include "dect2/dvbs_gen.h"
#include "dect2/demo_ts.h"
#include "dvbs_simd.h"
#include "dect2/dsp_compat.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {
namespace dvbs {

namespace {
constexpr double kPi = 3.14159265358979323846;

// white Gaussian noise, one table shared by all generators (unit variance per complex sample); each generator reads it from pseudo random positions
const std::vector<cf32>& noiseTable() {
    static const std::vector<cf32> t = [] {
        std::vector<cf32> v(1 << 20);
        std::mt19937 rng(20240607);
        std::normal_distribution<float> nd(0.f, 0.70710678f);
        for (auto& x : v) x = cf32(nd(rng), nd(rng));
        return v;
    }();
    return t;
}
} // namespace

PhaseNoise::PhaseNoise(int mask, double rate, uint32_t seed) {
    // mask points: frequency in Hz and level in dBc/Hz, log-linear in between, flat outside (EN 302 307-1 tables H.1 and M.2, from the pdf text)
    static const double fDth[7] = {100, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8};
    static const double typ[7] = {-25, -50, -73, -93, -103, -114, -114};
    static const double crit[7] = {-25, -50, -73, -85, -103, -114, -114};
    static const double fNon[8] = {10, 100, 1e3, 1e4, 1e5, 1e6, 1e7, 5e7};
    static const double non[8] = {-33, -62, -79, -89, -99, -109, -119, -120};
    const double* f = mask == 3 ? fNon : fDth;
    const double* lv = mask == 3 ? non : mask == 2 ? crit : typ;
    const int np = mask == 3 ? 8 : 7;
    const int lg = 20, M = 1 << lg;
    std::vector<float> re(M, 0.f), im(M, 0.f);
    std::mt19937 rng(seed * 2654435761u + 17u);
    std::normal_distribution<float> nd(0.f, 1.f);
    const double df = rate / M;
    double var = 0;
    for (int k = 1; k < M / 2; k++) {
        const double fk = k * df;
        double db = lv[0];
        if (fk >= f[np - 1]) db = lv[np - 1];
        else if (fk > f[0]) {
            int i = 0;
            while (i < np - 2 && fk > f[i + 1]) i++;
            db = lv[i] + (lv[i + 1] - lv[i]) * std::log10(fk / f[i]) / std::log10(f[i + 1] / f[i]);
        }
        const double S = 2.0 * std::pow(10.0, db / 10.0);        // one sided power spectral density of the phase in rad^2/Hz
        const double a = std::sqrt(S * df);
        var += S * df;
        re[k] = (float)(a * nd(rng)); im[k] = (float)(a * nd(rng));
    }
    fftSplit(re.data(), im.data(), lg, true);
    th_.assign(re.begin(), re.end());
    // the real part of the inverse transform of a one sided spectrum has the variance of the sum of S df: check, so the mask is met whatever the FFT does
    double m = 0, v = 0;
    for (float x : th_) m += x;
    m /= M;
    for (float& x : th_) { x -= (float)m; v += (double)x * x; }
    v /= M;
    const double g = v > 0 ? std::sqrt(var / v) : 1.0;
    for (float& x : th_) x *= (float)g;
    rms_ = std::sqrt(var);
}

double rrcAt(double t, double a) {
    const double eps = 1e-9;
    if (std::fabs(t) < eps) return 1 - a + 4 * a / kPi;
    if (a > 0 && std::fabs(std::fabs(4 * a * t) - 1) < 1e-7) {
        return a / std::sqrt(2.0) * ((1 + 2 / kPi) * std::sin(kPi / (4 * a)) + (1 - 2 / kPi) * std::cos(kPi / (4 * a)));
    }
    const double num = std::sin(kPi * t * (1 - a)) + 4 * a * t * std::cos(kPi * t * (1 + a));
    const double den = kPi * t * (1 - (4 * a * t) * (4 * a * t));
    return num / den;
}

DvbsSignal::DvbsSignal(const DvbsSignalConfig& c) : cfg_(c) {
    DvbsTxConfig tx = c.tx;
    tx.rollOff = c.tx.rollOff;
    std::function<void(uint8_t*)> ts = c.ts ? c.ts : demoTsSource(dvbsNetBitrate(tx));
    tx_ = makeDvbsTx(tx, ts);
    half_ = c.shaperHalfSpan;
    phases_ = 1024;
    taps_.assign((size_t)phases_ * 2 * half_, 0.f);
    for (int ph = 0; ph < phases_; ph++)
        for (int i = 0; i < 2 * half_; i++) taps_[(size_t)ph * 2 * half_ + i] = (float)rrcAt((double)ph / phases_ + (half_ - 1 - i), tx.rollOff);
    const double rs = tx.symbolRate * (1.0 + c.clockPpm * 1e-6);
    dt_ = rs / c.sampleRate;
    t_ = 0;
    step_ = std::polar(1.0, 2 * kPi * c.cfoHz / c.sampleRate);
    // The shaped signal has unit power for unit energy symbols; the noise variance per sample for Es/N0 follows from the sample rate
    // over the symbol rate. The final scale puts the rms at cfg.level.
    const double esn0 = c.snrDb >= 150 ? 1e15 : std::pow(10.0, c.snrDb / 10.0);
    const double nvar = (c.sampleRate / tx.symbolRate) / esn0;
    noiseAmp_ = (float)std::sqrt(nvar);
    scale_ = (float)(c.level / std::sqrt(1.0 + nvar));
    noiseState_ = 88172645463325252ull ^ ((uint64_t)c.seed * 0x9E3779B97F4A7C15ull);
    if (c.phaseNoise > 0) pn_ = std::make_unique<PhaseNoise>(c.phaseNoise, c.sampleRate, c.seed);
    re_.assign(8192 + 2 * half_, 0.f);
    im_.assign(8192 + 2 * half_, 0.f);
    base_ = -(long long)half_;       // symbols before the start are zero
    have_ = half_;
}

void DvbsSignal::generate(cf32* out, size_t n) {
    const auto& nt = noiseTable();
    const size_t mask = nt.size() - 1;
    const int H = half_;
    for (size_t j = 0; j < n; j++) {
        const long long k0 = (long long)std::floor(t_);
        const double fr = t_ - (double)k0;
        int ph = (int)(fr * phases_ + 0.5);
        long long kk = k0;
        if (ph >= phases_) { ph = 0; kk++; }
        // symbols kk-H+1 ... kk+H must be in the history
        for (;;) {
            if ((long long)have_ >= kk + H + 1 - base_) break;
            if (have_ + 256 > re_.size()) {      // slide the window: drop what is no longer needed
                const long long first = kk - H + 1 - base_;
                const size_t d = (size_t)std::max<long long>(0, first);
                if (d) {
                    memmove(re_.data(), re_.data() + d, (have_ - d) * sizeof(float));
                    memmove(im_.data(), im_.data() + d, (have_ - d) * sizeof(float));
                    have_ -= d; base_ += (long long)d;
                } else { re_.resize(re_.size() * 2); im_.resize(im_.size() * 2); }
            }
            sc_.resize(256);
            tx_->generate(sc_.data(), 256);
            for (int i = 0; i < 256; i++) { re_[have_ + i] = sc_[i].real(); im_[have_ + i] = sc_[i].imag(); }
            have_ += 256;
        }
        const float* g = &taps_[(size_t)ph * 2 * H];
        const long long first = kk - H + 1 - base_;
        const float* xr = &re_[(size_t)first];
        const float* xi = &im_[(size_t)first];
        float sv[2];
        dot2(g, xr, xi, 2 * H, sv);
        cf32 v(sv[0], sv[1]);
        // noise from the shared table: a new pseudo random start every 4096 samples
        if ((noiseCount_++ & 4095) == 0) {
            noiseState_ ^= noiseState_ << 13; noiseState_ ^= noiseState_ >> 7; noiseState_ ^= noiseState_ << 17;
            noisePos_ = (size_t)(noiseState_ & mask);
        }
        if (noiseAmp_ > 0) v += nt[noisePos_ & mask] * noiseAmp_;
        noisePos_++;
        if (cfg_.inverted) v = std::conj(v);
        out[j] = v;
        t_ += dt_;
    }
    // level, carrier offset, then the receiver faults
    const bool iq = cfg_.iqGainDb != 0 || cfg_.iqPhaseDeg != 0;
    const double gI = std::pow(10.0, cfg_.iqGainDb / 40.0), gQ = 1.0 / gI, pr = cfg_.iqPhaseDeg * kPi / 180.0 / 2;
    const float cp = (float)std::cos(pr), sp = (float)std::sin(pr);
    const bool cfo = cfg_.cfoHz != 0, dc = cfg_.dcOffset != 0;
    const float dcv = (float)(cfg_.dcOffset * cfg_.level), sc = scale_;
    // the carrier offset oscillator runs in double precision, written out (the library complex product tests for NaN results every time)
    double rr = rot_.real(), ri = rot_.imag();
    const double sr = step_.real(), si = step_.imag();
    for (size_t j = 0; j < n; j++) {
        float vr = out[j].real() * sc, vi = out[j].imag() * sc;
        if (pn_) { const cf32 w = std::polar(1.0f, (float)pn_->next()); const float t = vr * w.real() - vi * w.imag(); vi = vr * w.imag() + vi * w.real(); vr = t; }
        if (cfo) {
            const float cr = (float)rr, ci = (float)ri;
            const float t = vr * cr - vi * ci; vi = vr * ci + vi * cr; vr = t;
            const double nr = rr * sr - ri * si; ri = rr * si + ri * sr; rr = nr;
            if ((++rotCount_ & 1023) == 0) { const double inv = 1.0 / std::sqrt(rr * rr + ri * ri); rr *= inv; ri *= inv; }
        }
        if (iq) {
            const float i0 = vr, q0 = vi;
            vr = (float)gI * (i0 * cp - q0 * sp); vi = (float)gQ * (q0 * cp - i0 * sp);
        }
        if (dc) { vr += dcv; vi += dcv; }
        out[j] = cf32(vr, vi);
    }
    rot_ = std::complex<double>(rr, ri);
}

DvbsSignalConfig dvbsConfigFromSynth(const SynthConfig& sc, double fs) {
    DvbsSignalConfig c;
    DvbsTxConfig& t = c.tx;
    t.standard = sc.modeOpt[0] == 1 ? 1 : sc.modeOpt[0] == 2 ? 3 : 2;
    t.mod = t.standard == 1 ? kQpsk : std::max(0, std::min(t.standard == 3 ? kS2Mods - 1 : 3, sc.modeOpt[1]));
    t.shortFrame = sc.modeOpt[4] != 0;
    // modeOpt[2]: 0 is the default code rate (2/3; S2X: the first MODCOD of the modulation), otherwise the rate index plus one (S2X: the position
    // in table 17a plus one)
    if (t.standard == 1) t.rate = sc.modeOpt[2] ? std::max(0, std::min(4, sc.modeOpt[2] - 1)) : 1;
    else if (t.standard == 2) t.rate = sc.modeOpt[2] ? std::max(0, std::min(10, sc.modeOpt[2] - 1)) : 5;
    else {
        if (s2xRateCount(t.mod, t.shortFrame) == 0) t.shortFrame = false;    // 64APSK and up have normal frames only
        const int n = s2xRateCount(t.mod, t.shortFrame);
        t.rate = s2xRate(t.mod, t.shortFrame, std::max(0, std::min(n - 1, sc.modeOpt[2] - 1)));
    }
    static const double ro[6] = {0.35, 0.25, 0.20, 0.15, 0.10, 0.05};
    t.rollOff = ro[std::max(0, std::min(5, sc.modeOpt[3]))];
    t.pilots = sc.modeOpt[5] != 0;
    t.vcm = sc.modeOpt[7] == 1 && t.standard == 2;
    c.phaseNoise = sc.modeOpt[7] == 2 ? 1 : sc.modeOpt[7] == 3 ? 2 : 0;
    c.inverted = sc.modeOpt[6] != 0;
    // symbol rate: what was asked for, or 5 Msym/s, limited by what the sample rate can carry (1.3 samples per symbol and the roll-off)
    const double maxRs = fs / (1.3 * (1 + t.rollOff));
    t.symbolRate = sc.modeVal[0] > 0 ? sc.modeVal[0] : std::min(5e6, maxRs);
    c.sampleRate = fs;
    c.snrDb = sc.snrDb;
    c.cfoHz = sc.cfoHz;
    c.clockPpm = sc.modeVal[1] + sc.sroPpm;
    return c;
}

} // namespace dvbs

namespace {
class DvbsSynth : public ModeSynth {
public:
    DvbsSynth(const dvbs::DvbsSignalConfig& c) : sig_(c) {}
    double sampleRate() const override { return sig_.sampleRate(); }
    void generate(cf32* out, size_t n) override { sig_.generate(out, n); }
private:
    dvbs::DvbsSignal sig_;
};
} // namespace

std::unique_ptr<ModeSynth> makeDvbsSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate < 2e6 || sampleRate > 40e6) return nullptr;
    dvbs::DvbsSignalConfig c = dvbs::dvbsConfigFromSynth(cfg, sampleRate);
    if (c.tx.standard >= 2 && !dvbs::s2Dims(c.tx.mod, c.tx.rate, c.tx.shortFrame).ok) return nullptr;
    return std::make_unique<DvbsSynth>(c);
}

} // namespace dect2
