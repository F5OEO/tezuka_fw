// Transport stream packets out over a WebSocket server (zynqrx rx --ws port): every client gets the stream as binary messages of up to 7 packets
// (1316 bytes), in parallel with the UDP output. A client that cannot keep up is dropped, never waited for.
#pragma once
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace wsdetail {
inline uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
inline void sha1(const uint8_t* d, size_t n, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> m(d, d + n);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    const uint64_t bits = (uint64_t)n * 8;
    for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)m[o + 4 * i] << 24 | (uint32_t)m[o + 4 * i + 1] << 16 | (uint32_t)m[o + 4 * i + 2] << 8 | m[o + 4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], dd = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & dd); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ dd; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & dd) | (c & dd); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ dd; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = dd; dd = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += dd; h[4] += e;
    }
    for (int i = 0; i < 5; i++) for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}
inline std::string base64(const uint8_t* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        s += t[v >> 18 & 63]; s += t[v >> 12 & 63];
        s += i + 1 < n ? t[v >> 6 & 63] : '='; s += i + 2 < n ? t[v & 63] : '=';
    }
    return s;
}
}  // namespace wsdetail

class WsOut {
public:
    static constexpr size_t kMaxPackets = 7;
    static constexpr int kJoinWait = 5;                // seconds a new client waits for a keyframe
    static constexpr size_t kMaxBacklog = 4u << 20;   // bytes queued for one client before it is dropped
    WsOut() = default;
    WsOut(const WsOut&) = delete;
    WsOut& operator=(const WsOut&) = delete;
    ~WsOut() { close(); }

    // A new client normally starts as soon as it connects (after the PAT and PMT), whatever the packet. With true it waits for a keyframe
    // (up to kJoinWait seconds) so that a player decodes from its first packet.
    void waitForKeyframe(bool on) { std::lock_guard<std::mutex> lk(mu_); waitKey_ = on; }

    // Listens on all interfaces. Clients connect with ws://board:port/stream.ts.
    bool open(int port) {
        close();
        if (port <= 0 || port > 65535) { fprintf(stderr, "ws: bad port %d\n", port); return false; }
        lfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (lfd_ < 0) { perror("ws: socket"); return false; }
        const int one = 1;
        setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((uint16_t)port);
        if (bind(lfd_, (sockaddr*)&a, sizeof a) < 0 || listen(lfd_, 8) < 0) { perror("ws: bind/listen"); ::close(lfd_); lfd_ = -1; return false; }
        run_ = true;
        th_ = std::thread([this] { acceptLoop(); });
        fprintf(stderr, "ws: serving the transport stream on ws://0.0.0.0:%d/stream.ts (binary messages of up to %zu packets)\n", port, kMaxPackets);
        return true;
    }
    void close() {
        run_ = false;
        if (th_.joinable()) th_.join();
        if (lfd_ >= 0) { ::close(lfd_); lfd_ = -1; }
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& c : clients_) ::close(c.fd);
        clients_.clear();
        fill_ = 0;
    }
    bool isOpen() const { return lfd_ >= 0; }
    size_t clients() { std::lock_guard<std::mutex> lk(mu_); return clients_.size(); }

    // n packets of 188 bytes. Never blocks.
    void push(const uint8_t* p, size_t n) {
        if (lfd_ < 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        for (size_t i = 0; i < n; i++, p += 188) {
            memcpy(buf_ + fill_ * 188, p, 188);
            inspect(p, fill_);
            if (++fill_ == kMaxPackets) sendFrame();
        }
    }
    // sends a partial message
    void flush() {
        if (lfd_ < 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        if (fill_) sendFrame();
    }

private:
    struct Client { int fd; std::vector<uint8_t> out; bool started = false; std::chrono::steady_clock::time_point born; };

    // One WebSocket message of n packets to a client; false when the client has to go.
    bool sendTo(Client& c, const uint8_t* pk, size_t n) {
        const size_t len = n * 188;
        uint8_t frame[4 + (kMaxPackets + 2) * 188];
        frame[0] = 0x82;   // FIN, binary
        frame[1] = 126;
        frame[2] = (uint8_t)(len >> 8);
        frame[3] = (uint8_t)len;
        memcpy(frame + 4, pk, len);
        const uint8_t* d = frame;
        size_t left = len + 4;
        if (c.out.empty()) {
            const ssize_t s = send(c.fd, d, left, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (s < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) return false; }
            else { d += s; left -= (size_t)s; }
        }
        if (left) {   // queue the rest, then try to drain
            c.out.insert(c.out.end(), d, d + left);
            const ssize_t s = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (s < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) return false; }
            else c.out.erase(c.out.begin(), c.out.begin() + s);
            if (c.out.size() > kMaxBacklog) { fprintf(stderr, "ws: dropping a client that is too slow\n"); return false; }
        }
        return true;
    }

    void sendFrame() {   // mu_ held
        const size_t n = fill_;
        fill_ = 0;
        const auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < clients_.size();) {
            Client& c = clients_[i];
            bool ok = true;
            if (c.started) ok = sendTo(c, buf_, n);
            else {
                // A new client starts at a keyframe, after the PAT and PMT, so that a player can decode from its first packet. Without a keyframe
                // in sight (audio only, no PMT yet, an unknown codec) it starts after kJoinWait seconds with what there is.
                const bool late = now - c.born > std::chrono::seconds(kJoinWait);
                if (!waitKey_ || keyIdx_ >= 0 || late) {
                    uint8_t m[(kMaxPackets + 2) * 188];
                    size_t k = 0;
                    if (havePat_) { memcpy(m, pat_, 188); k++; }
                    if (havePmt_) { memcpy(m + k * 188, pmt_, 188); k++; }
                    const size_t from = keyIdx_ >= 0 ? (size_t)keyIdx_ : 0;
                    memcpy(m + k * 188, buf_ + from * 188, (n - from) * 188);
                    ok = sendTo(c, m, k + n - from);
                    c.started = true;
                }
            }
            if (!ok) { ::close(c.fd); clients_.erase(clients_.begin() + i); }
            else i++;
        }
        keyIdx_ = -1;
    }

    // Follows the PAT and PMT (kept to start new clients) and finds the packets a decoder can start from.
    void inspect(const uint8_t* p, size_t idx) {   // mu_ held
        if (p[0] != 0x47) return;
        const int pid = (p[1] & 0x1F) << 8 | p[2];
        const int afc = (p[3] >> 4) & 3;
        size_t off = 4;
        if (afc & 2) off += 1 + p[4];
        if (!(afc & 1) || off >= 188) return;
        const bool pusi = p[1] & 0x40;
        if (pusi && (pid == 0 || pid == pmtPid_)) {
            const size_t st = off + 1 + p[off];   // after the pointer field
            if (st + 12 > 188) return;
            const uint8_t* t = p + st;
            const size_t sl = ((t[1] & 0x0F) << 8 | t[2]) + 3;
            if (st + sl > 188) return;   // a section over several packets is not kept
            if (pid == 0 && t[0] == 0) {
                for (size_t i = 8; i + 4 <= sl - 4; i += 4)
                    if ((t[i] << 8 | t[i + 1]) != 0) { pmtPid_ = (t[i + 2] & 0x1F) << 8 | t[i + 3]; break; }
                memcpy(pat_, p, 188); havePat_ = true;
            } else if (pid == pmtPid_ && t[0] == 2) {
                const size_t pil = (t[10] & 0x0F) << 8 | t[11];
                for (size_t i = 12 + pil; i + 5 <= sl - 4;) {
                    const int type = t[i], epid = (t[i + 1] & 0x1F) << 8 | t[i + 2];
                    if (type == 0x1B || type == 0x24 || type == 0x02 || type == 0x01 || type == 0x10) { vidPid_ = epid; vidType_ = type; break; }
                    i += 5 + ((t[i + 3] & 0x0F) << 8 | t[i + 4]);
                }
                memcpy(pmt_, p, 188); havePmt_ = true;
            }
            return;
        }
        if (pid != vidPid_ || !pusi || keyIdx_ >= 0) return;
        bool key = (afc & 2) && p[4] >= 1 && (p[5] & 0x40);   // random access indicator
        for (size_t i = off; !key && i + 4 < 188; i++) {
            if (p[i] || p[i + 1] || p[i + 2] != 1) continue;
            const int nal = p[i + 3];
            if (vidType_ == 0x1B) key = (nal & 0x1F) == 5 || (nal & 0x1F) == 7;                                  // IDR, SPS
            else if (vidType_ == 0x24) { const int t = (nal >> 1) & 0x3F; key = t == 32 || t == 33 || t == 19 || t == 20; }   // VPS, SPS, IDR
            else if (vidType_ == 0x02 || vidType_ == 0x01) key = nal == 0xB3;                                      // sequence header
        }
        if (key) keyIdx_ = (int)idx;
    }

    void acceptLoop() {
        while (run_) {
            pollfd pf{lfd_, POLLIN, 0};
            if (poll(&pf, 1, 200) <= 0) continue;
            const int fd = accept(lfd_, nullptr, nullptr);
            if (fd < 0) continue;
            if (handshake(fd)) {
                const int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
                std::lock_guard<std::mutex> lk(mu_);
                clients_.push_back({fd, {}, false, std::chrono::steady_clock::now()});
                fprintf(stderr, "ws: client connected (%zu)\n", clients_.size());
            } else ::close(fd);
        }
    }

    static bool handshake(int fd) {
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        std::string req;
        char b[512];
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
            const ssize_t r = recv(fd, b, sizeof b, 0);
            if (r <= 0) return false;
            req.append(b, (size_t)r);
        }
        // GET /stream.ts only
        const size_t sp = req.find(' '), sp2 = sp == std::string::npos ? sp : req.find(' ', sp + 1);
        std::string path = sp2 == std::string::npos ? "" : req.substr(sp + 1, sp2 - sp - 1);
        path = path.substr(0, path.find('?'));
        if (path != "/stream.ts") {
            static const char msg[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(fd, msg, sizeof msg - 1, MSG_NOSIGNAL);
            return false;
        }
        const size_t k = req.find("Sec-WebSocket-Key:");
        if (k == std::string::npos) {
            static const char msg[] = "HTTP/1.1 426 Upgrade Required\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(fd, msg, sizeof msg - 1, MSG_NOSIGNAL);
            return false;
        }
        size_t s = k + 18, e = req.find("\r\n", s);
        while (s < e && req[s] == ' ') s++;
        std::string key = req.substr(s, e - s);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) key.pop_back();
        key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t h[20];
        wsdetail::sha1((const uint8_t*)key.data(), key.size(), h);
        const std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + wsdetail::base64(h, 20) + "\r\n\r\n";
        return send(fd, resp.data(), resp.size(), MSG_NOSIGNAL) == (ssize_t)resp.size();
    }

    int lfd_ = -1;
    std::atomic<bool> run_{false};
    std::thread th_;
    std::mutex mu_;
    std::vector<Client> clients_;
    uint8_t buf_[kMaxPackets * 188];
    size_t fill_ = 0;
    bool waitKey_ = false;                  // hold a new client until a keyframe (see waitForKeyframe)
    int keyIdx_ = -1;                       // first packet of buf_ a decoder can start from
    uint8_t pat_[188], pmt_[188];
    bool havePat_ = false, havePmt_ = false;
    int pmtPid_ = -1, vidPid_ = -1, vidType_ = 0;
};
#else
class WsOut {
public:
    bool open(int) { fprintf(stderr, "websocket output is not available on Windows in this tool\n"); return false; }
    bool isOpen() const { return false; }
    void push(const uint8_t*, size_t) {}
    void flush() {}
    void close() {}
};
#endif
