// Live IQ from the radio's AD936x through libiio (zynqrx rx --iio). Built only with -DZYNQRX_IIO (Makefile: IIO=1, the default for the board).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <complex>
#include <cmath>
#include <string>
#include <vector>
#include <sys/types.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifdef ZYNQRX_IIO
#include <iio.h>

// The AD9361 FIR for DVB-S/S2: root raised cosine that decimates by 4. The converter runs at 4 times the output rate, which is 2 samples per symbol
// (8 per symbol at the FIR's input): the stream the receiver takes with --prefiltered. Taps in 1/32768 with unity gain at DC.
inline std::string rrcFirConfig(int taps, double roll) {
    const double pi = 3.14159265358979323846, a = roll, sps = 8.0;
    std::vector<double> h((size_t)taps);
    double sum = 0;
    for (int i = 0; i < taps; i++) {
        const double x = (i - (taps - 1) / 2.0) / sps;   // time in symbols, centred between the two middle taps
        double v;
        if (std::fabs(x) < 1e-9) v = 1 - a + 4 * a / pi;
        else if (std::fabs(std::fabs(x) - 1 / (4 * a)) < 1e-9) v = a / std::sqrt(2.0) * ((1 + 2 / pi) * std::sin(pi / (4 * a)) + (1 - 2 / pi) * std::cos(pi / (4 * a)));
        else v = (std::sin(pi * x * (1 - a)) + 4 * a * x * std::cos(pi * x * (1 + a))) / (pi * x * (1 - std::pow(4 * a * x, 2)));
        h[(size_t)i] = v;
        sum += v;
    }
    std::string c = "RX 3 GAIN 0 DEC 4\nTX 3 GAIN 0 INT 4\n";
    for (double v : h) { const long long t = std::llround(v / sum * 32768.0); c += std::to_string(t) + "," + std::to_string(t) + "\n"; }
    return c;
}

class IioSource {
public:
    IioSource() = default;
    IioSource(const IioSource&) = delete;
    IioSource& operator=(const IioSource&) = delete;
    ~IioSource() {
        if (buf_) iio_buffer_destroy(buf_);
        if (firOn_ && rxCfg_) {   // the radio goes back to a rate it can run without the FIR
            setRate(std::llround(4 * rate_));
            setFirEn(rxCfg_, 0);
        }
        if (ctx_) iio_context_destroy(ctx_);
    }

    // uri empty = the local context (on the board); otherwise e.g. ip:192.168.2.1 or usb:1.2.5. gainDb < 0 = the radio's slow attack AGC.
    // rrcRoll > 0: the RRC decimate-by-4 FIR with that roll-off; rateHz is then the output rate (2 samples per symbol, 0.52 - 15.36 Msps).
    bool open(const std::string& uri, double rateHz, double freqHz, double bwHz, double gainDb, double rrcRoll = 0) {
        ctx_ = uri.empty() ? iio_create_default_context() : iio_create_context_from_uri(uri.c_str());
        if (!ctx_) { fprintf(stderr, "iio: cannot open context %s\n", uri.empty() ? "(default)" : uri.c_str()); return false; }
        iio_device* phy = iio_context_find_device(ctx_, "ad9361-phy");
        dev_ = iio_context_find_device(ctx_, "cf-ad9361-lpc");
        if (!phy || !dev_) { fprintf(stderr, "iio: ad9361-phy / cf-ad9361-lpc not found\n"); return false; }
        iio_channel* rxCfg = iio_device_find_channel(phy, "voltage0", false);   // RX path settings
        iio_channel* lo = iio_device_find_channel(phy, "altvoltage0", true);    // RX LO
        if (!rxCfg || !lo) { fprintf(stderr, "iio: RX channels of ad9361-phy not found\n"); return false; }
        rxCfg_ = rxCfg;
        txCfg_ = iio_device_find_channel(phy, "voltage0", true);   // the TX path has its own sampling_frequency; the FIR config covers both paths
        rate_ = rateHz;
        if (rrcRoll > 0) {
            if (rateHz < 2.083e6 / 4 || rateHz > 61.44e6 / 4) { fprintf(stderr, "iio: the RRC filter needs 0.52 - 15.36 Msps\n"); return false; }
            setFirEn(rxCfg, 0);
            bool loaded = false;
            for (int taps : {96, 64, 48, 32, 16}) {
                long long r = setRate(std::llround(4 * rateHz));
                if (r < 0) { fprintf(stderr, "iio: converter rate %.3f Msps refused (%lld)\n", 4 * rateHz / 1e6, r); break; }
                const std::string cfg = rrcFirConfig(taps, rrcRoll);
                r = iio_device_attr_write(phy, "filter_fir_config", cfg.c_str());
                if (r < 0) { fprintf(stderr, "iio: %d tap filter not loaded (%lld)\n", taps, r); continue; }
                r = setFirEn(rxCfg, 1);
                if (r < 0) { fprintf(stderr, "iio: %d tap filter not enabled (%lld)\n", taps, r); continue; }
                firOn_ = true;
                // the converter rounds its rate (2.664 Msps -> 2663997 Hz) and the output rate must be exactly a quarter of what it really runs at
                long long conv = 0, outRate = std::llround(rateHz);
                if (iio_channel_attr_read_longlong(rxCfg, "sampling_frequency", &conv) == 0 && conv > 0) outRate = conv / 4;
                r = setRate(outRate);
                for (long long d = 1; r < 0 && d <= 2; d++) r = setRate(outRate + (d & 1 ? d : -d + 1));
                if (r < 0) { fprintf(stderr, "iio: output rate %lld Hz refused (%lld), converter %lld Hz, %d taps\n", outRate, r, conv, taps); setFirEn(rxCfg, 0); firOn_ = false; continue; }
                fprintf(stderr, "iio: RRC %.2f decimate by 4 FIR, %d taps\n", rrcRoll, taps);
                loaded = true;
                break;
            }
            if (!loaded) { fprintf(stderr, "iio: the radio did not accept the RRC FIR at %.3f Msps\n", rateHz / 1e6); return false; }
        }
        if ((!firOn_ && iio_channel_attr_write_longlong(rxCfg, "sampling_frequency", (long long)rateHz) < 0) ||
            iio_channel_attr_write_longlong(rxCfg, "rf_bandwidth", (long long)(bwHz > 0 ? bwHz : rateHz)) < 0 ||
            iio_channel_attr_write_longlong(lo, "frequency", (long long)freqHz) < 0) {
            fprintf(stderr, "iio: the radio refused %.3f Msps / %.3f MHz\n", rateHz / 1e6, freqHz / 1e6);
            return false;
        }
        if (gainDb >= 0) {
            iio_channel_attr_write(rxCfg, "gain_control_mode", "manual");
            iio_channel_attr_write_double(rxCfg, "hardwaregain", gainDb);
        } else
            iio_channel_attr_write(rxCfg, "gain_control_mode", "slow_attack");
        i_ = iio_device_find_channel(dev_, "voltage0", false);
        q_ = iio_device_find_channel(dev_, "voltage1", false);
        if (!i_ || !q_) { fprintf(stderr, "iio: RX data channels not found\n"); return false; }
        iio_channel_enable(i_);
        iio_channel_enable(q_);
        iio_device_set_kernel_buffers_count(dev_, 8);   // a few extra buffers ride out the receiver being busy
        buf_ = iio_device_create_buffer(dev_, kBlock, false);
        if (!buf_) { fprintf(stderr, "iio: cannot create the RX buffer\n"); return false; }
        return true;
    }

    // Blocks until the next buffer is in; returns the number of samples written to x (at most max), 0 on error. Samples are 12 bit in int16 (full scale 2048).
    size_t read(std::complex<float>* x, size_t max) {
        const ssize_t r = iio_buffer_refill(buf_);
        if (r < 0) { if (r != -9) fprintf(stderr, "iio: buffer refill failed (%d)\n", (int)r); return 0; }   // -9: cancel() woke it
        const char* p = (const char*)iio_buffer_first(buf_, i_);
        const char* end = (const char*)iio_buffer_end(buf_);
        const ptrdiff_t step = iio_buffer_step(buf_);
        size_t n = 0;
#if defined(__ARM_NEON)
        if (step == 4) {   // I and Q only: 12-bit samples in int16, four samples (eight values) at a time
            float* o = reinterpret_cast<float*>(x);
            const float32x4_t scale = vdupq_n_f32(1.f / 2048.f);
            for (; p + 16 <= end && n + 4 <= max; p += 16, n += 4) {
                const int16x8_t v = vld1q_s16(reinterpret_cast<const int16_t*>(p));
                vst1q_f32(o + 2 * n, vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(v))), scale));
                vst1q_f32(o + 2 * n + 4, vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(v))), scale));
            }
        }
#endif
        for (; p < end && n < max; p += step, n++) {
            const int16_t* s = (const int16_t*)p;
            x[n] = std::complex<float>(s[0] * (1.f / 2048.f), s[1] * (1.f / 2048.f));
        }
        return n;
    }

    // Wakes a read() that waits for the next buffer (from another thread); that read() then returns 0
    void cancel() { if (buf_) iio_buffer_cancel(buf_); }
    // the gain the radio runs at now (dB), -1 when it cannot be read
    int gainDb() const {
        double g = -1;
        return rxCfg_ && iio_channel_attr_read_double(rxCfg_, "hardwaregain", &g) == 0 ? (int)std::lround(g) : -1;
    }

    static constexpr size_t kBlock = 1 << 16;

private:
    iio_context* ctx_ = nullptr;
    iio_device* dev_ = nullptr;
    iio_channel *i_ = nullptr, *q_ = nullptr;
    iio_buffer* buf_ = nullptr;
    iio_channel* rxCfg_ = nullptr;
    iio_channel* txCfg_ = nullptr;
    // the sampling frequency of the RX path and of the TX path (what iio_attr -c voltage0 does: both)
    // filter_fir_en is a separate switch for the RX and the TX path on this kernel (in_voltage_ / out_voltage_ filter_fir_en); the FIR config covers both
    long long setFirEn(iio_channel* rx, long long on) {
        const long long r = iio_channel_attr_write_longlong(rx, "filter_fir_en", on);
        if (txCfg_) iio_channel_attr_write_longlong(txCfg_, "filter_fir_en", on);
        return r;
    }
    long long setRate(long long hz) {
        const long long r = iio_channel_attr_write_longlong(rxCfg_, "sampling_frequency", hz);
        if (txCfg_) iio_channel_attr_write_longlong(txCfg_, "sampling_frequency", hz);
        return r;
    }
    bool firOn_ = false;
    double rate_ = 0;
};
#else
class IioSource {
public:
    bool open(const std::string&, double, double, double, double, double = 0) { fprintf(stderr, "this build has no libiio (build with IIO=1)\n"); return false; }
    size_t read(std::complex<float>*, size_t) { return 0; }
    void cancel() {}
    int gainDb() const { return -1; }
    static constexpr size_t kBlock = 1 << 16;
};
#endif
