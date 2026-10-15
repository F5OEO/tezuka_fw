// DVB-S/S2 receiver: spectrum analysis, see dvbs_spec.h.
#include "dvbs_spec.h"
#include "dect2/dsp_compat.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {
namespace dvbs {

void SpectrumEstimator::configure(double fs, int fftSize) {
    fs_ = fs;
    n_ = fftSize;
    log2n_ = 0;
    while ((1 << log2n_) < n_) log2n_++;
    n_ = 1 << log2n_;
    win_.resize(n_);
    hannWindowNorm(win_.data(), n_);
    re_.resize(n_); im_.resize(n_);
    reset();
}

void SpectrumEstimator::reset() {
    acc_.assign(n_, 0.0);
    part_.clear();
    segs_ = 0;
    skip_ = 0;
}

void SpectrumEstimator::push(const cf32* x, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (skip_) { const size_t k = std::min(skip_, n - i); skip_ -= k; i += k; continue; }
        const size_t k = std::min<size_t>((size_t)n_ - part_.size(), n - i);
        part_.insert(part_.end(), x + i, x + i + k);
        i += k;
        if ((int)part_.size() == n_) {
            for (int j = 0; j < n_; j++) { re_[j] = part_[j].real() * win_[j]; im_[j] = part_[j].imag() * win_[j]; }
            fftSplit(re_.data(), im_.data(), log2n_, false);
            for (int j = 0; j < n_; j++) {
                const int sh = (j + n_ / 2) % n_;     // fft shift: bin sh of the output holds frequency (sh - N/2)
                acc_[sh] += ((double)re_[j] * re_[j] + (double)im_[j] * im_[j]) / n_;
            }
            segs_++;
            part_.clear();
            if (stride_ > (size_t)n_) skip_ = stride_ - (size_t)n_;
        }
    }
}

std::vector<double> SpectrumEstimator::averaged() const {
    std::vector<double> p(acc_);
    if (segs_ > 0) for (auto& v : p) v /= segs_;
    return p;
}

SpectrumResult SpectrumEstimator::analyse() const { return analysePsd(averaged(), fs_, std::max(1, segs_)); }

void SpectrumEstimator::display(std::vector<float>& db, int points) const {
    const std::vector<double> p = averaged();
    db.assign(points, -200.f);
    double mx = 1e-30;
    for (double v : p) mx = std::max(mx, v);
    const int per = std::max(1, n_ / points);
    for (int i = 0; i < points; i++) {
        double s = 0; int c = 0;
        for (int k = i * n_ / points; k < std::min(n_, i * n_ / points + per); k++) { s += p[k]; c++; }
        db[i] = (float)(10 * std::log10(std::max(1e-30, c ? s / c : 0.0) / mx));
    }
}

namespace {
inline double binHzOf(double fs, int n) { return fs / n; }
// moving average with half width w (edges use what is there)
std::vector<double> boxcar(const std::vector<double>& x, int w) {
    const int n = (int)x.size();
    std::vector<double> y(n), cs(n + 1, 0.0);
    for (int i = 0; i < n; i++) cs[i + 1] = cs[i] + x[i];
    for (int i = 0; i < n; i++) { const int a = std::max(0, i - w), b = std::min(n - 1, i + w); y[i] = (cs[b + 1] - cs[a]) / (b - a + 1); }
    return y;
}
std::vector<double> median5(const std::vector<double>& x) {
    const int n = (int)x.size();
    std::vector<double> y(x);
    for (int i = 2; i < n - 2; i++) { double v[5] = {x[i - 2], x[i - 1], x[i], x[i + 1], x[i + 2]}; std::sort(v, v + 5); y[i] = v[2]; }
    return y;
}
}

namespace {
// `region` (when given) receives the bins around the strongest peak, so that a caller can blank out a candidate that could not be measured (a carrier
// cut by the edge of the band)
SpectrumResult analyseOne(const std::vector<double>& psd, double fs, int segments, std::pair<int, int>* region = nullptr) {
    SpectrumResult r;
    const int n = (int)psd.size();
    if (n < 256) return r;
    std::vector<double> p(psd);
    // the DC spike of a radio: replace the bins around 0 Hz by a straight line between their neighbours
    {
        const int c = n / 2, w = 3;
        for (int k = c - w; k <= c + w; k++) p[k] = p[c - w - 1] + (p[c + w + 1] - p[c - w - 1]) * (double)(k - (c - w - 1)) / (2 * w + 2);
    }
    const std::vector<double> sm = boxcar(median5(p), 2);
    const std::vector<double> wide = boxcar(p, std::max(8, n / 128));
    // noise floor: a low percentile of the spectrum (corrected for the bias of a percentile of a noisy average)
    std::vector<double> s(sm);
    std::sort(s.begin(), s.end());
    double floorLin = s[(size_t)(n * 0.15)] / (1.0 - 1.04 / std::sqrt(4.0 * std::max(1, segments)));
    // the carrier: the strongest region of the wide average
    int kpk = n / 2;
    for (int k = 4; k < n - 4; k++) if (wide[k] > wide[kpk]) kpk = k;
    // the plateau level: the mean of the spectrum around the peak
    double lp = wide[kpk];
    if (region) {
        const double lvl = floorLin + 0.4 * (lp - floorLin);
        int a = kpk, b = kpk;
        while (a > 0 && wide[a - 1] > lvl) a--;
        while (b < n - 1 && wide[b + 1] > lvl) b++;
        *region = {a, b};
    }
    double lo = -1, hi = -1;
    bool templ = false;
    if (lp < 4.0 * floorLin) {
        // a weak carrier: no clean edges to see. Try rectangles of every width and position: the average inside, compared with the noise floor,
        // in units of the scatter of an average over that many bins
        std::vector<double> cs(n + 1, 0.0);
        for (int i = 0; i < n; i++) cs[i + 1] = cs[i] + p[i];
        double bestZ = 0; int bw = 0, bk = 0;
        for (double w = 24; w < 0.92 * n; w *= 1.02) {
            const int wi = (int)w;
            const int stp = std::max(1, wi / 48);
            for (int k0 = wi / 2 + 2; k0 + wi / 2 + 2 < n; k0 += stp) {
                const double mean = (cs[k0 + wi / 2] - cs[k0 - wi / 2]) / wi;
                if (mean <= floorLin) continue;
                const double z = (mean - floorLin) / floorLin * std::sqrt(std::max(1, segments) * wi / 2.0);
                if (z > bestZ) { bestZ = z; bw = wi; bk = k0; }
            }
        }
        r.zscore = (float)bestZ;
        if (bestZ < 8.0 || bw < 24) return r;
        lo = bk - bw / 2.0; hi = bk + bw / 2.0;
        lp = (cs[bk + bw / 2] - cs[bk - bw / 2]) / bw;
        kpk = bk;
        templ = true;
    }
    if (!templ) r.zscore = 100.f;
    r.floorDb = (float)(10 * std::log10(std::max(1e-30, floorLin)));
    r.plateauDb = (float)(10 * std::log10(std::max(1e-30, lp)));
    r.snrDb = r.plateauDb - r.floorDb;
    if (!templ && lp < 2.0 * floorLin) return r;
    double half = floorLin + 0.5 * (lp - floorLin);
    // crossing of the half level: the first bin outward that is below and stays below for the next eight bins
    auto cross = [&](int dir) -> double {
        const int run = 8;
        for (int i = kpk; i > run && i < n - run - 1; i += dir) {
            if (sm[i] >= half) continue;
            bool stays = true;
            for (int j = 1; j <= run && stays; j++) stays = sm[i + dir * j] < half * 1.05;
            if (!stays) continue;
            const double a = sm[i - dir], b = sm[i];             // a >= half > b (or close to it)
            const double f = a > b ? (a - half) / (a - b) : 0.5;
            return (double)(i - dir) + dir * f;
        }
        return -1;
    };
    if (!templ) {
        lo = cross(-1); hi = cross(+1);
        if (lo < 0 || hi < 0 || hi - lo < 6) return r;
    }
    // the peak of a noisy spectrum is too high; the plateau is the mean over the middle of the region, and the edges follow from that level
    for (int it = 0; it < (templ ? 0 : 3); it++) {
        const int a = (int)(lo + 0.3 * (hi - lo)), b = (int)(lo + 0.7 * (hi - lo));
        double m = 0; int c = 0;
        for (int k = a; k <= b; k++) { m += p[k]; c++; }
        lp = m / std::max(1, c);
        half = floorLin + 0.5 * (lp - floorLin);
        const double l2 = cross(-1), h2 = cross(+1);
        if (l2 < 0 || h2 < 0 || h2 - l2 < 6) break;
        lo = l2; hi = h2;
    }
    r.plateauDb = (float)(10 * std::log10(std::max(1e-30, lp)));
    r.snrDb = r.plateauDb - r.floorDb;
    const double binHz = fs / n;
    r.edgeLoHz = (lo - n / 2) * binHz;
    r.edgeHiHz = (hi - n / 2) * binHz;
    r.rateHz = r.edgeHiHz - r.edgeLoHz;
    r.centerHz = 0.5 * (r.edgeHiHz + r.edgeLoHz);
    {   // least squares fit of a raised cosine spectrum to all bins around the carrier: centre, rate, level, floor (and the roll-off when the signal is strong)
        double th[5] = {r.centerHz / binHzOf(fs, n), r.rateHz / binHzOf(fs, n), lp, floorLin, 0.35};   // centre and rate in bins
        const bool fitAlpha = lp > 15.0 * floorLin;
        const int np = fitAlpha ? 5 : 4;
        const int a = std::max(3, (int)(th[0] + n / 2 - 0.62 * th[1])), b = std::min(n - 4, (int)(th[0] + n / 2 + 0.62 * th[1]));
        auto model = [&](const double* t, int k) {
            const double x = std::fabs((double)(k - n / 2) - t[0]) / t[1];
            const double al = std::max(0.03, std::min(0.6, t[4]));
            double m;
            if (x <= 0.5 * (1 - al)) m = 1;
            else if (x >= 0.5 * (1 + al)) m = 0;
            else { const double c = std::cos(3.14159265358979 / (2 * al) * (x - 0.5 * (1 - al))); m = c * c; }
            return t[3] + t[2] * m;
        };
        std::vector<double> wgt(n, 0.0);
        for (int k = a; k <= b; k++) { const double mv = model(th, k); wgt[k] = 1.0 / (mv * mv); }
        auto cost = [&](const double* t) { double c = 0; for (int k = a; k <= b; k++) { const double d = p[k] - model(t, k); c += wgt[k] * d * d; } return c; };
        double lambda = 1e-2, cur = cost(th);
        const double c0 = cur;
        for (int it = 0; it < 25; it++) {
            double J[5][1] = {}, A[5][5] = {}, g[5] = {};
            (void)J;
            const double step[5] = {0.5, 0.002 * th[1], 0.01 * th[2], 0.01 * th[3], 0.01};
            std::vector<std::vector<double>> jac(np, std::vector<double>(b - a + 1));
            for (int q = 0; q < np; q++) {
                double t2[5]; memcpy(t2, th, sizeof t2); t2[q] += step[q];
                for (int k = a; k <= b; k++) jac[q][k - a] = (model(t2, k) - model(th, k)) / step[q];
            }
            for (int k = a; k <= b; k++) {
                const double res = p[k] - model(th, k), w = wgt[k];
                for (int i = 0; i < np; i++) { g[i] += w * jac[i][k - a] * res; for (int j = 0; j < np; j++) A[i][j] += w * jac[i][k - a] * jac[j][k - a]; }
            }
            // solve (A + lambda diag(A)) d = g by Gaussian elimination
            double M[5][6];
            for (int i = 0; i < np; i++) { for (int j = 0; j < np; j++) M[i][j] = A[i][j] + (i == j ? lambda * A[i][i] : 0); M[i][np] = g[i]; }
            bool ok = true;
            for (int i = 0; i < np && ok; i++) {
                int piv = i;
                for (int j = i + 1; j < np; j++) if (std::fabs(M[j][i]) > std::fabs(M[piv][i])) piv = j;
                if (std::fabs(M[piv][i]) < 1e-30) { ok = false; break; }
                for (int j = 0; j <= np; j++) std::swap(M[i][j], M[piv][j]);
                for (int j = i + 1; j < np; j++) { const double f = M[j][i] / M[i][i]; for (int k = i; k <= np; k++) M[j][k] -= f * M[i][k]; }
            }
            if (!ok) break;
            double d[5] = {};
            for (int i = np - 1; i >= 0; i--) { double v = M[i][np]; for (int j = i + 1; j < np; j++) v -= M[i][j] * d[j]; d[i] = v / M[i][i]; }
            double t2[5]; memcpy(t2, th, sizeof t2);
            for (int i = 0; i < np; i++) t2[i] += d[i];
            if (t2[1] <= 4 || t2[2] <= 0 || t2[3] <= 0) { lambda *= 10; continue; }
            const double c2 = cost(t2);
            if (c2 < cur) { memcpy(th, t2, sizeof th); cur = c2; lambda = std::max(1e-6, lambda * 0.3); if (std::fabs(d[1]) < 1e-3 * th[1] * 0.01 && std::fabs(d[0]) < 0.01) break; }
            else lambda *= 8;
        }
        if (cur < c0 * 1.0001 && th[1] > 8) {
            r.rateHz = th[1] * binHzOf(fs, n);
            r.centerHz = th[0] * binHzOf(fs, n);
            r.edgeLoHz = r.centerHz - 0.5 * r.rateHz; r.edgeHiHz = r.centerHz + 0.5 * r.rateHz;
            lp = th[2]; floorLin = th[3];
            r.plateauDb = (float)(10 * std::log10(std::max(1e-30, lp)));
            r.floorDb = (float)(10 * std::log10(std::max(1e-30, floorLin)));
            r.snrDb = r.plateauDb - r.floorDb;
            if (fitAlpha) r.rollOff = std::max(0.03, std::min(0.6, th[4]));
        }
        // misfit: weighted rms of the residual over the carrier (1 = the scatter of an averaged periodogram is the only error)
        double ms = 0; int cnt = 0;
        for (int k = a; k <= b; k++) { const double mv = model(th, k); ms += (p[k] - mv) * (p[k] - mv) / (mv * mv); cnt++; }
        r.fitRms = (float)std::sqrt(ms / std::max(1, cnt));
    }
    // flatness of the plateau
    {
        const int a = (int)(lo + 0.2 * (hi - lo)), b = (int)(lo + 0.8 * (hi - lo));
        double m = 0, v = 0; int c = 0;
        for (int k = a; k <= b; k++) { m += sm[k]; c++; }
        m /= std::max(1, c);
        for (int k = a; k <= b; k++) { const double d = 10 * std::log10(std::max(1e-30, sm[k]) / std::max(1e-30, m)); v += d * d; }
        r.flatnessDb = (float)std::sqrt(v / std::max(1, c));
    }
    // roll-off from the width at -20 dB: the raised cosine reaches 1 % of the plateau at 0.5 + 0.436 alpha symbol rates from the centre
    if (lp > 150.0 * floorLin) {
        const double l20 = floorLin + 0.01 * (lp - floorLin);
        auto cross20 = [&](int dir) -> double {
            for (int i = (dir < 0 ? (int)lo : (int)hi); i > 4 && i < n - 5; i += dir) {
                if (sm[i] >= l20) continue;
                const double a = sm[i - dir], b = sm[i];
                const double f = a > b ? (a - l20) / (a - b) : 0.5;
                return (double)(i - dir) + dir * f;
            }
            return -1;
        };
        const double a20 = cross20(-1), b20 = cross20(+1);
        if (a20 > 0 && b20 > 0) {
            r.bw20Hz = (b20 - a20) * binHz;
            if (r.rollOff == 0) r.rollOff = std::max(0.0, std::min(0.5, (r.bw20Hz / r.rateHz - 1.0) / 0.872));
        }
    }
    r.valid = true;
    return r;
}
} // namespace

// A transponder carries several carriers side by side: the one wanted is the one the user tuned to, the one nearest the centre among those that are not
// much weaker than the strongest. The strongest is found first, blanked out, and the search repeated.
SpectrumResult analysePsd(const std::vector<double>& psd, double fs, int segments) {
    std::vector<double> p(psd);
    std::vector<SpectrumResult> found;
    const int n = (int)p.size();
    for (int it = 0; it < 4; it++) {
        std::pair<int, int> reg{0, -1};
        const SpectrumResult r = analyseOne(p, fs, segments, &reg);
        if (!r.valid) {
            // a candidate that cannot be measured (cut by the edge of the band, say): blank it out and look at the next one
            if (reg.second < reg.first || r.zscore < 8.f || it == 3) break;
            std::vector<double> s(p);
            std::sort(s.begin(), s.end());
            const double fl = s[(size_t)(n * 0.15)];
            const int m = std::max(8, (reg.second - reg.first) / 3);
            for (int k = std::max(0, reg.first - m); k <= std::min(n - 1, reg.second + m); k++) p[(size_t)k] = fl;
            continue;
        }
        if (r.zscore < 8.f) break;
        found.push_back(r);
        const double bin = fs / n, w = r.edgeHiHz - r.edgeLoHz;
        const int a = std::max(0, (int)std::floor((r.edgeLoHz - 0.35 * w) / bin + n / 2.0)), b = std::min(n - 1, (int)std::ceil((r.edgeHiHz + 0.35 * w) / bin + n / 2.0));
        const double floorLin = std::pow(10.0, r.floorDb / 10.0);
        for (int k = a; k <= b; k++) p[(size_t)k] = floorLin;
    }
    if (found.empty()) return analyseOne(psd, fs, segments);
    double top = -1e30;
    for (const auto& r : found) top = std::max(top, (double)r.plateauDb);
    const SpectrumResult* best = &found[0];
    for (const auto& r : found)
        if (r.plateauDb >= top - 12.0 && std::fabs(r.centerHz) < std::fabs(best->centerHz)) best = &r;
    return *best;
}

} // namespace dvbs
} // namespace dect2
