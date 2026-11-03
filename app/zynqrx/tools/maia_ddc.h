// The Maia DDC in the FPGA (maia-httpd REST API on the board, port 80): decimates the RX stream before the DMA, so that zynqrx can leave the
// AD9361 at the clock pluto_stream set for the transmission (RX and TX share it) and still get a few samples per symbol.
// PUT /api/ddc/design designs the decimating FIR; PATCH /api/ddc/config {"enabled":true} routes the DDC output into cf-ad9361-lpc
// (bit 0 of AXI register 0xBC, the same as firdecim.sh). Same settings as Android-DATV (PlutoDdc.kt).
#pragma once
#include <arpa/inet.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

class MaiaDdc {
public:
    static constexpr double kMinOutputHz = 100e3;   // the receiver's floor (DvbsReceiver::ready)
    static constexpr int kMaxDecimation = 1000;

    // D = floor(fin / (sps x rs)), 4 samples per symbol when the stream stays at or below 3 MS/s, else 2; 0 when D < 2 (the DDC is no use:
    // the receiver takes the converter's stream as it is)
    static int decimation(double finHz, double rsHz) {
        auto at = [&](double sps) {
            long d = (long)std::floor(finHz / (sps * rsHz));
            d = std::min<long>(d, kMaxDecimation);
            d = std::min<long>(d, (long)std::floor(finHz / kMinOutputHz));
            return d >= 2 ? (int)d : 0;
        };
        const int d4 = at(4.0);
        if (d4 && finHz / d4 <= 3e6) return d4;
        return at(2.0);
    }

    // designs the filter for decimation d; false when maia-httpd refused it or reports another decimation
    bool design(int d) {
        char body[256];
        snprintf(body, sizeof body, "{\"frequency\":0.0,\"decimation\":%d,\"transition_bandwidth\":0.1,\"passband_ripple\":0.01,"
                 "\"stopband_attenuation_db\":%s,\"stopband_one_over_f\":true}", d, d < 20 ? "60.0" : "45.0");
        std::string resp;
        if (request("PUT", "/api/ddc/design", body, resp) / 100 != 2) { fprintf(stderr, "ddc: design D=%d refused\n", d); return false; }
        if (request("GET", "/api/ddc/config", nullptr, resp) / 100 != 2) { fprintf(stderr, "ddc: config unreadable\n"); return false; }
        const double got = number(resp, "decimation");
        if ((int)got != d) { fprintf(stderr, "ddc: decimation %g instead of %d\n", got, d); return false; }
        outHz_ = number(resp, "output_sampling_frequency");
        return true;
    }
    // An RRC matched filter in the DDC for the FPGA symbol front end: output at 2 - 4 samples per symbol, root raised cosine filtered (what the
    // radio's own FIR does in the normal way). fir1 alone when the filter fits; otherwise a low-pass in fir1 brings the rate down to at most
    // 32 samples per symbol and the RRC runs in fir3. False when no layout fits maia-httpd's limits (the caller then filters on the CPU).
    // outHz: the DDC output rate; sps = outHz / rsHz.
    bool designRrc(double finHz, double rsHz, double roll, double& outHz) {
        std::vector<long> h1, h3;
        int d1 = 0, d3 = 0;
        const double ratio = finHz / rsHz;
        if (ratio < 4.0) return false;
        if (ratio <= 32.0) {
            d1 = (int)std::floor(ratio / 2.0);
            if (!rrcStage(finHz, rsHz, roll, d1, true, h1)) return false;
        } else {
            d1 = (int)std::ceil(ratio / 32.0);
            const double fs1 = finHz / d1;
            d3 = (int)std::floor(fs1 / rsHz / 2.0);
            if (d1 < 2 || d3 < 2 || d1 > 127 || !lowpassStage(finHz, d1, h1) || !rrcStage(fs1, rsHz, roll, d3, false, h3)) return false;
        }
        std::string body = "{\"frequency\":0.0,\"fir1\":" + stageJson(h1, d1);
        if (d3) body += ",\"fir3\":" + stageJson(h3, d3);
        body += "}";
        std::string resp;
        const int st = request("PUT", "/api/ddc/config", body.c_str(), resp);
        if (st / 100 != 2) { fprintf(stderr, "ddc: RRC filter refused (%d) %s\n", st, resp.substr(0, 160).c_str()); return false; }
        outHz = finHz / d1 / (d3 ? d3 : 1);
        if (request("GET", "/api/ddc/config", nullptr, resp) / 100 == 2) {
            const double o = number(resp, "output_sampling_frequency");
            if (o > 0 && std::isfinite(o)) outHz = o;
        }
        outHz_ = outHz;
        fprintf(stderr, "ddc: RRC %.2f, %s%d taps / %d%s, %.3f Msps out (%.2f samples per symbol)\n", roll, d3 ? "low-pass " : "",
                (int)h1.size(), d1, d3 ? (", RRC " + std::to_string(h3.size()) + " taps / " + std::to_string(d3)).c_str() : "", outHz / 1e6, outHz / rsHz);
        return true;
    }

    bool enable(bool on) {
        std::string resp;
        const int st = request("PATCH", "/api/ddc/config", on ? "{\"enabled\":true}" : "{\"enabled\":false}", resp);
        if (st / 100 != 2) { fprintf(stderr, "ddc: %s refused (%d)\n", on ? "enable" : "disable", st); return false; }
        enabled_ = on;
        return true;
    }
    // what maia-httpd reports as the output rate after design() (0 when it did not say)
    double outputHz() const { return outHz_ > 0 && std::isfinite(outHz_) ? outHz_ : 0; }
    // the route goes back to the full rate stream for the next user of the RX buffer
    // The DDC's NCO (PATCH /api/ddc/config {"frequency"}, within +-fs/2 of the converter rate): the signal at LO + hz is mixed to DC. design() and
    // designRrc() reset it to 0, so call this after them.
    bool setFrequency(double hz) {
        char body[64];
        snprintf(body, sizeof body, "{\"frequency\":%.1f}", hz);
        std::string resp;
        const int st = request("PATCH", "/api/ddc/config", body, resp);
        if (st / 100 != 2) { fprintf(stderr, "ddc: NCO %.1f Hz refused (%d) %s\n", hz, st, resp.substr(0, 160).c_str()); return false; }
        nco_ = hz != 0;
        return true;
    }
    ~MaiaDdc() { if (nco_) setFrequency(0); if (enabled_) enable(false); }

private:
    bool enabled_ = false, nco_ = false;
    double outHz_ = 0;

    // maia-httpd's limits (maia-httpd src/ddc/constants.rs, ddc.rs): 18-bit coefficients, stage decimation 2 - 127, operations = ceil(len / d)
    // (halved for the folded fir1 and fir3) at most 128, operations x input rate at most 187.5 MHz, operations x d within the coefficient RAM
    // (256 for fir1 and fir3). Gain: sum |h| about 2^21 for fir1, 2^18 for fir3 (MACC_TRUNC + WIDTH_GROWTH), as its own designer scales.
    static bool fits(size_t len, int d, double fsIn) {
        const long ops = ((long)((len + d - 1) / d) + 1) / 2;
        return d >= 2 && d <= 127 && ops <= 128 && ops * fsIn <= 187.5e6 && ops * d <= 256;
    }
    static void scale(const std::vector<double>& h, bool first, std::vector<long>& out) {
        double sumAbs = 0, maxAbs = 0;
        for (double v : h) { sumAbs += std::fabs(v); maxAbs = std::max(maxAbs, std::fabs(v)); }
        const double g = std::min(131071.0 / maxAbs, (first ? 2097152.0 : 262144.0) / sumAbs);
        out.clear();
        for (double v : h) out.push_back(std::lround(v * g));
    }
    // root raised cosine at fs (rs symbols/s), span 8 symbols (down to 4 when it does not fit), for a stage decimating by d
    static bool rrcStage(double fs, double rs, double a, int d, bool first, std::vector<long>& out) {
        const double pi = 3.14159265358979323846, sps = fs / rs;
        for (int span = 8; span >= 4; span--) {
            const int half = (int)std::floor(span * sps / 2);
            const size_t len = (size_t)(2 * half + 1);
            if (!fits(len, d, fs)) continue;
            std::vector<double> h(len);
            for (int k = -half; k <= half; k++) {
                const double x = k / sps;
                double v;
                if (std::fabs(x) < 1e-9) v = 1 - a + 4 * a / pi;
                else if (std::fabs(std::fabs(x) - 1 / (4 * a)) < 1e-9) v = a / std::sqrt(2.0) * ((1 + 2 / pi) * std::sin(pi / (4 * a)) + (1 - 2 / pi) * std::cos(pi / (4 * a)));
                else v = (std::sin(pi * x * (1 - a)) + 4 * a * x * std::cos(pi * x * (1 + a))) / (pi * x * (1 - 16 * a * a * x * x));
                h[(size_t)(k + half)] = v;
            }
            scale(h, first, out);
            return true;
        }
        return false;
    }
    // Blackman windowed sinc, cut-off at a quarter of the output rate: the signal (at most 0.17 of it, 4 or more samples per symbol) passes flat
    // and what folds back onto it is 60 dB down
    static bool lowpassStage(double fs, int d, std::vector<long>& out) {
        const double pi = 3.14159265358979323846, fc = 0.25 / d;   // of the input rate
        const int half = 5 * d;
        const size_t len = (size_t)(2 * half + 1);
        if (!fits(len, d, fs)) return false;
        std::vector<double> h(len);
        for (int k = -half; k <= half; k++) {
            const double s = k == 0 ? 2 * fc : std::sin(2 * pi * fc * k) / (pi * k);
            const double w = 0.42 + 0.5 * std::cos(pi * k / half) + 0.08 * std::cos(2 * pi * k / half);
            h[(size_t)(k + half)] = s * w;
        }
        scale(h, true, out);
        return true;
    }
    static std::string stageJson(const std::vector<long>& h, int d) {
        std::string s = "{\"decimation\":" + std::to_string(d) + ",\"coefficients\":[";
        for (size_t i = 0; i < h.size(); i++) s += (i ? "," : "") + std::to_string(h[i]);
        return s + "]}";
    }

    static double number(const std::string& json, const char* key) {
        const std::string k = std::string("\"") + key + "\"";
        size_t p = json.find(k);
        if (p == std::string::npos) return NAN;
        p = json.find(':', p + k.size());
        return p == std::string::npos ? NAN : strtod(json.c_str() + p + 1, nullptr);
    }
    // minimal HTTP/1.0 to localhost:80; returns the status code, 0 when unreachable
    static int request(const char* method, const char* path, const char* body, std::string& resp) {
        resp.clear();
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return 0;
        timeval tv{5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(80);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (sockaddr*)&a, sizeof a) < 0) { close(fd); return 0; }
        const size_t blen = body ? strlen(body) : 0;
        std::string req = std::string(method) + " " + path + " HTTP/1.0\r\nHost: localhost\r\nAccept: application/json\r\n";
        if (body) req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(blen) + "\r\n";
        req += "Connection: close\r\n\r\n";
        if (body) req += body;
        for (size_t off = 0; off < req.size();) {
            const ssize_t w = send(fd, req.data() + off, req.size() - off, MSG_NOSIGNAL);
            if (w <= 0) { close(fd); return 0; }
            off += (size_t)w;
        }
        char buf[4096];
        for (ssize_t r; (r = recv(fd, buf, sizeof buf, 0)) > 0 && resp.size() < 256 * 1024;) resp.append(buf, (size_t)r);
        close(fd);
        int status = 0;
        if (sscanf(resp.c_str(), "HTTP/%*d.%*d %d", &status) != 1) return 0;
        const size_t h = resp.find("\r\n\r\n");
        resp = h == std::string::npos ? std::string() : resp.substr(h + 4);
        return status;
    }
};
