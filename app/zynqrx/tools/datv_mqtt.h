// MQTT control and status in the longmynd scheme (https://github.com/F5OEO/longmynd, mymqtt.c): commands on cmd/zynqrx/..., status on dt/zynqrx/...,
// so the scripts and dashboards built for that receiver work with zynqrx. Built only with -DZYNQRX_MQTT (Makefile: MQTT=1, the default for the board).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include "dect2/dvbs_tel.h"

// What the status messages are made from (filled by the receive loop once a second)
struct DatvStatus {
    bool running = false;                // the receiver process is started (cmd/zynqrx/process start), not stopped
    int state = 0;                       // 0 Init, 1 Hunting, 2 found header, 3 demod_s, 4 demod_s2 (longmynd's rx_state)
    long freqKHz = 0, srKS = 0;          // what was asked for (the set/ echoes)
    long swport = 0;
    std::string tsip;
    int lnbSupply = 0, lnbHorizontal = 0;
    dect2::DvbsTelemetry tel;            // the receiver's report
    int nullPercent = 0;                 // null packets in the last interval
    int gainDb = -1;                     // radio gain, -1 = not known
    bool haveTel = false;
    uint64_t bchBadSince = 0;            // BCH-uncorrectable frames since the last report
    std::string serviceName, providerName;
    std::vector<std::pair<int, int>> es; // elementary stream (pid, stream type)
    int matype1 = -1, matype2 = -1;      // baseband header MATYPE bytes of the last frame, -1 not known
    std::vector<std::pair<std::string, std::string>> settings;   // receiver settings echoed on dt/zynqrx/set/<name> (gain, bw, std, roll, mods, ...)
};

#ifdef ZYNQRX_MQTT
#include <mosquitto.h>

class MqttLink {
public:
    // called on libmosquitto's thread with the topic below cmd/zynqrx/ and the payload (a copy, NUL-terminated, trailing blanks removed)
    using Handler = std::function<void(const std::string& key, const std::string& value)>;
    MqttLink() = default;
    MqttLink(const MqttLink&) = delete;
    MqttLink& operator=(const MqttLink&) = delete;
    ~MqttLink() {
        if (mosq_) { mosquitto_disconnect(mosq_); mosquitto_loop_stop(mosq_, true); mosquitto_destroy(mosq_); mosquitto_lib_cleanup(); }
    }
    // never fails because the broker is away: the connection is made in the background and retried
    bool start(const std::string& host, int port, Handler h) {
        handler_ = std::move(h);
        mosquitto_lib_init();
        mosq_ = mosquitto_new(nullptr, true, this);
        if (!mosq_) { fprintf(stderr, "mqtt: out of memory\n"); return false; }
        mosquitto_reconnect_delay_set(mosq_, 1, 10, false);
        mosquitto_connect_callback_set(mosq_, [](mosquitto* m, void*, int rc) {
            if (rc == 0 && mosquitto_subscribe(m, nullptr, "cmd/zynqrx/#", 1) != MOSQ_ERR_SUCCESS) fprintf(stderr, "mqtt: subscribe failed\n");
            else if (rc == 0) fprintf(stderr, "mqtt: connected, listening on cmd/zynqrx/#\n");
        });
        mosquitto_message_callback_set(mosq_, [](mosquitto*, void* self, const mosquitto_message* msg) {
            static const std::string pre = "cmd/zynqrx/";
            if (!msg->topic || !msg->payload) return;
            const std::string topic = msg->topic;
            if (topic.compare(0, pre.size(), pre) != 0) return;
            std::string v((const char*)msg->payload, (size_t)msg->payloadlen);   // not NUL-terminated on the wire
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
            auto* l = (MqttLink*)self;
            if (l->handler_) l->handler_(topic.substr(pre.size()), v);
        });
        const int rc = mosquitto_connect_async(mosq_, host.c_str(), port, 60);
        if (rc != MOSQ_ERR_SUCCESS) fprintf(stderr, "mqtt: cannot connect to %s:%d yet (%s), retrying\n", host.c_str(), port, mosquitto_strerror(rc));
        return mosquitto_loop_start(mosq_) == MOSQ_ERR_SUCCESS;
    }
    void publish(const std::string& topic, const std::string& value) {
        if (mosq_) mosquitto_publish(mosq_, nullptr, topic.c_str(), (int)value.size(), value.data(), 2, false);   // QoS 2, not retained, as longmynd does
    }
private:
    mosquitto* mosq_ = nullptr;
    Handler handler_;
};

// longmynd's tables (mymqtt.c): FEC name by DVB-S2 MODCOD number, and the MER (dB * 10) at which each MODCOD starts to work, for margin_db
inline const char* datvFecName(int modcod) {
    static const char* t[] = {"none", "1/4", "1/3", "2/5", "1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "8/9", "9/10", "3/5", "2/3", "3/4", "5/6", "8/9", "9/10",
                              "2/3", "3/4", "4/5", "5/6", "8/9", "9/10", "3/4", "4/5", "5/6", "8/9", "9/10"};
    return modcod >= 0 && modcod <= 28 ? t[modcod] : "none";
}
inline const char* datvModulationName(int modcod) {
    if (modcod <= 0) return "none";
    return modcod < 12 ? "QPSK" : modcod <= 17 ? "8PSK" : modcod <= 23 ? "16APSK" : modcod <= 28 ? "32APSK" : "none";
}
inline int datvTheoreticMer(int modcod) {
    static const int t[] = {0, -24, -12, 0, 10, 22, 32, 40, 46, 52, 62, 65, 55, 66, 79, 94, 106, 110, 90, 102, 110, 116, 129, 131, 126, 136, 143, 157, 161};
    return modcod >= 0 && modcod <= 28 ? t[modcod] : 0;
}

// One round of dt/zynqrx/... messages, in the order and with the units of longmynd's status output
inline void datvPublish(MqttLink& mq, const DatvStatus& s) {
    auto pub = [&](const char* name, const std::string& v) { mq.publish(std::string("dt/zynqrx/") + name, v); };
    auto num = [&](const char* name, long long v) { pub(name, std::to_string(v)); };
    const auto& t = s.tel;
    static const char* states[] = {"Init", "Hunting", "found header", "demod_s", "demod_s2"};
    pub("process", s.running ? "start" : "stop");
    pub("rx_state", states[std::min(4, std::max(0, s.state))]);
    mq.publish("dt/zynqrx/set/sr", std::to_string(s.srKS));
    mq.publish("dt/zynqrx/set/frequency", std::to_string(s.freqKHz));
    mq.publish("dt/zynqrx/set/swport", std::to_string(s.swport));
    mq.publish("dt/zynqrx/set/tsip", s.tsip);
    for (const auto& kv : s.settings) mq.publish("dt/zynqrx/set/" + kv.first, kv.second);
    if (s.gainDb >= 0) {
        num("lna_gain", s.gainDb);
        num("agc2", (long long)(s.gainDb * 65535 / 73));          // AD9361 gain scale: 0 minimum, 65535 maximum
    }
    const bool locked = s.haveTel && s.state >= 3;
    // MER comes from the known symbols (header, pilots), so it is real as soon as the frame headers are found (state 2),
    // before the transport stream locks; longmynd reports it only when locked, here it is sent from "found header" on.
    // The error rates and the margin stay with the lock.
    const bool haveMer = s.haveTel && s.state >= 2;
    // puncture rate: n for n/(n+1), while the detected code is a DVB-S one
    {
        int n = 0;
        if (s.haveTel && t.standard == 1) { const std::string& r = t.codeRate; n = r == "1/2" ? 1 : r == "2/3" ? 2 : r == "3/4" ? 3 : r == "5/6" ? 5 : r == "7/8" ? 7 : 0; }
        num("puncrate", n);
    }
    num("carrier_frequency", s.freqKHz + (long long)std::llround(s.haveTel ? t.cfoHz / 1000.0 : 0.0));
    num("lnb_supply", s.lnbSupply);
    num("polarisation", s.lnbHorizontal);
    num("symbolrate", s.haveTel && t.symbolRate > 0 ? (long long)(((long long)std::llround(t.symbolRate) + 500) / 1000) : s.srKS);
    const long ber100 = s.haveTel && t.preFecBer >= 0 ? std::lround(t.preFecBer * 100.0 * 100.0) : 0;   // percentage * 100
    num("viterbi_error", locked && t.standard == 1 ? ber100 : 0);
    num("ber", locked ? ber100 : 0);
    const int mer10 = haveMer ? (int)std::lround(t.merDb * 10.0) : 0;
    {
        char b[32];
        snprintf(b, sizeof b, "%0.1f", mer10 / 10.0);
        pub("mer", b);
        const int modcod = s.haveTel && t.standard == 2 ? t.modcod : 0;
        num("margin_db", modcod != 0 && locked ? (mer10 - datvTheoreticMer(modcod)) / 10 : 0);      // integer arithmetic, as longmynd's
    }
    num("bch_uncorect", s.bchBadSince ? 1 : 0);
    num("bch_errors", locked && t.standard >= 2 ? 0 : 0);
    num("ldpc_errors", locked && t.standard >= 2 && t.preFecBer >= 0 ? std::lround(t.preFecBer * (t.frameSize == 2 ? 16200 : 64800)) : 0);
    pub("service_name", s.serviceName);
    pub("provider_name", s.providerName);
    num("ts_null", s.nullPercent);
    for (const auto& e : s.es) { num("es_pid", e.first); num("es_type", e.second); }
    if (s.haveTel && t.standard != 0) {
        pub("modulation", t.standard == 2 && t.modcod > 0 ? datvModulationName(t.modcod) : t.modulationName);
        pub("fec", t.standard == 2 && t.modcod > 0 ? datvFecName(t.modcod) : t.codeRate);
        num("short_frame", t.frameSize == 2 ? 1 : 0);
        num("pilots", t.pilots ? 1 : 0);
        if (t.rollOff > 0) {
            const double r = t.rollOff;
            pub("rolloff", r > 0.30 ? "0.35" : r > 0.225 ? "0.25" : r > 0.175 ? "0.20" : "0.15");
        }
    }
    if (s.matype1 >= 0) {
        static const char* kind[] = {"Generic packetized", "Generic continuous", "Generic packetized", "Transport"};
        pub("matype1", kind[(s.matype1 & 0xC0) >> 6]);
        char b[16];
        snprintf(b, sizeof b, "%x", s.matype2 >= 0 ? s.matype2 : 0);
        pub("matype2", b);
    }
}
#else
class MqttLink {
public:
    using Handler = std::function<void(const std::string&, const std::string&)>;
    bool start(const std::string&, int, Handler) { fprintf(stderr, "this build has no MQTT (build with MQTT=1)\n"); return false; }
    void publish(const std::string&, const std::string&) {}
};
inline void datvPublish(MqttLink&, const DatvStatus&) {}
#endif
