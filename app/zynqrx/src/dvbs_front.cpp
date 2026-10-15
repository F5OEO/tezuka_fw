// DVB-S/S2 receiver front end, see dvbs_front.h.
#include "dvbs_front.h"
#include <algorithm>
#include <cmath>

namespace dect2 {
namespace dvbs {

void DvbsFront::configure(const Config& c) {
    c_ = c;
    mix_.configure(c.fs, c.centerHz);
    // decimate by two while at least three samples per symbol remain
    int k = 0;
    while (c.fs / (double)(1 << (k + 1)) >= 3.0 * c.symbolRate && k < 8) k++;
    hb_.assign((size_t)k, Halfband());
    stage_.assign((size_t)k + 1, SplitBuf());
    fs1_ = c.fs / (double)(1 << k);
    // prefiltered input already at (nearly) two samples per symbol: the timing loop absorbs the difference, so the nominal rate is what arrives
    bypass_ = c.prefiltered && std::fabs(fs1_ / (2.0 * c.symbolRate) - 1.0) < 0.03;
    if (bypass_) c_.symbolRate = fs1_ / 2.0;
    else rs_.configure(fs1_ / (2.0 * c.symbolRate));
    // the matched filter spans 2 * hs symbols: the tails of a root raised cosine pulse fall off more slowly the smaller the roll-off, so a small
    // roll-off needs a longer filter (the truncation stays below -40 dB; each symbol of span costs four multiply-adds per sample of the filter)
    const int hs = c.rollOff >= 0.30 ? 6 : c.rollOff >= 0.20 ? 8 : c.rollOff >= 0.10 ? 10 : 12;
    // 16 interpolator taps: QPSK and 8PSK lose about 0.05 dB against 24 taps, 32APSK a good deal more
    // prefiltered: only the interpolation between the radio's samples is left. Eight taps of a windowed sinc cost no measurable quality (MER within 0.1 dB at
    // 25 dB, up to 16APSK, roll-off 0.2 to 0.35) and half the multiply-accumulates of 16; on a Cortex-A9 these are slow (about 5 cycles per 4 of them)
    if (c.prefiltered) timer_.configure(c.rollOff, 2, true);
    else timer_.configure(c.rollOff, hs);
    reset();
}

void DvbsFront::reset() {
    mix_.reset();
    for (auto& h : hb_) h.reset();
    rs_.reset();
    timer_.reset();
    a_.clear(); b_.clear(); two_.clear();
}

void DvbsFront::recentre() {
    if (bypass_) return;      // no resampler to fold the offset into: the loop keeps it
    const double off = timer_.rateOffset();
    // the loop found that the symbols come (1 + off) times faster than nominal: the resampler should produce 2 samples per real symbol
    c_.symbolRate *= (1.0 + off);
    rs_.setRatio(fs1_ / (2.0 * c_.symbolRate));
    timer_.shiftIntegrator(off);
}

void DvbsFront::process(const cf32* x, size_t n, std::vector<cf32>& out) {
    size_t done = 0;
    while (done < n) {
        const size_t m = std::min<size_t>(4096, n - done);
        if (bypass_) {   // prefiltered at 2 samples per symbol: the mixer writes into the timer's own buffer, no copy of the block in between
            mix_.process(x + done, m, timer_.inbuf());
            done += m;
            timer_.run(out);
            continue;
        }
        a_.clear();
        mix_.process(x + done, m, a_);
        done += m;
        SplitBuf* cur = &a_;
        for (size_t k = 0; k < hb_.size(); k++) {
            stage_[k].clear();
            hb_[k].process(*cur, cur->size(), stage_[k]);
            cur = &stage_[k];
        }
        two_.clear();
        rs_.process(*cur, cur->size(), two_);
        timer_.process(two_, out);
    }
}

} // namespace dvbs
} // namespace dect2
