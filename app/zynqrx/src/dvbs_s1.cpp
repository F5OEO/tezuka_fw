// DVB-S receive chain, see dvbs_s1.h.
#include "dvbs_s1.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>

namespace dect2 {
namespace dvbs {

namespace {
// puncturing patterns, as in dvbt.cpp (bit 0: X sent, bit 1: Y sent), steps per period, serial bits per period
const int kPat[5][7] = {{3, 0, 0, 0, 0, 0, 0}, {3, 2, 0, 0, 0, 0, 0}, {3, 2, 1, 0, 0, 0, 0}, {3, 2, 1, 2, 1, 0, 0}, {3, 2, 2, 2, 1, 2, 1}};
const int kSteps[5] = {1, 2, 3, 5, 7};
const int kBits[5] = {2, 3, 4, 6, 8};
const int kSymPer[5] = {1, 3, 2, 3, 4};      // symbols until the pattern lines up with the I/Q pairs again: lcm(bits, 2) / 2

// where a stream that starts at serial position s0 of the puncturing period begins a trellis step: values to drop and the step phase
void alignStart(int rate, int s0, int& drop, int& phase) {
    int cum = 0;
    const int k = kSteps[rate];
    drop = 0; phase = 0;
    for (int i = 0; i < k; i++) {
        const int n = __builtin_popcount((unsigned)kPat[rate][i]);
        if (s0 == cum) { drop = 0; phase = i; return; }
        if (n == 2 && s0 == cum + 1) { drop = 1; phase = (i + 1) % k; return; }
        cum += n;
    }
}
} // namespace

int s1PeriodSymbols(int rate) { return kSymPer[rate]; }

// ============================================================================ sync bytes
int s1FindSync(const std::vector<uint8_t>& bits, int& bestOff, int& bestPos, bool& inverted, int& blocks) {
    int bestScore = 0, bestPol = 0;
    bestOff = -1; bestPos = 0; blocks = 0;
    const size_t limit = bits.size();
    if (limit < 8 * 204 * 8) return 0;
    for (int off = 0; off < 8; off++) {
        const size_t nbytes = (limit - off) / 8;
        std::vector<uint8_t> by(nbytes);
        for (size_t i = 0; i < nbytes; i++) { unsigned c = 0; for (int j = 0; j < 8; j++) c = (c << 1) | bits[off + 8 * i + j]; by[i] = (uint8_t)c; }
        for (int pos = 0; pos < 204; pos++) {
            int score = 0;
            for (size_t i = pos; i < nbytes; i += 204) score += (by[i] == 0x47 || by[i] == 0xB8);
            if (score > bestScore) { bestScore = score; bestOff = off; bestPos = pos; }
        }
    }
    if (bestOff < 0) return 0;
    blocks = (int)((limit - bestOff - 8 * (size_t)bestPos) / 8 / 204);
    // polarity (the 180 degree ambiguity) from the pattern of the eight sync bytes of a group: one B8 and seven 47, or the other way round
    std::vector<uint8_t> s;
    for (size_t i = (size_t)bestPos; (i + 1) * 8 + bestOff <= limit; i += 204) {
        unsigned c = 0;
        for (int j = 0; j < 8; j++) c = (c << 1) | bits[bestOff + 8 * i + j];
        s.push_back((uint8_t)c);
    }
    int bestVote = -1;
    for (int pol = 0; pol < 2; pol++)
        for (int g0 = 0; g0 < 8; g0++) {
            int v = 0;
            for (size_t m = 0; m < s.size(); m++) v += (uint8_t)(s[m] ^ (pol ? 0xFF : 0)) == ((((int)m - g0 + 8) % 8) == 0 ? 0xB8 : 0x47);
            if (v > bestVote) { bestVote = v; bestPol = pol; }
        }
    inverted = bestPol != 0;
    return bestScore;
}

// ============================================================================ hypothesis search
S1Hypothesis s1Search(const cf32* y, size_t n, int rateHint) {
    S1Hypothesis best, none;
    if (n < 16000) return none;
    n = std::min<size_t>(n, 32768);
    std::vector<float> llr[4];
    double mean = 0;
    for (size_t i = 0; i < n; i++) mean += std::fabs(y[i].real()) + std::fabs(y[i].imag());
    mean /= (2.0 * n);
    const float scale = (float)std::min(40.0, 22.0 / std::max(1e-6, mean));
    for (int v = 0; v < 4; v++) {
        llr[v].resize(2 * n);
        for (size_t i = 0; i < n; i++) { const cf32 u = s1Variant(y[i], v); llr[v][2 * i] = u.real(); llr[v][2 * i + 1] = u.imag(); }
    }
    std::vector<int8_t> soft;
    std::vector<uint8_t> bits;
    double second = 0;
    for (int v = 0; v < 4; v++)
        for (int r = 0; r < 5; r++) {
            if (rateHint >= 0 && r != rateHint) continue;
            for (int o = 0; o < kSymPer[r]; o++) {
                int drop, phase;
                alignStart(r, (2 * o) % kBits[r], drop, phase);
                dvbt::Viterbi::depuncture(llr[v].data() + drop, llr[v].size() - drop, r, phase, soft, scale);
                dvbt::Viterbi::decode(soft, bits, 1, nullptr);
                int off, pos, blocks;
                bool inv;
                const int hits = s1FindSync(bits, off, pos, inv, blocks);
                const double sc = blocks > 0 ? (double)hits / blocks : 0.0;
                if (sc > best.score) {
                    second = best.score;
                    best.score = sc; best.variant = v; best.rate = r; best.offset = o; best.packets = blocks; best.inverted = inv;
                } else if (sc > second) second = sc;
            }
        }
    best.second = second;
    // a transport stream: at least a third of the packets show a sync byte where it belongs, and a clear lead over every other hypothesis
    best.ok = best.packets >= 12 && best.score >= 0.33 && best.score > 2.0 * second;
    return best;
}

// ============================================================================ streaming decoder
void S1Decoder::reset() {
    started_ = false; syncLocked_ = false; inverted_ = false;
    llr_.clear(); carry_.clear(); bits_.clear(); aligned_.clear(); after_.clear(); out_.clear(); held_.clear();
    first_ = true; skip_ = 0; phase_ = 0;
    deint_ = dvbt::ConvInterleaver(true);
    warm_ = 0; groupIdx_ = 0; haveGroup_ = false; consecBad_ = 0; syncMisses_ = 0; score_ = 0; ber_ = 0; blocks_ = 0;
    packets = rsClean = rsCorrected = rsFailed = bytesCorrected = 0;
    symbols = 0;
}

void S1Decoder::start(const S1Hypothesis& h, uint64_t skipped) {
    const uint64_t losses = syncLosses;
    reset();
    syncLosses = losses;
    h_ = h;
    h_.offset = (int)(((uint64_t)h.offset + skipped) % (uint64_t)kSymPer[h.rate]);
    started_ = true;
    alignStart(h.rate, (2 * h_.offset) % kBits[h_.rate], skip_, phase_);
}

void S1Decoder::push(const cf32* y, size_t n) {
    if (!started_) return;
    const size_t base = llr_.size();
    llr_.resize(base + 2 * n);
    for (size_t i = 0; i < n; i++) {
        const cf32 u = s1Variant(y[i], h_.variant);
        llr_[base + 2 * i] = u.real();
        llr_[base + 2 * i + 1] = u.imag();
    }
    symbols += n;
    // the GPU decodes a window of 64 of the usual windows in one call (about a million steps, 64 chunks run side by side; the chunks inside are the same)
    if (llr_.size() >= (gpuWindow_ ? 64u * 32768u : 32768u)) process();
}

void S1Decoder::process() {
    if (skip_ > 0) {
        const int k = std::min<int>(skip_, (int)llr_.size());
        llr_.erase(llr_.begin(), llr_.begin() + k);
        skip_ -= k;
    }
    const int P = kBits[h_.rate], k = kSteps[h_.rate];
    const size_t m = llr_.size() / P * P;      // whole puncturing periods: the phase is the same at the start of every chunk
    if (m == 0) return;
    double mean = 0;
    {   // sums of up to 1024 values in float (four lanes), the blocks in double
        const float* lp = llr_.data();
        for (size_t i = 0; i < m; i += 1024) {
            const size_t e = std::min(m, i + 1024);
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            size_t j = i;
            for (; j + 4 <= e; j += 4) { a0 += std::fabs(lp[j]); a1 += std::fabs(lp[j + 1]); a2 += std::fabs(lp[j + 2]); a3 += std::fabs(lp[j + 3]); }
            for (; j < e; j++) a0 += std::fabs(lp[j]);
            mean += (double)((a0 + a1) + (a2 + a3));
        }
    }
    mean /= (double)m;
    const float scale = (float)std::min(40.0, 22.0 / std::max(1e-6, mean));
    std::vector<int8_t> soft;
    dvbt::Viterbi::depuncture(llr_.data(), m, h_.rate, phase_, soft, scale);
    llr_.erase(llr_.begin(), llr_.begin() + (std::ptrdiff_t)m);
    // decode in windows with a history: the first and last L steps of a window are not reliable
    const size_t L = 192;
    std::vector<int8_t> window = carry_;
    window.insert(window.end(), soft.begin(), soft.end());
    const size_t steps = window.size() / 2;
    std::vector<uint8_t> dec;
    long metric = 0;
    bool done = false;
    const auto t0 = std::chrono::steady_clock::now();
    if (gpuWindow_ && GpuViterbi::instance().decode(window, dec, &metric)) {
        done = true;
        compute_->report(true, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / (double)std::max<size_t>(1, steps));
    }
    if (!done) {
        dvbt::Viterbi::decode(window, dec, 1, &metric);
        if (compute_) compute_->report(false, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / (double)std::max<size_t>(1, steps));
    }
    lastGpu_ = done;
    gpuWindow_ = compute_ && compute_->pickGpu();
    int64_t sai = 0;
    for (int8_t q : window) sai += q < 0 ? -q : q;
    const double sa = (double)sai;
    if (sa > 0) score_ = 0.8 * score_ + 0.2 * ((double)metric / sa);
    const size_t fromStep = carry_.empty() ? 0 : L;
    const size_t toStep = steps > L ? steps - L : 0;
    if (toStep > fromStep) bits_.insert(bits_.end(), dec.begin() + (std::ptrdiff_t)fromStep, dec.begin() + (std::ptrdiff_t)toStep);
    const size_t keep = std::min(window.size() / 2, 2 * L);
    carry_.assign(window.end() - 2 * (std::ptrdiff_t)keep, window.end());
    (void)k;
    blocks_++;

    // bits -> bytes, with the byte phase and the packet position found from the sync bytes
    if (!syncLocked_) findSync();
    if (!syncLocked_) return;
    const size_t nb = bits_.size() / 8 / 12 * 12;
    if (!nb) return;
    std::vector<uint8_t> by(nb), di(nb);
    for (size_t i = 0; i < nb; i++) { uint64_t x; memcpy(&x, &bits_[8 * i], 8); by[i] = (uint8_t)(((x & 0x0101010101010101ull) * 0x8040201008040201ull) >> 56); }   // little-endian host
    bits_.erase(bits_.begin(), bits_.begin() + (std::ptrdiff_t)(nb * 8));
    if (inverted_) for (auto& b : by) b = (uint8_t)~b;
    deint_.process(by.data(), di.data(), nb);
    after_.insert(after_.end(), di.begin(), di.end());
    static const std::vector<uint8_t> ks = [] {
        std::vector<uint8_t> t(8 * 188, 0);
        unsigned reg = 0xA9;
        auto clock8 = [&] {
            unsigned res = 0;
            for (int i = 0; i < 8; i++) { const unsigned fb = ((reg >> 13) ^ (reg >> 14)) & 1; reg = ((reg << 1) | fb) & 0x7FFF; res = (res << 1) | fb; }
            return (uint8_t)res;
        };
        for (int p = 0; p < 8; p++) { for (int j = 1; j < 188; j++) t[p * 188 + j] = clock8(); clock8(); }
        return t;
    }();
    size_t pos = 0;
    uint64_t fixedBytes = 0, blocksNow = 0;
    while (after_.size() - pos >= 204) {
        uint8_t* blk = &after_[pos];
        if (warm_ > 0) { warm_--; pos += 204; continue; }
        const bool syncOk = blk[0] == 0x47 || blk[0] == 0xB8;
        const int r = dvbt::rsDecode(blk);
        packets++;
        blocksNow++;
        uint8_t pkt[188];
        memcpy(pkt, blk, 188);
        if (r < 0) { rsFailed++; consecBad_++; }
        else { consecBad_ = 0; if (r == 0) rsClean++; else { rsCorrected++; bytesCorrected += (uint64_t)r; fixedBytes += (uint64_t)r; } }
        if (r >= 0 && pkt[0] == 0xB8) { groupIdx_ = 0; haveGroup_ = true; }
        else if (haveGroup_) groupIdx_ = (groupIdx_ + 1) % 8;
        if (haveGroup_ && consecBad_ < 8) {       // a long run of failed blocks is a stream that is lost, not packets with errors: nothing is delivered
            const uint8_t* key = &ks[(size_t)groupIdx_ * 188];
            pkt[0] = 0x47;
            for (int j = 1; j < 188; j++) pkt[j] ^= key[j];
            if (r < 0) pkt[1] |= 0x80;       // after the descrambler: its key would clear the flag in half of the positions
            deliver(pkt, r);
        }
        // sync is lost when the stream stays out of step: a misaligned stream shows a sync byte in about one block of 128
        if (r >= 0 || syncOk) syncMisses_ = 0;
        else syncMisses_++;
        // 48 blocks in a row that Reed-Solomon cannot repair: the carrier has slipped a quarter turn, or the signal was gone. The owner searches again.
        if (syncMisses_ > 100 || consecBad_ >= 48) {
            // the hypothesis (constellation variant, puncturing alignment) is no longer known to be right: the owner has to search again
            syncLocked_ = false; syncLosses++; started_ = false;
            bits_.clear(); after_.clear(); aligned_.clear(); haveGroup_ = false; syncMisses_ = 0; carry_.clear(); first_ = true;
            llr_.clear();
            releaseHeld(0);
            return;
        }
        pos += 204;
    }
    after_.erase(after_.begin(), after_.begin() + (std::ptrdiff_t)pos);
    if (blocksNow) ber_ = 0.9 * ber_ + 0.1 * ((double)fixedBytes / (double)(blocksNow * 204));
}

// A block that Reed-Solomon "corrected" can be a false correction when it sits next to a loss of signal: the last bits before a gap are decoded from
// nothing, and a word with many errors on one branch of the interleaver can land within reach of another codeword. Such a packet looks clean and is
// not. A corrected block is therefore held back for three blocks; if three blocks in a row fail behind it, it goes out with the error flag set.
void S1Decoder::deliver(uint8_t* pkt, int r) {
    if (held_.empty() && r <= 0) { out_.insert(out_.end(), pkt, pkt + 188); return; }
    Held h;
    memcpy(h.d, pkt, 188);
    h.r = r;
    held_.push_back(h);
    if (r < 0 && consecBad_ == 3) for (auto& e : held_) if (e.r > 0) e.d[1] |= 0x80;
    releaseHeld(3);
}

void S1Decoder::releaseHeld(size_t keep) {
    while (!held_.empty() && (held_.size() > keep || held_.front().r <= 0)) {
        out_.insert(out_.end(), held_.front().d, held_.front().d + 188);
        held_.pop_front();
    }
}

void S1Decoder::findSync() {
    if (bits_.size() < 8 * 204 * 14) return;
    int off, pos, blocks;
    bool inv;
    const int hits = s1FindSync(bits_, off, pos, inv, blocks);
    if (hits < 10 || hits * 2 < blocks) {
        if (bits_.size() > 8 * 204 * 40) bits_.erase(bits_.begin(), bits_.begin() + 8 * 204 * 20);     // nothing found: look further on
        return;
    }
    inverted_ = inv;
    const size_t dropBits = (size_t)off + 8 * (size_t)pos;
    bits_.erase(bits_.begin(), bits_.begin() + (std::ptrdiff_t)dropBits);
    syncLocked_ = true;
    deint_ = dvbt::ConvInterleaver(true);
    warm_ = 12;
    haveGroup_ = false;
    after_.clear();
    consecBad_ = 0; syncMisses_ = 0;
}

void S1Decoder::takePackets(std::vector<uint8_t>& out) {
    out.insert(out.end(), out_.begin(), out_.end());
    out_.clear();
}

} // namespace dvbs
} // namespace dect2
