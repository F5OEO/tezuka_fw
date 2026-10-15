// DVB-S and DVB-S2 transmitter chains, see dvbs_tx.h.
#include "dect2/dvbs_tx.h"
#include "dect2/dvbt.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

namespace dect2 {
namespace dvbs {

namespace {

void nullPacket(uint8_t* p) { memset(p, 0xFF, 188); p[0] = 0x47; p[1] = 0x1F; p[2] = 0xFF; p[3] = 0x10; }
bool isNull(const uint8_t* p) { return p[0] == 0x47 && ((p[1] & 0x1F) << 8 | p[2]) == 0x1FFF; }

// ============================================================================ DVB-S
class TxS1 : public DvbsTxSource {
public:
    TxS1(const DvbsTxConfig& c, std::function<void(uint8_t*)> ts) : rate_(c.rate), ts_(std::move(ts)), enc_(c.rate), inter_(false) {}
    void generate(cf32* out, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            while (pos_ >= syms_.size()) refill();
            out[i] = syms_[pos_++];
        }
    }
    uint64_t packetsSent() const override { return packets_; }

private:
    // one group of eight packets: energy dispersal (the PRBS restarts every eight packets), Reed-Solomon, convolutional interleaver, inner code
    void refill() {
        uint8_t ts[8 * 188], sc[8 * 188], rs[8 * 204], il[8 * 204];
        for (int p = 0; p < 8; p++) { if (ts_) ts_(ts + p * 188); else nullPacket(ts + p * 188); ts[p * 188] = 0x47; packets_++; }
        dvbt::scramble(ts, 8, sc);
        for (int p = 0; p < 8; p++) dvbt::rsEncode(sc + p * 188, rs + p * 204);
        inter_.process(rs, il, sizeof rs);
        std::vector<uint8_t> bits(sizeof il * 8), coded;
        for (size_t i = 0; i < sizeof il; i++) for (int b = 0; b < 8; b++) bits[i * 8 + b] = (il[i] >> (7 - b)) & 1;
        enc_.encode(bits, coded);
        // the serial stream alternates I and Q (table 2): a bit left over from the previous group completes the first pair
        std::vector<uint8_t> all;
        if (haveSpare_) all.push_back(spare_);
        all.insert(all.end(), coded.begin(), coded.end());
        haveSpare_ = all.size() & 1;
        if (haveSpare_) { spare_ = all.back(); all.pop_back(); }
        syms_.resize(all.size() / 2);
        pos_ = 0;
        for (size_t i = 0; i < syms_.size(); i++) syms_[i] = cf32((1 - 2 * all[2 * i]) * 0.70710678f, (1 - 2 * all[2 * i + 1]) * 0.70710678f);
    }
    int rate_;
    std::function<void(uint8_t*)> ts_;
    dvbt::InnerEncoder enc_;
    dvbt::ConvInterleaver inter_;
    std::vector<cf32> syms_;
    size_t pos_ = 0;
    bool haveSpare_ = false;
    uint8_t spare_ = 0;
    uint64_t packets_ = 0;
};

// ============================================================================ DVB-S2
const std::vector<VcmStep> kVcm = {
    {kQpsk, 3, false, false}, {k8psk, 5, false, true}, {kQpsk, 6, true, false}, {k16apsk, 6, false, false}, {kQpsk, 5, false, true},
    {k32apsk, 8, true, true}, {k8psk, 4, false, false},
};

class TxS2 : public DvbsTxSource {
public:
    TxS2(const DvbsTxConfig& c, std::function<void(uint8_t*)> ts) : c_(c), ts_(std::move(ts)) {}
    void generate(cf32* out, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            while (pos_ >= frame_.size()) nextFrame();
            out[i] = frame_[pos_++];
        }
    }
    uint64_t packetsSent() const override { return packets_; }

private:
    // the next user packet as it is transmitted: the sync byte is replaced by the CRC-8 of the previous packet (clause 5.1.4)
    void nextUp(std::vector<uint8_t>& up) {
        uint8_t p[188];
        for (;;) {
            if (ts_) ts_(p); else nullPacket(p);
            p[0] = 0x47;
            packets_++;
            if (c_.npd && isNull(p) && dnp_ < 255) { dnp_++; continue; }
            break;
        }
        up.assign(p, p + 188);
        if (c_.npd) { up.push_back((uint8_t)dnp_); dnp_ = 0; }
        up[0] = prevCrc_;
        prevCrc_ = s2Crc8(up.data() + 1, (int)up.size() - 1);
    }

    void nextFrame() {
        int mod = c_.mod, rate = c_.rate;
        bool sh = c_.shortFrame, pil = c_.pilots;
        if (c_.vcm) { const VcmStep& v = kVcm[vcmPos_++ % kVcm.size()]; mod = v.mod; rate = v.rate; sh = v.shortFrame; pil = v.pilots; }
        frame_.clear();
        pos_ = 0;
        if (c_.dummyEvery > 0 && ++sinceDummy_ > c_.dummyEvery) {
            sinceDummy_ = 0;
            frame_.resize(90 + 36 * 90);
            s2PlHeader(0, false, false, frame_.data());
            const auto& rn = s2ScramblingRn(c_.plScramble);
            for (int i = 0; i < 36 * 90; i++) frame_[90 + i] = s2RotateByR(cf32(0.70710678f, 0.70710678f), rn[i]);
            return;
        }
        const S2Dims d = s2Dims(mod, rate, sh);
        const int dfl = d.kbch - 80, upBits = c_.npd ? 1512 : 1504;
        // data field: the UP stream, cut at DFL bits. SYNCD points at the first packet that starts inside the field.
        std::vector<uint8_t> bb(d.kbch, 0);
        S2BbHeader h;
        h.ro = c_.rollOff > 0.3 ? 0 : c_.rollOff > 0.22 ? 1 : 2;
        h.ccm = !c_.vcm; h.npd = c_.npd; h.upl = upBits; h.dfl = dfl;
        h.syncd = upBitPos_ == 0 ? 0 : upBits - upBitPos_;
        if (h.syncd >= dfl) h.syncd = 65535;
        s2BuildBbHeader(h, bb.data());
        for (int i = 0; i < dfl; i++) {
            if (cur_.empty()) { nextUp(cur_); upBitPos_ = 0; }
            bb[80 + i] = (cur_[upBitPos_ >> 3] >> (7 - (upBitPos_ & 7))) & 1;
            if (++upBitPos_ == (int)cur_.size() * 8) { upBitPos_ = 0; cur_.clear(); }
        }
        s2BbScramble(bb.data(), d.kbch);
        std::vector<uint8_t> fec, il;
        s2EncodeFec(bb, rate, sh, fec);
        s2BitInterleave(fec, mod, rate, il);
        std::vector<cf32> sym(d.xfecSymbols);
        s2MapBits(il.data(), (int)il.size(), mod, rate, sym.data());
        // PLFRAME: header, then the slots with a pilot block after every 16 slots (but not at the end), all scrambled except the header
        const int total = s2FrameSymbols(d, pil);
        frame_.resize(total);
        s2PlHeader(s2Modcod(mod, rate), sh, pil, frame_.data());
        const auto& rn = s2ScramblingRn(c_.plScramble);
        int o = 90, si = 0, i = 0;
        for (int slot = 0; slot < d.slots; slot++) {
            for (int k = 0; k < 90; k++, si++) { frame_[o] = s2RotateByR(sym[si], rn[i]); o++; i++; }
            if (pil && (slot + 1) % 16 == 0 && slot + 1 < d.slots)
                for (int k = 0; k < 36; k++) { frame_[o] = s2RotateByR(cf32(0.70710678f, 0.70710678f), rn[i]); o++; i++; }
        }
    }

    DvbsTxConfig c_;
    std::function<void(uint8_t*)> ts_;
    std::vector<cf32> frame_;
    size_t pos_ = 0;
    std::vector<uint8_t> cur_;       // the packet being sent
    int upBitPos_ = 0;
    uint8_t prevCrc_ = 0;
    int dnp_ = 0, vcmPos_ = 0, sinceDummy_ = 0;
    uint64_t packets_ = 0;
};

} // namespace

const std::vector<VcmStep>& dvbsVcmCycle() { return kVcm; }

std::unique_ptr<DvbsTxSource> makeDvbsTx(const DvbsTxConfig& cfg, std::function<void(uint8_t*)> ts) {
    if (cfg.standard == 1) return std::make_unique<TxS1>(cfg, std::move(ts));
    return std::make_unique<TxS2>(cfg, std::move(ts));
}

double dvbsNetBitrate(const DvbsTxConfig& c) {
    if (c.standard == 1) {
        static const double r[5] = {1.0 / 2, 2.0 / 3, 3.0 / 4, 5.0 / 6, 7.0 / 8};
        return c.symbolRate * 2 * r[std::max(0, std::min(4, c.rate))] * 188.0 / 204.0;
    }
    auto one = [&](int mod, int rate, bool sh, bool pil) {
        const S2Dims d = s2Dims(mod, rate, sh);
        if (!d.ok) return 0.0;
        // user packets are 1504 bits (1512 with NPD); the data field holds kbch - 80 bits of them
        return c.symbolRate * (double)(d.kbch - 80) / (double)s2FrameSymbols(d, pil);
    };
    if (!c.vcm) return one(c.mod, c.rate, c.shortFrame, c.pilots);
    double bits = 0, syms = 0;
    for (const VcmStep& v : kVcm) {
        const S2Dims d = s2Dims(v.mod, v.rate, v.shortFrame);
        bits += d.kbch - 80;
        syms += s2FrameSymbols(d, v.pilots);
    }
    return c.symbolRate * bits / syms;
}

} // namespace dvbs
} // namespace dect2
