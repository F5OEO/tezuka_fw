// Service name, provider name and elementary streams from the PSI of a transport stream (PAT, PMT, SDT), for the status messages.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class TsPsi {
public:
    // one 188 byte packet
    void push(const uint8_t* p) {
        if (p[0] != 0x47 || (p[1] & 0x80)) return;
        const int pid = (p[1] & 0x1F) << 8 | p[2];
        if (pid != 0 && pid != 17 && pid != pmtPid_) return;
        if (!(p[3] & 0x10)) return;
        int off = 4;
        if (p[3] & 0x20) off += 1 + p[4];
        if (off >= 188) return;
        Asm& a = asm_[pid];
        if (p[1] & 0x40) {                         // a section starts here
            const int ptr = p[off];
            if (off + 1 + ptr > 188) return;
            // the end of the previous section can be before the pointer: not needed, sections are taken whole when they start
            a.buf.assign(p + off + 1 + ptr, p + 188);
            a.active = true;
        } else if (a.active) a.buf.insert(a.buf.end(), p + off, p + 188);
        else return;
        while (a.active && a.buf.size() >= 3) {
            const size_t len = 3 + ((a.buf[1] & 0x0F) << 8 | a.buf[2]);
            if (a.buf[0] == 0xFF) { a.active = false; break; }                  // stuffing
            if (a.buf.size() < len) break;
            section(pid, a.buf.data(), len);
            a.buf.erase(a.buf.begin(), a.buf.begin() + (std::ptrdiff_t)len);
            if (a.buf.empty()) a.active = false;
        }
    }
    void get(std::string& service, std::string& provider, std::vector<std::pair<int, int>>& es) {
        std::lock_guard<std::mutex> lk(mu_);
        service = service_; provider = provider_; es = es_;
    }
    void reset() { std::lock_guard<std::mutex> lk(mu_); service_.clear(); provider_.clear(); es_.clear(); asm_.clear(); pmtPid_ = -1; }

private:
    struct Asm { std::vector<uint8_t> buf; bool active = false; };
    std::map<int, Asm> asm_;
    int pmtPid_ = -1, program_ = -1;
    std::mutex mu_;
    std::string service_, provider_;
    std::vector<std::pair<int, int>> es_;

    static std::string text(const uint8_t* d, int n) {
        if (n > 0 && d[0] < 0x20) { d++; n--; }       // a leading character table selector
        std::string s((const char*)d, (size_t)n);
        for (auto& c : s) if ((unsigned char)c < 0x20) c = ' ';
        return s;
    }
    void section(int pid, const uint8_t* s, size_t len) {
        std::lock_guard<std::mutex> lk(mu_);
        const int table = s[0];
        if (pid == 0 && table == 0x00) {              // PAT: the first program that has a PMT
            for (size_t i = 8; i + 4 <= len - 4; i += 4) {
                const int prog = s[i] << 8 | s[i + 1], p = (s[i + 2] & 0x1F) << 8 | s[i + 3];
                if (prog != 0) { pmtPid_ = p; program_ = prog; break; }
            }
        } else if (pid == pmtPid_ && table == 0x02) {  // PMT
            const int pil = (s[10] & 0x0F) << 8 | s[11];
            size_t i = 12 + (size_t)pil;
            std::vector<std::pair<int, int>> es;
            while (i + 5 <= len - 4 && es.size() < 16) {
                const int type = s[i], epid = (s[i + 1] & 0x1F) << 8 | s[i + 2], il = (s[i + 3] & 0x0F) << 8 | s[i + 4];
                es.emplace_back(epid, type);
                i += 5 + (size_t)il;
            }
            if (!es.empty()) es_ = es;
        } else if (pid == 17 && table == 0x42) {      // SDT, this transport stream
            size_t i = 11;
            while (i + 5 <= len - 4) {
                const int sid = s[i] << 8 | s[i + 1], dl = (s[i + 3] & 0x0F) << 8 | s[i + 4];
                size_t d = i + 5;
                const size_t end = std::min(len - 4, d + (size_t)dl);
                while (d + 2 <= end) {
                    const int tag = s[d], l = s[d + 1];
                    if (tag == 0x48 && d + 2 + (size_t)l <= end && l >= 3) {
                        const int pl = s[d + 3];
                        if (2 + pl < l) {                                    // the name length byte is inside the descriptor
                            const int nl = s[d + 4 + pl];
                            if (3 + pl + nl <= l && (program_ < 0 || sid == program_ || service_.empty())) {
                                provider_ = text(s + d + 4, pl);
                                service_ = text(s + d + 5 + pl, nl);
                            }
                        }
                    }
                    d += 2 + (size_t)l;
                }
                i = end;
            }
        }
    }
};
