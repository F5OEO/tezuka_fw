// Transport stream packets out over UDP (zynqrx rx --udp addr:port): up to 7 packets (1316 bytes) per datagram, unicast or multicast (IPv4).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class UdpOut {
public:
    static constexpr size_t kMaxPackets = 7;
    UdpOut() = default;
    UdpOut(const UdpOut&) = delete;
    UdpOut& operator=(const UdpOut&) = delete;
    ~UdpOut() { close(); }
    void close() { flush(); if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

    // dest: "addr:port" (or "udp://addr:port"); multicast when addr is 224.0.0.0 - 239.255.255.255. ttl: multicast hop limit. ifAddr: the local interface
    // address for multicast (empty = the system's choice).
    bool open(std::string dest, int ttl, const std::string& ifAddr) {
        close();   // opening again moves the stream to the new destination
        if (dest.rfind("udp://", 0) == 0) dest = dest.substr(6);
        const size_t c = dest.rfind(':');
        if (c == std::string::npos || c == 0) { fprintf(stderr, "udp: give the destination as address:port\n"); return false; }
        const int port = atoi(dest.c_str() + c + 1);
        if (port <= 0 || port > 65535) { fprintf(stderr, "udp: bad port in %s\n", dest.c_str()); return false; }
        memset(&to_, 0, sizeof to_);
        to_.sin_family = AF_INET;
        to_.sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, dest.substr(0, c).c_str(), &to_.sin_addr) != 1) { fprintf(stderr, "udp: %s is not an IPv4 address\n", dest.substr(0, c).c_str()); return false; }
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) { perror("udp: socket"); return false; }
        const bool multicast = (ntohl(to_.sin_addr.s_addr) >> 28) == 0xE;
        if (multicast) {
            const unsigned char t = (unsigned char)ttl;
            setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &t, sizeof t);
            if (!ifAddr.empty()) {
                in_addr ia;
                if (inet_pton(AF_INET, ifAddr.c_str(), &ia) != 1) { fprintf(stderr, "udp: %s is not an IPv4 address\n", ifAddr.c_str()); return false; }
                setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &ia, sizeof ia);
            }
        }
        fprintf(stderr, "udp: sending the transport stream to %s (%s, up to %zu packets per datagram)\n", dest.c_str(), multicast ? "multicast" : "unicast", kMaxPackets);
        return true;
    }
    bool isOpen() const { return fd_ >= 0; }

    // n packets of 188 bytes
    void push(const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; i++, p += 188) {
            memcpy(buf_ + fill_ * 188, p, 188);
            if (++fill_ == kMaxPackets) flush();
        }
    }
    // sends what is queued (a partial datagram); the receiver gets the stream with at most a frame's delay otherwise
    void flush() {
        if (fd_ < 0 || !fill_) return;
        if (sendto(fd_, buf_, fill_ * 188, 0, (const sockaddr*)&to_, sizeof to_) < 0) errors_++;
        fill_ = 0;
    }
    uint64_t errors() const { return errors_; }

private:
    int fd_ = -1;
    sockaddr_in to_{};
    uint8_t buf_[kMaxPackets * 188];
    size_t fill_ = 0;
    uint64_t errors_ = 0;
};
#else
class UdpOut {
public:
    bool open(const std::string&, int, const std::string&) { fprintf(stderr, "udp output is not available on Windows in this tool\n"); return false; }
    bool isOpen() const { return false; }
    void push(const uint8_t*, size_t) {}
    void flush() {}
    uint64_t errors() const { return 0; }
};
#endif
