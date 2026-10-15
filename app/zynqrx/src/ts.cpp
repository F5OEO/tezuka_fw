#include "dect2/text_charset.h"
#include "dect2/ts.h"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace dect2 {

uint32_t mpegCrc32(const uint8_t* p, int n) {
    static uint32_t tab[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
            tab[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) crc = (crc << 8) ^ tab[(crc >> 24) ^ p[i]];
    return crc;
}

#ifdef _WIN32
// Windows has the ISO 8859 tables built in (code pages 28591..28599, 28603, 28605): no iconv needed.
static std::string convert(const char* charset, const uint8_t* p, int n) {
    std::wstring w;
    if (!strcmp(charset, "UCS-2BE")) {
        for (int i = 0; i + 1 < n; i += 2) w += (wchar_t)((p[i] << 8) | p[i + 1]);
    } else {
        UINT cp = 28591;
        int v = 0;
        if (sscanf(charset, "ISO-8859-%d", &v) == 1) {
            if ((v >= 1 && v <= 9) || v == 13 || v == 15) cp = 28590 + v;
            else if (v == 11) cp = 874;   // Thai
        } else if (!strcmp(charset, "BIG5")) cp = 950;
        else if (!strcmp(charset, "GB2312")) cp = 936;
        else if (!strcmp(charset, "EUC-KR")) cp = 949;
        const int wn = MultiByteToWideChar(cp, 0, (const char*)p, n, nullptr, 0);
        if (wn <= 0) return std::string((const char*)p, n);
        w.resize((size_t)wn);
        MultiByteToWideChar(cp, 0, (const char*)p, n, w.data(), wn);
    }
    if (w.empty()) return {};
    const int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(un > 0 ? un : 0), '\0');
    if (un > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), un, nullptr, nullptr);
    return out;
}
#else
static std::string convert(const char* charset, const uint8_t* p, int n) {
    iconv_t cd = iconv_open("UTF-8", charset);
    if (cd == (iconv_t)-1) return std::string((const char*)p, n);
    std::string out(n * 4 + 8, '\0');
    char* in = (char*)p;
    size_t inLeft = n, outLeft = out.size();
    char* o = &out[0];
    iconv(cd, &in, &inLeft, &o, &outLeft);
    iconv_close(cd);
    out.resize(out.size() - outLeft);
    return out;
}
#endif

// Unprefixed text that is a run of valid GB 2312 (EUC-CN) pairs, as some Chinese DTMB muxes send.
static bool looksGb2312(const uint8_t* p, int n) {
    if (n < 2 || p[0] < 0xA1) return false;
    int pairs = 0;
    for (int i = 0; i < n;) {
        if (p[i] < 0x80) { i++; continue; }
        if (i + 1 >= n || p[i] < 0xA1 || p[i] > 0xF7 || p[i + 1] < 0xA1 || p[i + 1] > 0xFE) return false;
        i += 2; pairs++;
    }
    return pairs > 0;
}

std::string dvbText(const uint8_t* p, int n, bool gbDefault) {
    if (n <= 0) return {};
    std::string s;
    auto clean = [](std::string t) {
        std::string r;
        for (size_t i = 0; i < t.size(); i++) {
            unsigned char c = t[i];
            if (c == 0xC2 && i + 1 < t.size() && (unsigned char)t[i + 1] >= 0x80 && (unsigned char)t[i + 1] < 0xA0) { i++; continue; } // C1 controls
            if (c == 0x0A || c == 0x0D) { r += ' '; continue; }
            if (c >= 0x20 || c >= 0x80) r += (char)c;
        }
        return r;
    };
    unsigned first = p[0];
    if (gbDefault && looksGb2312(p, n)) s = convert("GB2312", p, n);
    else if (first >= 0x20) s = text::iso6937(p, (size_t)n);                         // default table: ISO/IEC 6937 (EN 300 468 Annex A)
    else if (first >= 0x01 && first <= 0x0B && first != 0x0 && n > 1) {
        static const char* cs[] = {"", "ISO-8859-5", "ISO-8859-6", "ISO-8859-7", "ISO-8859-8", "ISO-8859-9", "ISO-8859-10", "ISO-8859-11", "", "ISO-8859-13", "ISO-8859-14", "ISO-8859-15"};
        s = convert(cs[first], p + 1, n - 1);
    } else if (first == 0x10 && n > 3) {
        char name[24]; snprintf(name, sizeof name, "ISO-8859-%d", (p[1] << 8) | p[2]);
        s = convert(name, p + 3, n - 3);
    } else if (first == 0x11 && n > 1) s = convert("UCS-2BE", p + 1, n - 1);
    else if (first == 0x15 && n > 1) s = std::string((const char*)p + 1, n - 1);
    else if (first == 0x14 && n > 1) s = convert("BIG5", p + 1, n - 1);
    else if (first == 0x13 && n > 1) s = convert("GB2312", p + 1, n - 1);   // GB-2312-1980 (EUC-CN form)
    else if (first == 0x12 && n > 1) s = convert("EUC-KR", p + 1, n - 1);   // KS X 1001-2004
    else s = text::iso6937(p + 1, (size_t)(n - 1));   // reserved selector: the default table
    return text::clean(clean(s));
}

const char* TsService::typeName() const {
    switch (type) {
    case 0x01: case 0x11: case 0x16: case 0x17: case 0x19: case 0x1A: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: return "TV";
    case 0x02: case 0x0A: return "radio";
    case 0x0C: return "data";
    case 0x03: return "teletext";
    default: return type ? "other" : "?";
    }
}

static void buildPatPacket(int tsid, int sid, int pmtPid, int& cc, uint8_t* out) {
    uint8_t sec[16];
    sec[0] = 0x00; sec[1] = 0xB0; sec[2] = 13;
    sec[3] = tsid >> 8; sec[4] = tsid & 0xFF;
    sec[5] = 0xC1; sec[6] = 0; sec[7] = 0;
    sec[8] = sid >> 8; sec[9] = sid & 0xFF;
    sec[10] = 0xE0 | (pmtPid >> 8); sec[11] = pmtPid & 0xFF;
    uint32_t crc = mpegCrc32(sec, 12);
    sec[12] = crc >> 24; sec[13] = crc >> 16; sec[14] = crc >> 8; sec[15] = crc;
    memset(out, 0xFF, 188);
    out[0] = 0x47; out[1] = 0x40; out[2] = 0x00; out[3] = 0x10 | (cc++ & 15);
    out[4] = 0x00;
    memcpy(out + 5, sec, 16);
}

bool ServiceFilter::process(const uint8_t* in, const TsSnapshot* snap, uint8_t* out) {
    if (sid_ < 0) { memcpy(out, in, 188); return true; }
    if (snap && (patSid_ != sid_ || pass_.empty())) {
        for (auto& sv : snap->services) if (sv.id == sid_ && sv.havePmt) {
            pass_ = {0x11, 0x12, 0x14, sv.pmtPid, sv.pcrPid};
            for (auto& st : sv.streams) pass_.push_back(st.pid);
            patSid_ = sv.id; patPmt_ = sv.pmtPid; patTsid_ = snap->tsid;
        }
    }
    if (pass_.empty()) return false;
    const int pid = ((in[1] & 0x1F) << 8) | in[2];
    if (pid == 0) {
        if (!(in[1] & 0x40)) return false;
        buildPatPacket(patTsid_, patSid_, patPmt_, patCc_, out);
        return true;
    }
    for (int p : pass_) if (p == pid) { memcpy(out, in, 188); return true; }
    return false;
}

void TsDemux::reset() { *this = TsDemux(); }

void TsDemux::markLoss() { for (auto& kv : pids_) kv.second.resync = true; for (auto& kv : sec_) kv.second.active = false; }

void TsDemux::feed(const uint8_t* p) {
    if (p[0] != 0x47) return;
    const int pid = ((p[1] & 0x1F) << 8) | p[2];
    const bool tei = p[1] & 0x80, pusi = p[1] & 0x40;
    const int scr = (p[3] >> 6) & 3, afc = (p[3] >> 4) & 3, cc = p[3] & 15;
    total_++; totalWin_++;
    PidState& st = pids_[pid];
    st.packets++; st.bytesWin += 188;
    if (pid == 0x1FFF) { nulls_++; nullWin_++; return; }
    if (tei) { tei_++; st.tei++; return; }
    // (only packets with a payload: the adaptation field is never scrambled, and a PCR-only packet of a scrambled stream may say 00)
    if (afc & 1) st.scrambled = scr != 0;
    if (afc & 1) {
        if (!st.resync && st.lastCc >= 0 && cc != ((st.lastCc + 1) & 15) && !(cc == st.lastCc && afc == 2)) { st.ccErrors++; ccErr_++; }
        st.lastCc = cc;
        st.resync = false;
    } else if (afc == 2) st.resync = false;
    if (!(afc & 1) || scr) return;
    int off = 4;
    if (afc == 3) off += 1 + p[4];
    if (off >= 188) return;
    // only PSI/SI PIDs and PMT PIDs carry sections we care about
    const bool psi = pid == 0 || pid == 0x10 || pid == 0x11 || pid == 0x12 || pid == 0x14 || pid == 0x1FFB || pmtToService_.count(pid);
    if (!psi) return;
    SectionBuf& sb = sec_[pid];
    const uint8_t* d = p + off;
    int n = 188 - off;
    if (pusi) {
        int ptr = d[0];
        // a pointer past the end of the packet: a corrupt packet that error correction missed (a Reed-Solomon miscorrection). Forget the
        // partial section and wait for the next one, rather than building a section from a backwards range.
        if (1 + ptr > n) { sb.data.clear(); sb.active = false; return; }
        if (sb.active && ptr > 0 && ptr < n) { sb.data.insert(sb.data.end(), d + 1, d + 1 + ptr); }
        // the previous section may be complete now
        auto tryEmit = [&]() {
            while (sb.data.size() >= 3) {
                int sl = ((sb.data[1] & 0x0F) << 8) | sb.data[2];
                if ((int)sb.data.size() < sl + 3) break;
                section(pid, sb.data.data(), sl + 3);
                sb.data.erase(sb.data.begin(), sb.data.begin() + sl + 3);
                if (!sb.data.empty() && sb.data[0] == 0xFF) { sb.data.clear(); break; }
            }
        };
        if (sb.active) tryEmit();
        sb.data.assign(d + 1 + ptr, d + n);
        sb.active = true;
        tryEmit();
    } else if (sb.active) {
        sb.data.insert(sb.data.end(), d, d + n);
        while (sb.data.size() >= 3) {
            int sl = ((sb.data[1] & 0x0F) << 8) | sb.data[2];
            if ((int)sb.data.size() < sl + 3) break;
            section(pid, sb.data.data(), sl + 3);
            sb.data.erase(sb.data.begin(), sb.data.begin() + sl + 3);
            if (!sb.data.empty() && sb.data[0] == 0xFF) { sb.data.clear(); break; }
        }
    }
}

void TsDemux::section(int pid, const uint8_t* s, int len) {
    if (len < 8) return;
    const int tid = s[0];
    // Every table parsed here carries a CRC_32 except the TDT (the TOT has one although its syntax indicator is 0). Decide by the
    // table id, not by the syntax bit: a corrupted section whose syntax bit reads 0 would otherwise skip the check and fill the
    // service list with phantom programs from a damaged PAT.
    if (tid != 0x70 && mpegCrc32(s, len) != 0) return;
    if (tid == 0x00) parsePat(s, len);
    else if (tid == 0x02) parsePmt(pid, s, len);
    else if (tid == 0x42) parseSdt(s, len);
    else if (tid == 0x40) parseNit(s, len);
    else if (tid == 0x4E || (tid >= 0x50 && tid <= 0x5F)) parseEit(s, len);
    else if (tid == 0x70 || tid == 0x73) parseTdt(s, len);
    else if (pid == 0x1FFB && (tid == 0xC8 || tid == 0xC9)) parseTvct(s, len);
}

// ATSC A/65 virtual channel table: for each channel a 7 character name, major.minor number, the program number it maps to and
// the service type. Shown like DVB services (name, number in the provider line).
void TsDemux::parseTvct(const uint8_t* s, int len) {
    if (len < 14) return;
    tsid_ = (s[3] << 8) | s[4];
    const int n = s[9];
    int i = 10;
    for (int c = 0; c < n && i + 32 <= len - 4; c++) {
        std::string name;
        for (int k = 0; k < 7; k++) {
            const int hi = s[i + 2 * k], lo = s[i + 2 * k + 1];
            if (hi == 0 && lo == 0) break;
            name += hi == 0 && lo >= 0x20 && lo < 0x7F ? (char)lo : '?';
        }
        const int major = ((s[i + 14] & 0x0F) << 6) | (s[i + 15] >> 2);
        const int minor = ((s[i + 15] & 0x03) << 8) | s[i + 16];
        const int prog = (s[i + 24] << 8) | s[i + 25];
        const int stype = s[i + 27] & 0x3F;   // 2 digital TV, 3 audio, 4 data
        const int dl = ((s[i + 30] & 0x03) << 8) | s[i + 31];
        if (prog != 0 && prog != 0xFFFF) {
            TsService& sv = services_[prog];
            sv.id = prog;
            sv.name = name;
            sv.haveSdt = true;
            sv.lcn = major * 100 + minor;
            sv.type = stype == 2 ? 0x01 : stype == 3 ? 0x02 : 0x0C;
            char b[40];
            snprintf(b, sizeof b, "channel %d.%d", major, minor);
            sv.provider = b;
        }
        i += 32 + dl;
    }
}

void TsDemux::parsePat(const uint8_t* s, int len) {
    tsid_ = (s[3] << 8) | s[4];
    for (int i = 8; i + 4 <= len - 4; i += 4) {
        int prog = (s[i] << 8) | s[i + 1];
        int pid = ((s[i + 2] & 0x1F) << 8) | s[i + 3];
        if (prog == 0) continue; // network PID
        TsService& sv = services_[prog];
        sv.id = prog;
        sv.pmtPid = pid;
        pmtToService_[pid] = prog;
    }
}

static const char* streamKind(int st, std::string& codec, bool& isVideo, bool& isAudio) {
    isVideo = isAudio = false;
    switch (st) {
    case 0x01: codec = "MPEG-1 video"; isVideo = true; break;
    case 0x02: codec = "MPEG-2"; isVideo = true; break;
    case 0x03: codec = "MPEG-1 audio"; isAudio = true; break;
    case 0x04: codec = "MPEG audio"; isAudio = true; break;
    case 0x0F: codec = "AAC"; isAudio = true; break;
    case 0x11: codec = "HE-AAC"; isAudio = true; break;
    case 0x1B: codec = "H.264"; isVideo = true; break;
    case 0x24: codec = "HEVC"; isVideo = true; break;
    case 0x81: codec = "AC-3"; isAudio = true; break;
    case 0x87: codec = "E-AC-3"; isAudio = true; break;
    // the Chinese AVS family (GB/T 17975.1, DTMB in China): AVS1-P2 and AVS+, AVS2, AVS3 (docs/CODECS.md)
    case 0x42: codec = "AVS"; isVideo = true; break;
    case 0xD2: codec = "AVS2"; isVideo = true; break;
    case 0xD4: codec = "AVS3"; isVideo = true; break;
    default: break;
    }
    return isVideo ? "video" : isAudio ? "audio" : "data";
}

void TsDemux::parsePmt(int pid, const uint8_t* s, int len) {
    auto it = pmtToService_.find(pid);
    if (it == pmtToService_.end()) return;
    TsService& sv = services_[it->second];
    int prog = (s[3] << 8) | s[4];
    if (prog != sv.id) return;
    sv.pcrPid = ((s[8] & 0x1F) << 8) | s[9];
    int pil = ((s[10] & 0x0F) << 8) | s[11];
    bool ca = false;
    for (int i = 12; i + 2 <= 12 + pil && i + 2 <= len; ) { int tag = s[i], l = s[i + 1]; if (tag == 0x09) ca = true; i += 2 + l; }
    std::vector<TsStream> streams;
    int i = 12 + pil;
    while (i + 5 <= len - 4) {
        TsStream st;
        st.streamType = s[i];
        st.pid = ((s[i + 1] & 0x1F) << 8) | s[i + 2];
        int esl = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
        bool isV, isA;
        st.kind = streamKind(st.streamType, st.codec, isV, isA);
        for (int j = i + 5; j + 2 <= i + 5 + esl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (j + 2 + l > len) break;
            const uint8_t* d = s + j + 2;
            switch (tag) {
            case 0x09: ca = true; break;
            case 0x05: if (l >= 4 && memcmp(d, "DRA1", 4) == 0) { st.codec = "DRA"; st.kind = "audio"; } break;   // registration: DRA sound (GB/T 22726)
            case 0x0A: if (l >= 3) st.lang.assign((const char*)d, 3); break;
            case 0x6A: st.codec = "AC-3"; st.kind = "audio"; break;
            case 0x7A: st.codec = "E-AC-3"; st.kind = "audio"; break;
            case 0x7B: st.codec = "DTS"; st.kind = "audio"; break;
            case 0x7C: st.codec = "AAC"; st.kind = "audio"; break;
            case 0x59: st.codec = "DVB subtitles"; st.kind = "subtitles"; if (l >= 3) st.lang.assign((const char*)d, 3); break;
            case 0x56: st.codec = "Teletext"; st.kind = "teletext"; if (l >= 3) st.lang.assign((const char*)d, 3); break;
            case 0x46: st.codec = "VBI teletext"; st.kind = "teletext"; break;
            default: break;
            }
            j += 2 + l;
        }
        if (st.codec.empty()) { char b[24]; snprintf(b, sizeof b, "type 0x%02X", st.streamType); st.codec = b; }
        streams.push_back(st);
        i += 5 + esl;
    }
    sv.streams = streams;
    sv.havePmt = true;
    sv.caFlag = sv.caFlag || ca;
}

void TsDemux::parseSdt(const uint8_t* s, int len) {
    tsid_ = (s[3] << 8) | s[4];
    onid_ = (s[8] << 8) | s[9];
    int i = 11;
    while (i + 5 <= len - 4) {
        int sid = (s[i] << 8) | s[i + 1];
        bool freeCa = s[i + 3] & 0x10;
        int dl = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
        TsService& sv = services_[sid];
        sv.id = sid;
        sv.haveSdt = true;
        sv.caFlag = sv.caFlag || freeCa;
        for (int j = i + 5; j + 2 <= i + 5 + dl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (j + 2 + l > len) break;
            if (tag == 0x48 && l >= 3) {
                const uint8_t* d = s + j + 2;
                sv.type = d[0];
                int pl = d[1];
                if (2 + pl + 1 <= l) {
                    sv.provider = dvbText(d + 2, pl, gbText_);
                    int nl = d[2 + pl];
                    if (3 + pl + nl <= l) sv.name = dvbText(d + 3 + pl, nl, gbText_);
                }
            }
            j += 2 + l;
        }
        i += 5 + dl;
    }
}

void TsDemux::parseNit(const uint8_t* s, int len) {
    int ndl = ((s[8] & 0x0F) << 8) | s[9];
    for (int j = 10; j + 2 <= 10 + ndl && j + 2 <= len; ) {
        int tag = s[j], l = s[j + 1];
        if (tag == 0x40) network_ = dvbText(s + j + 2, l, gbText_);
        j += 2 + l;
    }
    // transport stream loop: logical channel numbers (descriptor 0x83 in private data, e.g. EACEM / UK)
    int i = 10 + ndl;
    if (i + 2 > len) return;
    int tsl = ((s[i] & 0x0F) << 8) | s[i + 1];
    i += 2;
    int end = std::min(len - 4, i + tsl);
    while (i + 6 <= end) {
        int dl = ((s[i + 4] & 0x0F) << 8) | s[i + 5];
        for (int j = i + 6; j + 2 <= i + 6 + dl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (tag == 0x83) for (int k = 0; k + 4 <= l; k += 4) {
                int sid = (s[j + 2 + k] << 8) | s[j + 3 + k];
                int lcn = ((s[j + 4 + k] & 0x03) << 8) | s[j + 5 + k];
                services_[sid].id = sid;
                services_[sid].lcn = lcn;
            }
            j += 2 + l;
        }
        i += 6 + dl;
    }
}

void TsDemux::parseEit(const uint8_t* s, int len) {
    const int tid = s[0];
    const int sid = (s[3] << 8) | s[4];
    const int secNum = s[6];
    if (len < 18) return;
    // legacy: present / following event names for the service list
    if (tid == 0x4E && secNum <= 1) {
        int i = 14;
        std::string nameOut;
        if (i + 12 <= len - 4) {
            int dl = ((s[i + 10] & 0x0F) << 8) | s[i + 11];
            for (int j = i + 12; j + 2 <= i + 12 + dl && j + 2 <= len; ) {
                int tag = s[j], l = s[j + 1];
                if (tag == 0x4D && l >= 5) {
                    const uint8_t* d = s + j + 2;
                    int nl = d[3];
                    if (4 + nl <= l) nameOut = dvbText(d + 4, nl, gbText_);
                }
                j += 2 + l;
            }
        }
        TsService& sv = services_[sid];
        sv.id = sid;
        (secNum == 0 ? sv.now : sv.next) = nameOut;
    }
    // the guide: every event in the section
    auto bcd = [](int v) { return (v >> 4) * 10 + (v & 15); };
    int i = 14;
    const int end = len - 4;
    auto& evs = epg_[sid];
    while (i + 12 <= end) {
        EpgEvent ev;
        ev.eventId = (s[i] << 8) | s[i + 1];
        const int mjd = (s[i + 2] << 8) | s[i + 3];
        const bool undefined = s[i + 2] == 0xFF && s[i + 3] == 0xFF && s[i + 4] == 0xFF;
        ev.start = undefined ? 0 : (int64_t)(mjd - 40587) * 86400 + bcd(s[i + 4]) * 3600 + bcd(s[i + 5]) * 60 + bcd(s[i + 6]);
        ev.duration = bcd(s[i + 7]) * 3600 + bcd(s[i + 8]) * 60 + bcd(s[i + 9]);
        ev.running = s[i + 10] >> 5;
        const int dl = ((s[i + 10] & 0x0F) << 8) | s[i + 11];
        const int dend = std::min(end, i + 12 + dl);
        std::map<int, std::string> ext;
        for (int j = i + 12; j + 2 <= dend; ) {
            const int tag = s[j], l = s[j + 1];
            if (j + 2 + l > dend) break;
            const uint8_t* d = s + j + 2;
            if (tag == 0x4D && l >= 5) {              // short event descriptor
                ev.lang.assign((const char*)d, 3);
                const int nl = d[3];
                if (4 + nl < l) {
                    ev.title = dvbText(d + 4, nl, gbText_);
                    const int tl = d[4 + nl];
                    if (5 + nl + tl <= l) ev.text = dvbText(d + 5 + nl, tl, gbText_);
                }
            } else if (tag == 0x4E && l >= 6) {       // extended event descriptor (several pieces, in order)
                const int num = d[0] >> 4;
                const int itemsLen = d[4];
                if (5 + itemsLen < l) {
                    const int tl = d[5 + itemsLen];
                    if (6 + itemsLen + tl <= l) ext[num] = dvbText(d + 6 + itemsLen, tl, gbText_);
                }
            } else if (tag == 0x54 && l >= 2) {       // content descriptor
                ev.genre = d[0] >> 4;
            }
            j += 2 + l;
        }
        for (auto& kv : ext) ev.extended += kv.second;
        i = dend;
        if (ev.start == 0 || ev.duration <= 0) continue;
        auto it = evs.find(ev.eventId);
        if (it != evs.end() && ev.extended.empty()) ev.extended = it->second.extended;
        evs[ev.eventId] = ev;
    }
    // keep the table bounded: drop the oldest events
    while (evs.size() > 400) {
        auto oldest = evs.begin();
        for (auto it = evs.begin(); it != evs.end(); ++it) if (it->second.start < oldest->second.start) oldest = it;
        evs.erase(oldest);
    }
}

std::map<int, std::vector<EpgEvent>> TsDemux::epg() const {
    std::map<int, std::vector<EpgEvent>> out;
    for (auto& kv : epg_) {
        auto& v = out[kv.first];
        for (auto& e : kv.second) v.push_back(e.second);
        std::sort(v.begin(), v.end(), [](const EpgEvent& a, const EpgEvent& b) { return a.start < b.start; });
    }
    return out;
}

void TsDemux::parseTdt(const uint8_t* s, int len) {
    if (len < 8) return;
    int mjd = (s[3] << 8) | s[4];
    auto bcd = [](int v) { return (v >> 4) * 10 + (v & 15); };
    int hh = bcd(s[5]), mm = bcd(s[6]), ss = bcd(s[7]);
    int y = (int)((mjd - 15078.2) / 365.25);
    int m = (int)((mjd - 14956.1 - (int)(y * 365.25)) / 30.6001);
    int d = mjd - 14956 - (int)(y * 365.25) - (int)(m * 30.6001);
    int k = (m == 14 || m == 15) ? 1 : 0;
    y += k; m = m - 1 - k * 12;
    char b[40]; snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d UTC", 1900 + y, m, d, hh, mm, ss);
    utc_ = b;
    tdtUnix_ = (int64_t)(mjd - 40587) * 86400 + hh * 3600 + mm * 60 + ss;
    tdtAt_ = streamClock_;
}

void TsDemux::advance(double dt) {
    if (dt <= 0) return;
    streamClock_ += dt;
    const double a = 0.35;
    for (auto& kv : pids_) {
        double k = kv.second.bytesWin * 8.0 / dt / 1000.0;
        kv.second.kbps = kv.second.kbps * (1 - a) + k * a;
        kv.second.bytesWin = 0;
    }
    muxKbps_ = muxKbps_ * (1 - a) + (totalWin_ * 188 * 8.0 / dt / 1000.0) * a;
    nullKbps_ = nullKbps_ * (1 - a) + (nullWin_ * 188 * 8.0 / dt / 1000.0) * a;
    totalWin_ = nullWin_ = 0;
}

TsSnapshot TsDemux::snapshot() const {
    TsSnapshot sn;
    sn.networkName = network_; sn.utc = utc_; sn.onid = onid_; sn.tsid = tsid_;
    sn.totalPackets = total_; sn.nullPackets = nulls_; sn.teiPackets = tei_; sn.ccErrors = ccErr_;
    sn.muxKbps = muxKbps_; sn.nullKbps = nullKbps_;
    sn.utcNow = tdtUnix_ ? tdtUnix_ + (int64_t)(streamClock_ - tdtAt_) : 0;
    std::map<int, std::string> label;
    for (auto& kv : services_) {
        TsService sv = kv.second;
        // scrambled: what the streams' packets say, not the CA signalling (a free channel can carry CA descriptors or free_CA_mode)
        sv.scrambled = false;
        for (auto& st : sv.streams) {
            auto it = pids_.find(st.pid);
            if (it != pids_.end()) { st.kbps = it->second.kbps; st.scrambled = it->second.scrambled; }
            sv.scrambled = sv.scrambled || st.scrambled;
            label[st.pid] = (sv.name.empty() ? "service " + std::to_string(sv.id) : sv.name) + " " + st.kind + " (" + st.codec + ")";
        }
        label[sv.pmtPid] = "PMT " + (sv.name.empty() ? std::to_string(sv.id) : sv.name);
        if (sv.pmtPid || sv.haveSdt || sv.havePmt) sn.services.push_back(sv);
    }
    std::sort(sn.services.begin(), sn.services.end(), [](const TsService& a, const TsService& b) {
        if (a.lcn != b.lcn) return (a.lcn ? a.lcn : 9999) < (b.lcn ? b.lcn : 9999);
        return a.id < b.id;
    });
    label[0] = "PAT"; label[1] = "CAT"; label[0x10] = "NIT"; label[0x11] = "SDT/BAT"; label[0x12] = "EIT"; label[0x14] = "TDT/TOT"; label[0x1FFF] = "null";
    label[0x13] = "RST"; label[0x1F] = "DIT"; label[0x1E] = "ST";
    for (auto& kv : pids_) {
        TsPid p;
        p.pid = kv.first; p.packets = kv.second.packets; p.ccErrors = kv.second.ccErrors; p.tei = kv.second.tei;
        p.kbps = kv.second.kbps; p.scrambled = kv.second.scrambled;
        auto it = label.find(kv.first);
        p.label = it != label.end() ? it->second : "";
        sn.pids.push_back(p);
    }
    return sn;
}

} // namespace dect2
