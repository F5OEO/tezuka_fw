// MPEG transport-stream analysis: PSI/SI parsing (PAT, PMT, SDT, NIT, EIT now/next, TDT), per-PID statistics.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dect2 {

struct TsStream {
    int pid = 0, streamType = 0;
    std::string codec;     // "H.264", "HEVC", "AAC", "AC-3", ...
    std::string kind;      // "video", "audio", "subtitles", "teletext", "data"
    std::string lang;
    double kbps = 0;
    bool scrambled = false;
};

struct TsService {
    int id = 0;
    std::string name, provider;
    int type = 0;            // DVB service type
    int pmtPid = 0, pcrPid = 0;
    bool caFlag = false;     // free_CA_mode or CA descriptors present (signalling only: many free channels carry them, e.g. on DTMB in China)
    bool scrambled = false;  // its elementary streams arrive scrambled (transport_scrambling_control of their payload packets): what to show
    bool haveSdt = false, havePmt = false;
    std::vector<TsStream> streams;
    std::string now, next;   // EIT present / following event names
    int lcn = 0;
    const char* typeName() const;
};

// One programme from the Event Information Table (EN 300 468): what is on, when, and what it is about.
struct EpgEvent {
    int eventId = 0;
    int64_t start = 0;        // seconds since 1970, UTC
    int duration = 0;         // seconds
    int running = 0;          // running_status (4 = running)
    int genre = -1;           // first content nibble (0x1 movie, 0x2 news, 0x4 sport, ...), -1 unknown
    std::string title, text, extended, lang;
    int64_t end() const { return start + duration; }
};

struct TsPid {
    int pid = 0;
    uint64_t packets = 0, ccErrors = 0, tei = 0;
    double kbps = 0;
    bool scrambled = false;
    std::string label;
};

struct TsSnapshot {
    std::vector<TsService> services;
    std::vector<TsPid> pids;
    std::string networkName, utc;
    int onid = 0, tsid = 0;
    uint64_t totalPackets = 0, nullPackets = 0, teiPackets = 0, ccErrors = 0;
    double muxKbps = 0, nullKbps = 0;
    int64_t utcNow = 0;      // current UTC from the TDT (advanced with the stream), 0 until one has been received
};

class TsDemux {
public:
    void feed(const uint8_t* pkt188);
    void markLoss();                 // data was lost before the next packet (do not count continuity errors)
    void advance(double dtSeconds);  // stream time passed; updates bit rates
    TsSnapshot snapshot() const;
    // Programme guide per service id, sorted by start time (now/next plus the schedule when the broadcaster sends it).
    std::map<int, std::vector<EpgEvent>> epg() const;
    void reset();
    void setGbDefault(bool on) { gbText_ = on; }   // DTMB: unprefixed SI text may be GB 2312

private:
    bool gbText_ = false;
    struct SectionBuf { std::vector<uint8_t> data; bool active = false; };
    struct PidState { uint64_t packets = 0, bytesWin = 0, ccErrors = 0, tei = 0; int lastCc = -1; double kbps = 0; bool scrambled = false; bool resync = true; };
    void section(int pid, const uint8_t* s, int len);
    void parsePat(const uint8_t* s, int len);
    void parsePmt(int pid, const uint8_t* s, int len);
    void parseSdt(const uint8_t* s, int len);
    void parseNit(const uint8_t* s, int len);
    void parseTvct(const uint8_t* s, int len);   // ATSC PSIP terrestrial/cable virtual channel table: names and channel numbers
    void parseEit(const uint8_t* s, int len);
    void parseTdt(const uint8_t* s, int len);
    std::map<int, PidState> pids_;
    std::map<int, SectionBuf> sec_;
    std::map<int, TsService> services_;   // by service id
    std::map<int, int> pmtToService_;
    std::map<int, std::map<int, EpgEvent>> epg_;   // service id -> event id -> event
    std::map<int, std::map<int, std::string>> epgExt_; // extended text pieces by event
    int64_t tdtUnix_ = 0;
    double streamClock_ = 0, tdtAt_ = 0;
    std::string network_, utc_;
    int onid_ = 0, tsid_ = 0;
    uint64_t total_ = 0, nulls_ = 0, tei_ = 0, ccErr_ = 0, nullWin_ = 0, totalWin_ = 0;
    double muxKbps_ = 0, nullKbps_ = 0;
};

// Selects one service from a multiplex: passes its PMT/ES/PCR PIDs and SI, and replaces the PAT with a single-program one.
class ServiceFilter {
public:
    void select(int serviceId) { sid_ = serviceId; pass_.clear(); patSid_ = -1; }
    int selected() const { return sid_; }
    // Returns true if the packet should be forwarded; `out` receives the (possibly rewritten) packet.
    bool process(const uint8_t* in, const TsSnapshot* snap, uint8_t* out);

private:
    int sid_ = -1;
    std::vector<int> pass_;
    int patCc_ = 0, patSid_ = -1, patPmt_ = 0, patTsid_ = 0;
};

uint32_t mpegCrc32(const uint8_t* p, int n);
std::string dvbText(const uint8_t* p, int n, bool gbDefault = false); // gbDefault: unprefixed GB 2312 pairs are decoded as GB 2312 (DTMB) // DVB text with character-table prefix -> UTF-8

} // namespace dect2
