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
    ~MaiaDdc() { if (enabled_) enable(false); }

private:
    bool enabled_ = false;
    double outHz_ = 0;

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
