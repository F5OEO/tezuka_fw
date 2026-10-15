#include "dect2/dvbt_gen.h"
#include "dect2/fftutil.h"
#include "dect2/ts.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace dect2 {
namespace dvbt {

Generator::Generator(const Params& p, PacketSource src, unsigned seed)
    : p_(p), N_(::dect2::dvbt::fftN(p.mode)), G_(guardSamples(p.mode, p.guard)), K_(carriersK(p.mode)), src_(std::move(src)), rng_(seed), enc_(p.crHp), encLp_(p.crLp > 4 ? 0 : p.crLp) {
    if (!src_) {
        src_ = [this, n = 0u](uint8_t* pkt) mutable {
            pkt[0] = 0x47;
            const unsigned pid = 0x100 + (n++ % 4);
            pkt[1] = (uint8_t)(pid >> 8); pkt[2] = (uint8_t)pid; pkt[3] = 0x10 | (n & 15);
            for (int i = 4; i < 188; i++) pkt[i] = (uint8_t)rng_();
        };
    }
    tpsVal_.assign(tpsCarriers(p.mode).size(), 1.f);
    carriers_.assign(K_, cf32(0, 0));
    fft_ = new Fft(N_);
}

namespace {
std::vector<uint8_t> psiSection(std::vector<uint8_t> body) {
    const uint32_t crc = mpegCrc32(body.data(), (int)body.size());
    body.push_back(crc >> 24); body.push_back(crc >> 16); body.push_back(crc >> 8); body.push_back(crc);
    return body;
}
void putSection(uint8_t* pkt, int pid, const std::vector<uint8_t>& sec, int cc) {
    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47; pkt[1] = (uint8_t)(0x40 | (pid >> 8)); pkt[2] = (uint8_t)pid; pkt[3] = (uint8_t)(0x10 | (cc & 15)); pkt[4] = 0;
    memcpy(pkt + 5, sec.data(), std::min<size_t>(sec.size(), 183));
}
}

std::function<void(uint8_t*)> testTsSource(unsigned seed) {
    // PAT: program 1 -> PMT 0x100
    std::vector<uint8_t> pat = {0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xE1, 0x00};
    // PMT: PCR 0x101, one private-data stream on 0x101
    std::vector<uint8_t> pmt = {0x02, 0xB0, 0x12, 0x00, 0x01, 0xC1, 0x00, 0x00, 0xE1, 0x01, 0xF0, 0x00, 0x06, 0xE1, 0x01, 0xF0, 0x00};
    // SDT actual: service 1 "Test service" by "DecT2"
    const std::string prov = "OnAir", name = "Test service";
    std::vector<uint8_t> desc = {0x48, (uint8_t)(3 + prov.size() + name.size()), 0x01, (uint8_t)prov.size()};
    desc.insert(desc.end(), prov.begin(), prov.end());
    desc.push_back((uint8_t)name.size());
    desc.insert(desc.end(), name.begin(), name.end());
    std::vector<uint8_t> sdt = {0x42, 0xF0, 0x00, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x23, 0x10, 0xFF, 0x00, 0x01, 0xFC, (uint8_t)(0x80 | (desc.size() >> 8)), (uint8_t)(desc.size() & 0xFF)};
    sdt.insert(sdt.end(), desc.begin(), desc.end());
    pat[2] = (uint8_t)(pat.size() - 3 + 4);
    pmt[2] = (uint8_t)(pmt.size() - 3 + 4);
    sdt[2] = (uint8_t)(sdt.size() - 3 + 4);
    auto secs = std::make_shared<std::vector<std::vector<uint8_t>>>();
    secs->push_back(psiSection(pat)); secs->push_back(psiSection(pmt)); secs->push_back(psiSection(sdt));
    auto rng = std::make_shared<std::mt19937>(seed);
    auto n = std::make_shared<unsigned>(0);
    auto cc = std::make_shared<std::array<int, 4>>();
    cc->fill(0);
    return [=](uint8_t* pkt) {
        const unsigned i = (*n)++;
        const int pids[3] = {0x0000, 0x0100, 0x0011};
        if (i % 60 < 3) { const int k = (int)(i % 60); putSection(pkt, pids[k], (*secs)[k], (*cc)[k]++); return; }
        if (i % 7 == 0) { // some private data, so the stream is not all null packets
            pkt[0] = 0x47; pkt[1] = 0x01; pkt[2] = 0x01; pkt[3] = (uint8_t)(0x10 | ((*cc)[3]++ & 15));
            for (int j = 4; j < 188; j++) pkt[j] = (uint8_t)(*rng)();
            return;
        }
        memset(pkt, 0xFF, 188);
        pkt[0] = 0x47; pkt[1] = 0x1F; pkt[2] = 0xFF; pkt[3] = 0x10;
    };
}

void Generator::codeGroup(PacketSource& src, ConvInterleaver& ci, InnerEncoder& enc, std::vector<uint8_t>& q) {
    uint8_t ts[8 * 188], sc[8 * 188], rs[8 * 204], il[8 * 204];
    for (int i = 0; i < 8; i++) {
        if (src) src(ts + i * 188);
        else { memset(ts + i * 188, 0xFF, 188); ts[i * 188 + 1] = 0x1F; ts[i * 188 + 2] = 0xFF; ts[i * 188 + 3] = 0x10; }
        ts[i * 188] = 0x47;
    }
    scramble(ts, 8, sc);
    for (int i = 0; i < 8; i++) rsEncode(sc + i * 188, rs + i * 204);
    ci.process(rs, il, sizeof il);
    std::vector<uint8_t> bits(sizeof il * 8), coded;
    for (size_t i = 0; i < sizeof il; i++) for (int j = 0; j < 8; j++) bits[i * 8 + j] = (il[i] >> (7 - j)) & 1;
    enc.encode(bits, coded);
    q.insert(q.end(), coded.begin(), coded.end());
}

void Generator::refill() {
    const int m = bitsPerCell(p_.mod);
    if (p_.hier) {
        // two independent streams: HP (2 bits a cell, its own rate) and LP (the other v - 2 bits, at the LP rate)
        const size_t u = (size_t)m - 2;
        if (codedQ_.size() < 252) codeGroup(src_, ci_, enc_, codedQ_);
        if (codedLpQ_.size() < 126 * u) codeGroup(srcLp_, ciLp_, encLp_, codedLpQ_);
        const size_t blocks = std::min(codedQ_.size() / 252, codedLpQ_.size() / (126 * u));
        if (blocks) {
            std::vector<uint8_t> words;
            bitInterleaveHier(codedQ_, codedLpQ_, p_.mod, words);
            wordsQ_.insert(wordsQ_.end(), words.begin(), words.end());
            codedQ_.erase(codedQ_.begin(), codedQ_.begin() + (long)(blocks * 252));
            codedLpQ_.erase(codedLpQ_.begin(), codedLpQ_.begin() + (long)(blocks * 126 * u));
        }
        return;
    }
    codeGroup(src_, ci_, enc_, codedQ_);
    const size_t blk = (size_t)126 * m;
    const size_t use = codedQ_.size() / blk * blk;
    if (use) {
        std::vector<uint8_t> in(codedQ_.begin(), codedQ_.begin() + use), words;
        bitInterleave(in, p_.mod, words);
        wordsQ_.insert(wordsQ_.end(), words.begin(), words.end());
        codedQ_.erase(codedQ_.begin(), codedQ_.begin() + use);
    }
}

void Generator::nextSymbol(std::vector<cf32>& out) {
    const int Nd = dataCarriers(p_.mode);
    while ((int)wordsQ_.size() < Nd) refill();
    std::vector<uint8_t> words(wordsQ_.begin(), wordsQ_.begin() + Nd);
    wordsQ_.erase(wordsQ_.begin(), wordsQ_.begin() + Nd);
    std::vector<cf32> cells, inter(Nd);
    mapSymbol(words, p_.mod, p_.hier, cells);
    const auto& H = symbolPermutation(p_.mode);
    if (sym_ % 2) for (int q = 0; q < Nd; q++) inter[q] = cells[H[q]];
    else for (int q = 0; q < Nd; q++) inter[H[q]] = cells[q];

    std::vector<uint8_t> roles;
    carrierRoles(p_.mode, sym_, roles);
    const auto& tpsK = tpsCarriers(p_.mode);
    const auto tps = tpsBits(p_, frame_);
    const auto& w = prbsW();
    int di = 0;
    for (int k = 0; k < K_; k++) {
        if (roles[k] == 0) carriers_[k] = inter[di++];
        else if (roles[k] == 1 || roles[k] == 2) carriers_[k] = cf32(pilotValue(k), 0);
    }
    for (size_t i = 0; i < tpsK.size(); i++) {
        if (sym_ == 0) tpsVal_[i] = w[tpsK[i]] ? -1.f : 1.f;
        else if (tps[sym_]) tpsVal_[i] = -tpsVal_[i];
        carriers_[tpsK[i]] = cf32(tpsVal_[i], 0);
    }
    // IFFT: carrier k sits at frequency index k - (K-1)/2
    std::vector<cf32> X(N_, cf32(0, 0));
    const int kc = (K_ - 1) / 2;
    for (int k = 0; k < K_; k++) X[((k - kc) % N_ + N_) % N_] = carriers_[k];
    fft_->inverse(X.data());
    // unit mean power: the unnormalised inverse has power sum|c|^2, scale by 1/sqrt(sum |c|^2 / N ... ) -> use the nominal carrier count
    const float scale = 1.f / std::sqrt((float)K_ * 1.04f);
    out.resize((size_t)N_ + G_);
    for (int i = 0; i < N_; i++) out[G_ + i] = X[i] * scale;
    for (int i = 0; i < G_; i++) out[i] = out[N_ + i];
    if (++sym_ == symbolsPerFrame()) { sym_ = 0; frame_ = (frame_ + 1) % 4; }
}

} // namespace dvbt
} // namespace dect2
