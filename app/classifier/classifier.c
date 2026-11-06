/*
 * classifier.c — onboard PSD spectral-shape signal classifier
 *
 * Connects as a WebSocket *client* to maia-httpd's existing /waterfall
 * feed (ws://localhost/waterfall) — the same magnitude-only FFT stream
 * Dashboard/radioastro.jsx already consumes in the browser — normalizes
 * each frame, correlates it against a set of pre-computed templates
 * loaded from disk, and publishes the best match over MQTT.
 *
 * Deliberately does NOT touch iio_ws_proxy or the raw IQ path: this is
 * a magnitude/PSD spectral-shape matcher, not a true (phase-sensitive)
 * cyclostationary SCF classifier — see the plan doc for why. It adds no
 * new consumer of the AD9361 ADC, since maia-httpd is already running
 * and already computing this exact FFT for the Dashboard's own Spectrum
 * and Radio Astronomy pages.
 *
 * Frame format (from maia-httpd, matching Dashboard/radioastro.jsx's
 * parsing): binary WS message = float32[], f[0] is a structural step
 * index (unused here), f[1..] are FFT bins as raw linear amplitude.
 *
 * Template file format (little-endian, shared with the offline
 * tools/classifier_templates generator):
 *   char     magic[4]      = "SCFT"
 *   uint32_t version       = 1 or 2
 *   uint32_t count
 *   uint32_t canonical_n
 *   then `count` entries of:
 *     char     label[32]   (NUL-padded, truncated if longer)
 *     uint32_t template_id
 *     float    data[canonical_n]   (pre-normalized: zero-mean, unit L2 norm)
 *     -- version 2 only, appended per entry --
 *     float    bandwidth_frac_lo, bandwidth_frac_hi
 *     float    papr_db_lo,        papr_db_hi
 *     float    flatness_lo,       flatness_hi
 *     float    peak_count_lo,     peak_count_hi
 *   (a range with hi < lo means "unbounded" — no gate on that feature;
 *   version 1 files are loaded as if every range were unbounded, so old
 *   templates.bin files still classify exactly as before)
 *
 * Beyond the shape-correlation score, each frame's scalar features
 * (occupied bandwidth, peak-to-average ratio, spectral flatness, local
 * peak count — see extract_frame_features()) are published every cycle
 * regardless of match outcome, plus two cross-cycle features tracked in a
 * short rolling history (see history_push()/history_compute()): peak-bin
 * drift (catches a genuinely sweeping/chirping carrier, which a single
 * static frame can't show) and duty cycle (catches bursty/pulsed
 * emitters). A version-2 template can also declare expected ranges for
 * the four per-frame features; classify() then applies those as a soft
 * penalty on top of the shape score (see template_gate_factor()), so two
 * classes with a similar curve but different real bandwidth/peakiness
 * can still be told apart.
 *
 * Build (cross-compile for Pluto):
 * $(CC) -O2 -o classifier classifier.c -lwebsockets -lmosquitto -lm
 *
 * Usage:
 * classifier [OPTIONS]
 * -H HOST   maia-httpd host          (default: localhost)
 * -p PORT   maia-httpd port          (default: 80)
 * -P PATH   waterfall WS path        (default: /waterfall)
 * -f FILE   templates file           (default: /mnt/jffs2/classifier/templates.bin)
 * -B HOST   MQTT broker host         (default: localhost)
 * -q PORT   MQTT broker port         (default: 1883)
 * -c THRESH match confidence floor   (default: 0.6, below this publishes "unknown")
 * -m MARGIN hysteresis hold margin   (default: 0.15 — once locked onto a
 *           template, only drops back to "unknown" below THRESH - MARGIN,
 *           so a borderline score doesn't flip found/unknown every cycle)
 * -i SEC    min seconds between correlate+publish cycles (default: 1.0)
 * -S HZ     span of the /waterfall frame in Hz when MQTT cannot supply it (replay)
 * -w HZ     width of the receiver-baseline structuring element (default: 4e6)
 * -x DB     persistent narrow structure above the baseline counts as a spur
 *           candidate when it is seen on two different RX frequencies (default: 8)
 * -s DB     minimum window SNR over the background to accept a match (default: 6)
 * -g DB     minimum prominence of the matched window over its flanks (default: 4)
 * -l SEC    background learning time after start/retune (default: 10)
 * -a SEC    background adaptation time constant after learning (default: 300; 0 = frozen)
 * -r FILE   replay raw float32 frames from FILE instead of /waterfall (offline test)
 * -n N      floats per replayed frame (default: 4096)
 * -F FPS    virtual frame rate for replay timing (default: 7.5)
 * -v        verbose (log every publish to stderr too)
 *
 * Background model: the /waterfall power spectrum is dominated by the
 * receiver's own response (the AD9361 RF filter passband inside the
 * sample-rate span, gain-dependent noise floor, DC/LO spurs). Correlating
 * templates straight against it finds those shapes: on a SignalSDRPro at
 * 2.4 GHz / 30.72 MS/s / 18 MHz RF bandwidth, the seed templates scored
 * 0.65-0.95 on an empty band, all of it the filter edge. So the daemon
 * (1) tracks a per-bin LOWER ENVELOPE of the live spectrum over time
 * (bins move the estimate down quickly and up slowly, so bursty traffic
 * does not pull it up; settles over the first -l seconds after start or
 * any RX retune, then rises with time constant -a), and (2) derives the
 * RECEIVER BASELINE from that envelope by a morphological opening with
 * a -w Hz wide structuring element: the passband curve and the noise
 * floor are smooth on that scale and survive, anything narrower (a
 * broadcast station that is always on, a spur) is removed from the
 * baseline and therefore stays in the RESIDUAL, live minus baseline,
 * that the templates are matched against. Emitters wider than -w are
 * absorbed into the baseline and cannot be classified.
 *
 * Spurs: the receiver's own narrow lines (DC, clock harmonics) also
 * survive in the residual and would be labeled. The centre bins are
 * always masked; other persistent narrow lines (-x dB over the baseline
 * at the end of learning) are remembered per bin, and a bin that is hot
 * on two different RX frequencies is a receiver spur, masked from then
 * on. A station is only hot while the LO sits near it.
 *
 * A match must sit on a window whose power exceeds the baseline by -s dB
 * (measured on a 16-bin smoothed peak inside the matched window, so a
 * narrow carrier in a wide window still counts) and must stand -g dB
 * above the live level on either side of the window: a broadband floor
 * rise (the AD9361 desenses by ~9 dB across the whole span when a nearby
 * Wi-Fi burst hits it) clears the SNR gate everywhere but has no
 * prominence anywhere. cmd/classifier/background "learn" forces a relearn.
 *
 * Template resolution: a version-3 template records its hertz per bin.
 * At each frame size or span change it is resampled onto the live grid
 * (canonical_n * hz_per_bin / live_hz_per_bin bins, at least 8), so a
 * 300 kHz FM shape is matched over 300 kHz whatever the span. Version
 * 1/2 templates have no resolution and are matched bin-for-bin as before.
 */

#define _GNU_SOURCE
#include <libwebsockets.h>
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

static void sanitize_label(char *label, size_t n);

/* ------------------------------------------------------------------ */
/* Templates                                                           */
/* ------------------------------------------------------------------ */

/* A range with hi < lo means "unbounded" (no gate on this feature) — the
 * default for every field on a version-1 template. */
struct feature_range { float lo, hi; };

struct template {
    char     label[32];
    uint32_t id;
    float   *data;   /* canonical_n floats, pre-normalized (zero-mean, unit L2 norm) */
    float    hz_per_bin;  /* version 3; 0 = no resolution, match bin-for-bin */

    /* data resampled onto the live grid and re-normalized, see
     * templates_prepare(); live_n == canonical_n and live == data when
     * hz_per_bin is 0 and the frame is at least canonical_n wide. */
    float   *live;
    uint32_t live_n;

    /* version 2 only; has_gate is false (all ranges unbounded) for
     * version-1 files, which then behave exactly as before. */
    bool     has_gate;
    struct feature_range bandwidth_frac, papr_db, flatness, peak_count;
};

struct templates {
    uint32_t          canonical_n;
    uint32_t          count;
    struct template   *items;
    double            prepared_hz;      /* live hz/bin the items[].live grids were built for */
    uint32_t          prepared_live_n;
};

static void templates_free(struct templates *t)
{
    if (!t->items) return;
    for (uint32_t i = 0; i < t->count; i++) {
        if (t->items[i].live != t->items[i].data) free(t->items[i].live);
        free(t->items[i].data);
    }
    free(t->items);
    t->items = NULL;
    t->count = 0;
}

/* Returns 0 on success (t populated), -1 on failure (t left zeroed —
 * caller should treat this as "no templates loaded", not a fatal error,
 * since the file legitimately may not exist yet on a fresh device). */
static int templates_load(const char *path, struct templates *t)
{
    memset(t, 0, sizeof(*t));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char magic[4];
    uint32_t version, count, canonical_n;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "SCFT", 4) != 0) {
        fprintf(stderr, "templates: bad magic in %s\n", path);
        fclose(f);
        return -1;
    }
    if (fread(&version, sizeof(version), 1, f) != 1 || version < 1 || version > 3) {
        fprintf(stderr, "templates: unsupported version in %s\n", path);
        fclose(f);
        return -1;
    }
    if (fread(&count, sizeof(count), 1, f) != 1 ||
        fread(&canonical_n, sizeof(canonical_n), 1, f) != 1 ||
        count == 0 || canonical_n == 0 || count > 4096 || canonical_n > (1u << 20)) {
        fprintf(stderr, "templates: bad header in %s\n", path);
        fclose(f);
        return -1;
    }

    struct template *items = calloc(count, sizeof(*items));
    if (!items) { fclose(f); return -1; }

    for (uint32_t i = 0; i < count; i++) {
        if (fread(items[i].label, 1, sizeof(items[i].label), f) != sizeof(items[i].label))
            goto fail;
        items[i].label[sizeof(items[i].label) - 1] = '\0';
        sanitize_label(items[i].label, sizeof(items[i].label));
        if (fread(&items[i].id, sizeof(items[i].id), 1, f) != 1)
            goto fail;
        items[i].data = malloc(canonical_n * sizeof(float));
        if (!items[i].data) goto fail;
        if (fread(items[i].data, sizeof(float), canonical_n, f) != canonical_n)
            goto fail;

        if (version >= 2) {
            float ranges[8];
            if (fread(ranges, sizeof(float), 8, f) != 8) goto fail;
            items[i].has_gate       = true;
            items[i].bandwidth_frac = (struct feature_range){ ranges[0], ranges[1] };
            items[i].papr_db        = (struct feature_range){ ranges[2], ranges[3] };
            items[i].flatness       = (struct feature_range){ ranges[4], ranges[5] };
            items[i].peak_count     = (struct feature_range){ ranges[6], ranges[7] };
        } else {
            items[i].has_gate = false; /* v1: no gate at all, ranges are never consulted */
        }
        if (version >= 3) {
            if (fread(&items[i].hz_per_bin, sizeof(float), 1, f) != 1) goto fail;
            if (!(items[i].hz_per_bin > 0.0f) || items[i].hz_per_bin > 1e9f) items[i].hz_per_bin = 0.0f;
        }
        items[i].live = items[i].data;
        items[i].live_n = canonical_n;
        continue;
fail:
        fprintf(stderr, "templates: truncated/corrupt %s\n", path);
        for (uint32_t j = 0; j <= i; j++) free(items[j].data);
        free(items);
        fclose(f);
        return -1;
    }

    fclose(f);
    t->canonical_n = canonical_n;
    t->prepared_hz = 0.0;
    t->prepared_live_n = 0;
    t->count       = count;
    t->items       = items;
    fprintf(stderr, "templates: loaded %u templates (%u bins each) from %s\n",
            count, canonical_n, path);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Signal processing                                                   */
/* ------------------------------------------------------------------ */

/* Linear-interpolate src[0..src_n) into dst[0..dst_n), in place resample.
 * If src_n == dst_n this degenerates to a copy. */
static void resample_linear(const float *src, uint32_t src_n, float *dst, uint32_t dst_n)
{
    if (src_n == dst_n) { memcpy(dst, src, dst_n * sizeof(float)); return; }
    if (src_n < 2 || dst_n == 0) { for (uint32_t i = 0; i < dst_n; i++) dst[i] = 0.0f; return; }

    double scale = (double)(src_n - 1) / (double)(dst_n > 1 ? dst_n - 1 : 1);
    for (uint32_t i = 0; i < dst_n; i++) {
        double pos = i * scale;
        uint32_t lo = (uint32_t)pos;
        uint32_t hi = lo + 1 < src_n ? lo + 1 : lo;
        double frac = pos - lo;
        dst[i] = (float)(src[lo] * (1.0 - frac) + src[hi] * frac);
    }
}

/* Zero-mean, unit-L2-norm normalize in place. Returns false (vector left
 * as all-zero) if the input has ~no energy, so callers can skip it. */
static bool normalize(float *v, uint32_t n)
{
    if (n == 0) return false;
    double mean = 0.0;
    for (uint32_t i = 0; i < n; i++) mean += v[i];
    mean /= n;

    double energy = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        v[i] = (float)(v[i] - mean);
        energy += (double)v[i] * v[i];
    }
    if (energy < 1e-12) return false;

    double inv_norm = 1.0 / sqrt(energy);
    for (uint32_t i = 0; i < n; i++) v[i] = (float)(v[i] * inv_norm);
    return true;
}

/* Build each template's live-grid copy for the current frame (live_n bins
 * spanning live_hz hertz per bin). A template with a resolution is
 * stretched or squeezed to canonical_n * hz_per_bin / live_hz bins
 * (clamped to [8, live_n]); one without is used as is, unless the frame
 * is narrower than it, in which case it is squeezed to the frame. No-op
 * when nothing changed since the last call. */
#define TEMPLATE_MIN_LIVE_BINS 8
static void templates_prepare(struct templates *t, double live_hz, uint32_t live_n)
{
    if (!t->items || live_n == 0) return;
    if (t->prepared_hz == live_hz && t->prepared_live_n == live_n) return;
    for (uint32_t i = 0; i < t->count; i++) {
        struct template *tpl = &t->items[i];
        uint32_t want = t->canonical_n;
        if (tpl->hz_per_bin > 0.0f && live_hz > 0.0)
            want = (uint32_t)(t->canonical_n * (double)tpl->hz_per_bin / live_hz + 0.5);
        if (want < TEMPLATE_MIN_LIVE_BINS) want = TEMPLATE_MIN_LIVE_BINS;
        if (want > live_n) want = live_n;
        if (tpl->live != tpl->data) { free(tpl->live); tpl->live = tpl->data; }
        tpl->live_n = t->canonical_n;
        if (want == t->canonical_n) continue;
        float *r = malloc(want * sizeof(float));
        if (!r) continue;
        resample_linear(tpl->data, t->canonical_n, r, want);
        if (!normalize(r, want)) { free(r); continue; }
        tpl->live = r;
        tpl->live_n = want;
    }
    t->prepared_hz = live_hz;
    t->prepared_live_n = live_n;
}

/* Both a and b must already be zero-mean/unit-L2-norm of length n —
 * their dot product is then the cosine similarity, in [-1, 1]. */
static double cosine_sim(const float *a, const float *b, uint32_t n)
{
    double s = 0.0;
    for (uint32_t i = 0; i < n; i++) s += (double)a[i] * b[i];
    return s;
}

/* ------------------------------------------------------------------ */
/* Frame features — scalar descriptors of a single live frame, computed
 * once per cycle from the linear POWER bins (the /waterfall feed is
 * integrated |X|^2; before normalize() mean-centers them, which would
 * make these power ratios meaningless).
 * Published every cycle regardless of match outcome (see
 * publish_features()) and, for a version-2 template, also used to gate
 * the shape score (see template_gate_factor()). */
/* ------------------------------------------------------------------ */
struct frame_features {
    bool     valid;         /* false if the frame had ~no energy at all */
    double   bandwidth_frac;/* -10 dB occupied width, as a fraction of the frame */
    double   papr_db;       /* peak-to-average power ratio */
    double   flatness;      /* spectral flatness (geomean/mean of power), in (0, 1] */
    double   peak_count;    /* number of locally-prominent maxima */
    uint32_t peak_bin;      /* index of the global peak */
};

/* Cross-cycle features from history_compute() — see the rolling
 * `history[]` ring in struct app. Both fields are "n/a" (valid=false)
 * until the ring has enough samples spanning enough real time; no
 * spurious estimates right after the daemon (re)starts. */
struct temporal_features {
    bool     drift_valid;
    double   drift_bins_per_s; /* + = peak moving toward higher bins over time */
    bool     duty_valid;
    double   duty_cycle_pct;   /* % of recent cycles with a standout peak present */
};

/* -10 dB occupied bandwidth is found by scanning outward from the peak
 * until power drops below peak/10, not a global threshold count — that
 * stays robust to a stray unrelated noise bin elsewhere in the frame. */
#define BW_GATE_RATIO      0.1   /* -10 dB */
/* A candidate peak must be within this ratio of the global peak to count
 * for peak_count — otherwise every noise wiggle would count. */
#define PEAK_PROMINENCE_RATIO 0.0316 /* -15 dB */

/* `raw` is linear power per bin (what /waterfall carries, and what the
 * residual keeps). An earlier version squared it as if it were amplitude,
 * which doubled every dB figure and squared the flatness. */
static struct frame_features extract_frame_features(const float *raw, uint32_t n)
{
    struct frame_features ff;
    memset(&ff, 0, sizeof(ff));
    if (n == 0) return ff;

    uint32_t peak_i = 0;
    double peak_power = raw[0] > 0 ? raw[0] : 0.0;
    double sum_power = 0.0, sum_log_power = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double power = raw[i] > 0 ? raw[i] : 0.0;
        sum_power     += power;
        sum_log_power += log(power > 1e-20 ? power : 1e-20);
        if (power > peak_power) { peak_power = power; peak_i = i; }
    }
    if (peak_power <= 0.0 || sum_power <= 0.0) return ff; /* leaves valid=false */

    double mean_power = sum_power / n;

    ff.papr_db  = 10.0 * log10(peak_power / (mean_power > 1e-20 ? mean_power : 1e-20));
    double geomean_power = exp(sum_log_power / n);
    ff.flatness = geomean_power / (mean_power > 1e-20 ? mean_power : 1e-20);
    if (ff.flatness > 1.0) ff.flatness = 1.0; /* numerical guard: should be <=1 by AM-GM */

    double bw_gate = peak_power * BW_GATE_RATIO;
    uint32_t lo = peak_i, hi = peak_i;
    while (lo > 0     && (double)raw[lo - 1] >= bw_gate) lo--;
    while (hi + 1 < n && (double)raw[hi + 1] >= bw_gate) hi++;
    ff.bandwidth_frac = (double)(hi - lo + 1) / (double)n;

    /* Local maxima above a fixed prominence floor, deduped by a minimum
     * bin separation (n/64, at least 1) — a lightweight heuristic, not a
     * proper prominence-based peak detector, but enough to tell "one
     * carrier" from "a comb of several" in a single pass. */
    double prom_gate = peak_power * PEAK_PROMINENCE_RATIO;
    uint32_t min_sep = n / 64 > 1 ? n / 64 : 1;
    uint32_t count = 0, last_peak = 0;
    bool have_last = false;
    for (uint32_t i = 1; i + 1 < n; i++) {
        double p = (double)raw[i];
        if (p < prom_gate) continue;
        if (raw[i] <= raw[i - 1] || raw[i] <= raw[i + 1]) continue;
        if (have_last && (i - last_peak) < min_sep) continue;
        count++;
        last_peak = i;
        have_last = true;
    }
    ff.peak_count = (double)count;

    ff.peak_bin = peak_i;
    ff.valid = true;
    return ff;
}

/* Soft penalty in (0, 1] for how well `ff` matches a version-2 template's
 * declared expected ranges. A hard reject at the boundary would be
 * brittle — real-world noise sits right there — so this steps down a
 * bounded amount per out-of-range feature instead of rejecting outright,
 * keeping the ranking well-behaved while still letting a badly-mismatched
 * feature (wrong bandwidth by a wide margin, say) push a shape-similar-
 * but-wrong template below a better-gated competitor. Templates without a
 * gate (v1, or a v2 entry with every range left unbounded) always return
 * 1.0 — no effect on their score. */
/* Four gated features (bandwidth/PAPR/flatness/peak_count) at 0.15 each
 * means missing all four lands exactly at the floor (1.0 - 4*0.15 = 0.4) —
 * chosen together so the floor is an actual reachable bound, not a dead
 * constant a smaller per-feature penalty would never hit. */
#define GATE_PENALTY_PER_FEATURE 0.15
#define GATE_PENALTY_FLOOR       0.4

static double range_miss(double v, struct feature_range r)
{
    if (r.hi < r.lo) return 0.0; /* unbounded: no penalty */
    return (v >= r.lo && v <= r.hi) ? 0.0 : 1.0;
}

static double template_gate_factor(const struct template *tpl, const struct frame_features *ff)
{
    if (!tpl->has_gate || !ff->valid) return 1.0;
    double misses = range_miss(ff->bandwidth_frac, tpl->bandwidth_frac)
                  + range_miss(ff->papr_db,        tpl->papr_db)
                  + range_miss(ff->flatness,       tpl->flatness)
                  + range_miss(ff->peak_count,     tpl->peak_count);
    double factor = 1.0 - misses * GATE_PENALTY_PER_FEATURE;
    return factor < GATE_PENALTY_FLOOR ? GATE_PENALTY_FLOOR : factor;
}

struct match_result {
    bool     found;
    uint32_t template_idx;
    double   score;          /* clamped to [0, 1] */
    int32_t  bin_offset;     /* position (in live-frame bins) where the best window started */
    uint32_t bandwidth_bins; /* width of the matched window, in live-frame bins */
    double   snr_db;         /* window power over the background, dB */
    double   prom_db;        /* window peak over the flanking live level, dB */
};

/* Plain float32 dot product, NEON when available. Templates are zero-mean,
 * so dot(window, tpl) == dot(window - mean, tpl): no per-window centering
 * is needed, and with the window's sum and sum-of-squares from prefix
 * sums each sliding position costs exactly this one dot product. The
 * previous implementation centered, normalized and dotted every window
 * in double precision with a serial accumulator: ~41% of a Cortex-A9
 * core for three templates. */
static inline float dot_f32(const float *a, const float *b, uint32_t n)
{
    uint32_t i = 0;
    float s;
#ifdef __ARM_NEON
    float32x4_t acc0 = vdupq_n_f32(0.0f), acc1 = vdupq_n_f32(0.0f);
    for (; i + 8 <= n; i += 8) {
        acc0 = vmlaq_f32(acc0, vld1q_f32(a + i),     vld1q_f32(b + i));
        acc1 = vmlaq_f32(acc1, vld1q_f32(a + i + 4), vld1q_f32(b + i + 4));
    }
    float32x4_t acc = vaddq_f32(acc0, acc1);
    float32x2_t h = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    s = vget_lane_f32(vpadd_f32(h, h), 0);
#else
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3];
    }
    s = s0 + s1 + s2 + s3;
#endif
    for (; i < n; i++) s += a[i] * b[i];
    return s;
}

/* Window SNR: the largest SNR_SMOOTH_BINS-bin average of live/background
 * inside [start, start+n), in dB. Window-mean SNR would hide a narrow
 * carrier inside a wide template window; the smoothed peak does not. */
#define SNR_SMOOTH_BINS 16
static double window_snr_db(const double *pl, const double *pb, uint32_t start, uint32_t n)
{
    uint32_t w = n < SNR_SMOOTH_BINS ? n : SNR_SMOOTH_BINS;
    double best = 0.0;
    for (uint32_t i = start; i + w <= start + n; i++) {
        double lw = pl[i + w] - pl[i], bw = pb[i + w] - pb[i];
        if (lw > 0 && bw > 0) { double r = lw / bw; if (r > best) best = r; }
    }
    return best > 0 ? 10.0 * log10(best) : 0.0;
}

/* Prominence: the window's smoothed live peak against the mean live level
 * of up to n/2 bins on each side of it (whatever the frame has). */
static double window_prominence_db(const double *pl, uint32_t live_n, uint32_t start, uint32_t n)
{
    uint32_t w = n < SNR_SMOOTH_BINS ? n : SNR_SMOOTH_BINS, half = n / 2;
    double peak = 0.0;
    for (uint32_t i = start; i + w <= start + n; i++) {
        double lw = (pl[i + w] - pl[i]) / w;
        if (lw > peak) peak = lw;
    }
    uint32_t l0 = start > half ? start - half : 0, l1 = start;
    uint32_t r0 = start + n, r1 = r0 + half < live_n ? r0 + half : live_n;
    double sum = (pl[l1] - pl[l0]) + (pl[r1] - pl[r0]);
    uint32_t cnt = (l1 - l0) + (r1 - r0);
    if (cnt == 0 || sum <= 0 || peak <= 0) return 0.0;
    return 10.0 * log10(peak / (sum / cnt));
}

/* Per-template best hit for one frame. `resid` is the baseline-subtracted
 * frame scaled so max(resid) == 1 (cosine similarity is scale invariant;
 * the scaling keeps the double prefix sums well conditioned). `live` and
 * `base` are the (spur-masked) live and baseline power spectra, for the
 * window SNR and prominence. Templates must have been prepared for this
 * frame (templates_prepare()). */
static void classify_all(const float *resid, const float *live, const float *base, uint32_t live_n,
                         const struct templates *t, const struct frame_features *ff,
                         double snr_min_db, struct match_result *hits)
{
    double snr_lin = pow(10.0, snr_min_db / 10.0);
    /* prefix sums: resid, resid^2, live, base */
    double *ps  = malloc((live_n + 1) * sizeof(double));
    double *ps2 = malloc((live_n + 1) * sizeof(double));
    double *pl  = malloc((live_n + 1) * sizeof(double));
    double *pb  = malloc((live_n + 1) * sizeof(double));
    if (!ps || !ps2 || !pl || !pb) { free(ps); free(ps2); free(pl); free(pb); return; }
    ps[0] = ps2[0] = pl[0] = pb[0] = 0.0;
    for (uint32_t i = 0; i < live_n; i++) {
        ps[i + 1]  = ps[i]  + resid[i];
        ps2[i + 1] = ps2[i] + (double)resid[i] * resid[i];
        pl[i + 1]  = pl[i]  + live[i];
        pb[i + 1]  = pb[i]  + base[i];
    }

    for (uint32_t ti = 0; ti < t->count; ti++) {
        const struct template *tpl = &t->items[ti];
        struct match_result best = { .found = false, .score = -2.0 };
        double gate = template_gate_factor(tpl, ff);
        uint32_t tn = tpl->live_n;
        if (tn > live_n) tn = live_n;

        /* Only windows whose mean power clears the SNR gate are candidates:
         * otherwise a narrow template wins the cosine race on a noise spike
         * (0.99 against an 8-bin blip) and the real emitter under a wider
         * template is never considered. */
        uint32_t best_start = 0; double best_cos = -2.0;
        for (uint32_t start = 0; start + tn <= live_n; start++) {
            double bw = pb[start + tn] - pb[start];
            if (bw <= 0 || pl[start + tn] - pl[start] < bw * snr_lin) continue;
            double S = ps[start + tn] - ps[start];
            double E = ps2[start + tn] - ps2[start];
            double var = E - S * S / (double)tn;      /* sum of squares after mean removal */
            if (var < 1e-12) continue;                /* flat window: nothing to match */
            double c = (double)dot_f32(resid + start, tpl->live, tn) / sqrt(var);
            if (c > best_cos) { best_cos = c; best_start = start; }
        }
        if (best_cos > -2.0) {
            best.found = true;
            best.score = best_cos * gate;
            best.bin_offset = (int32_t)best_start;
            best.bandwidth_bins = tn;
            best.snr_db = window_snr_db(pl, pb, best_start, tn);
            best.prom_db = tn < live_n ? window_prominence_db(pl, live_n, best_start, tn) : 0.0;
        }
        if (best.score < 0.0) best.score = 0.0;
        if (best.score > 1.0) best.score = 1.0;
        best.template_idx = ti;
        hits[ti] = best;
    }
    free(ps); free(ps2); free(pl); free(pb);
}

/* ------------------------------------------------------------------ */
/* App state (moved ahead of publish_result below, which needs the full
 * definition for its hysteresis bookkeeping — the WebSocket client code
 * further down that owns the rest of this struct's fields follows later) */
/* ------------------------------------------------------------------ */
struct app {
    struct templates templates;
    const char *templates_path;
    double      threshold;
    double      hold_margin;
    double      min_interval_s;
    bool        verbose;

    /* Hysteresis state: without this, a score that hovers right at
     * `threshold` flips found/unknown on essentially every ~1s publish
     * cycle (each cycle is evaluated from scratch, with no memory of the
     * last one) — visible on the Dashboard as the label chip and the
     * reference-match panel blinking in and out once a second. Once
     * locked onto a template, we only drop back to "unknown" if the score
     * falls below (threshold - hold_margin), not merely below threshold. */
    bool        locked;
    uint32_t    locked_template_id;
    double      snr_min_db;      /* -s */
    uint32_t    shape_pub_id;    /* publish_shapes() bookkeeping */
    double      shape_pub_hz;
    uint32_t    shape_pub_live_n;
    double      prom_min_db;     /* -P; only applied to sliding (sub-span) windows */

    /* Background model (see header comment). bg is the temporal lower
     * envelope, base the receiver baseline derived from it, mask marks
     * spur bins; all bg_n bins. bg_frames counts frames folded in since
     * the last reset. */
    float      *bg;
    float      *base;
    uint8_t    *mask;
    uint32_t    bg_n;
    bool        bg_learned;      /* learning period finished, spur bookkeeping done */
    double      base_width_hz;   /* -w */
    double      spur_db;         /* -x */
    double      span_hz;         /* -S, or state/rx/span / sampling from MQTT */
    double      live_hz;         /* span_hz / bg_n, 0 when unknown */

    /* Spur map: per bin, how many distinct RX frequencies had it hot at
     * the end of learning (>= 2 means receiver spur) and the frequency
     * it was last counted at; a bin only counts again once the LO has
     * moved by more than a span, so revisiting a band or nudging the
     * tuning does not turn a station into a spur. */
    uint8_t    *spur_count;
    double     *spur_last_hz;
    uint32_t    spur_n;
    uint32_t    bg_frames;
    double      bg_t0;           /* now_s() at last reset */
    double      bg_last_t;       /* now_s() at last update */
    double      learn_s;         /* -l */
    double      bg_tau_s;        /* -a; 0 = frozen after learning */

    /* Set from libmosquitto's network thread (see mqtt_on_message), read
     * and cleared on the frame thread under `lock`. */
    pthread_mutex_t lock;
    bool        tune_changed;    /* an RX setting we subscribe to changed */
    bool        relearn;         /* cmd/classifier/background "learn" */
    double      span_hz_mqtt;    /* latest state/rx/span (or sampling) */
    char        freq_mqtt[64];   /* latest state/rx/frequency */

    /* Replay mode (-r): frames come from a file at a virtual clock. */
    const char *replay_path;
    uint32_t    replay_floats;
    double      replay_fps;
    double      replay_t;        /* virtual seconds */

    /* Rolling history of recent peak positions, for the two cross-cycle
     * features a single frame can't show on its own (see history_push() /
     * history_compute()): peak-bin drift (a genuinely sweeping/chirping
     * carrier) and duty cycle (a bursty/pulsed emitter). A fixed-size ring
     * covering FEATURE_HISTORY cycles at the ~1/s throttle rate below —
     * about 16s of history by default, plenty for both without unbounded
     * growth. */
    struct {
        double   t;             /* seconds, now_s() clock */
        double   peak_bin_frac; /* 0..1, scale-invariant across frame-size changes */
        bool     occupied;
    } history[16];
    uint32_t history_next;
    uint32_t history_filled;
    double   start_t;

    /* Per-connection message reassembly buffer (WS fragments can arrive
     * across multiple RECEIVE callbacks; browsers hide this, raw lws
     * doesn't) */
    uint8_t  *msg_buf;
    size_t    msg_len;
    size_t    msg_cap;

    double    last_run_t;        /* now_s() of the last classify cycle, 0 = never */
    volatile bool running;
    volatile bool connected;   /* set true on ESTABLISHED, false on CLOSED/ERROR */
};

#define FEATURE_HISTORY ((uint32_t)(sizeof(((struct app *)0)->history) / sizeof(((struct app *)0)->history[0])))

static struct app A;

/* Monotonic seconds, or the virtual replay clock in -r mode so a recorded
 * capture processes at full speed with the same throttle/learning/
 * adaptation timing it would have had live. */
static double now_s(void)
{
    if (A.replay_path) return A.replay_t;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ */
/* MQTT publish — one persistent libmosquitto connection for the whole
 * daemon lifetime (same library zynqrx and mqtt_pubd already link).
 * Earlier revisions forked a mosquitto_pub process per field, i.e. about
 * a dozen fork+exec+TCP handshakes per cycle: measured at ~22% of the
 * Zynq's CPU and ~13 forks/s on a SignalSDRPro, far more than the
 * correlation itself costs. It also passed the template label through a
 * shell. Now: QoS 0, retained (matching api_controller.sh's state/
 * convention, so a late-connecting Dashboard gets the last result
 * immediately), published from the lws thread while libmosquitto's own
 * network thread (mosquitto_loop_start) handles I/O and reconnects. */
/* ------------------------------------------------------------------ */
static struct mosquitto *g_mosq;
static bool g_mqtt_verbose;

#define TOPIC_PREFIX "state/classifier/"

static void mqtt_pub(const char *topic, const char *value)
{
    if (!g_mosq) { if (A.replay_path) printf("%.1f " TOPIC_PREFIX "%s %s\n", A.replay_t, topic, value); return; }
    char full[128];
    snprintf(full, sizeof(full), TOPIC_PREFIX "%s", topic);
    int rc = mosquitto_publish(g_mosq, NULL, full, (int)strlen(value), value, 0, true);
    if (rc != MOSQ_ERR_SUCCESS && g_mqtt_verbose)
        fprintf(stderr, "mqtt: publish %s failed: %s\n", full, mosquitto_strerror(rc));
}

/* RX settings whose change invalidates the learned background. These are
 * api_controller.sh's retained state/ topics; the retained value arrives
 * right after subscribing and seeds the table, only a later *different*
 * value counts as a retune. */
static struct { const char *topic; char value[64]; bool have; } RX_WATCH[] = {
    { "state/rx/frequency", "", false },
    { "state/rx/sampling",  "", false },
    { "state/rx/span",      "", false },
    { "state/rx/bandwidth", "", false },
    { "state/rx/gain",      "", false },
    { "state/rx/gain_mode", "", false },
    { "state/rx/rfinput",   "", false },
};
#define RX_WATCH_N (sizeof(RX_WATCH) / sizeof(RX_WATCH[0]))
#define BACKGROUND_CMD "cmd/classifier/background"

static void mqtt_on_connect(struct mosquitto *m, void *ud, int rc)
{
    (void)ud;
    if (rc != 0) { fprintf(stderr, "classifier: MQTT connect refused: %s\n", mosquitto_connack_string(rc)); return; }
    fprintf(stderr, "classifier: connected to MQTT broker\n");
    for (size_t i = 0; i < RX_WATCH_N; i++) mosquitto_subscribe(m, NULL, RX_WATCH[i].topic, 0);
    mosquitto_subscribe(m, NULL, BACKGROUND_CMD, 0);
}

static void mqtt_on_message(struct mosquitto *m, void *ud, const struct mosquitto_message *msg)
{
    (void)m; (void)ud;
    if (!msg->topic) return;
    char val[64];
    int n = msg->payloadlen < (int)sizeof(val) - 1 ? msg->payloadlen : (int)sizeof(val) - 1;
    memcpy(val, msg->payload ? msg->payload : "", n); val[n] = '\0';

    pthread_mutex_lock(&A.lock);
    if (strcmp(msg->topic, BACKGROUND_CMD) == 0) {
        if (strcmp(val, "learn") == 0) A.relearn = true;
    } else {
        for (size_t i = 0; i < RX_WATCH_N; i++) {
            if (strcmp(msg->topic, RX_WATCH[i].topic) != 0) continue;
            if (RX_WATCH[i].have && strcmp(RX_WATCH[i].value, val) != 0) A.tune_changed = true;
            snprintf(RX_WATCH[i].value, sizeof(RX_WATCH[i].value), "%s", val);
            RX_WATCH[i].have = true;
            if (strcmp(msg->topic, "state/rx/span") == 0 ||
                (strcmp(msg->topic, "state/rx/sampling") == 0 && !RX_WATCH[2].have)) {
                double v = atof(val);
                if (v > 0) A.span_hz_mqtt = v;
            }
            if (strcmp(msg->topic, "state/rx/frequency") == 0)
                snprintf(A.freq_mqtt, sizeof(A.freq_mqtt), "%s", val);
            break;
        }
    }
    pthread_mutex_unlock(&A.lock);
}

/* Published as the broker's last-will if the daemon dies without a clean
 * shutdown, and explicitly by main() on SIGTERM — otherwise the retained
 * label would outlive the daemon and the Dashboard would show a stale
 * match forever. */
#define OFFLINE_RESULT "{\"label\":\"unknown\",\"confidence\":0,\"online\":false}"

static int mqtt_init(const char *host, int port, bool verbose)
{
    g_mqtt_verbose = verbose;
    mosquitto_lib_init();
    g_mosq = mosquitto_new("tezuka_classifier", true, NULL);
    if (!g_mosq) { fprintf(stderr, "classifier: mosquitto_new failed\n"); return -1; }
    mosquitto_connect_callback_set(g_mosq, mqtt_on_connect);
    mosquitto_message_callback_set(g_mosq, mqtt_on_message);
    mosquitto_reconnect_delay_set(g_mosq, 1, 10, true);
    mosquitto_will_set(g_mosq, TOPIC_PREFIX "result", (int)strlen(OFFLINE_RESULT), OFFLINE_RESULT, 0, true);
    int rc = mosquitto_connect_async(g_mosq, host, port, 60);
    if (rc != MOSQ_ERR_SUCCESS)
        fprintf(stderr, "classifier: MQTT %s:%d not reachable yet (%s), will keep retrying\n",
                host, port, mosquitto_strerror(rc));
    if (mosquitto_loop_start(g_mosq) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "classifier: mosquitto_loop_start failed\n");
        mosquitto_destroy(g_mosq); g_mosq = NULL; mosquitto_lib_cleanup();
        return -1;
    }
    return 0;
}

static void mqtt_shutdown(void)
{
    if (!g_mosq) return;
    mqtt_pub("label", "unknown");
    mqtt_pub("confidence", "0.0");
    mqtt_pub("result", OFFLINE_RESULT);
    /* give the network thread a moment to flush the three retained messages */
    usleep(200 * 1000);
    mosquitto_disconnect(g_mosq);
    mosquitto_loop_stop(g_mosq, false);
    mosquitto_destroy(g_mosq);
    g_mosq = NULL;
    mosquitto_lib_cleanup();
}

/* Labels come from templates.bin, which cmd/classifier/templates lets any
 * MQTT client replace. They only ever travel as MQTT payload now, but keep
 * them printable ASCII so they are safe to embed in the JSON result and
 * harmless to display. */
static void sanitize_label(char *label, size_t n)
{
    for (size_t i = 0; i < n && label[i]; i++) {
        unsigned char c = (unsigned char)label[i];
        if (c < 0x20 || c > 0x7e || c == '"' || c == '\\') label[i] = '?';
    }
}

/* live_n is published alongside bin_offset/bandwidth_bins rather than
 * assumed shared knowledge: it's what those two are measured in units
 * of, and publishing it removes any need for a subscriber (the
 * Dashboard) to assume its own /waterfall connection saw the exact same
 * frame size the daemon did at this instant.
 *
 * Decision: a hit is acceptable when its score clears `threshold` AND its
 * window sits at least snr_min_db above the background. Hysteresis
 * (a->locked / a->locked_template_id): the held template keeps the label
 * while it stays above (threshold - hold_margin); a *different* template
 * only takes over when it beats the held one's score by hold_margin, so
 * two templates trading 0.72/0.75 no longer flip the label every cycle. */
/* "[v0,v1,...]" with 4 decimals, malloc'd; NULL on allocation failure. */
static char *json_floats(const float *v, uint32_t n)
{
    char *out = malloc((size_t)n * 12 + 3);
    if (!out) return NULL;
    size_t pos = 0;
    out[pos++] = '[';
    for (uint32_t i = 0; i < n; i++)
        pos += sprintf(out + pos, "%s%.4f", i ? "," : "", v[i]);
    out[pos++] = ']'; out[pos] = '\0';
    return out;
}

/* The matched template on the live grid (republished when the match or
 * the grid changes) and the normalized live window it matched (every
 * match cycle), so the Dashboard's reference panel can overlay exactly
 * what the daemon compared. Both zero-mean / unit-L2-norm. */
static void publish_shapes(struct app *a, const struct template *tpl, const struct templates *t,
                           const float *resid, const struct match_result *m)
{
    uint32_t tn = tpl->live_n;
    if (tn > 1024) return;   /* a whole-span v1 template: too big for the live channel */
    if (a->shape_pub_id != tpl->id || a->shape_pub_hz != t->prepared_hz || a->shape_pub_live_n != t->prepared_live_n) {
        char *data = json_floats(tpl->live, tn);
        if (data) {
            size_t len = strlen(data) + 128;
            char *msg = malloc(len);
            if (msg) {
                snprintf(msg, len, "{\"id\":%u,\"label\":\"%s\",\"bins\":%u,\"hz_per_bin\":%.1f,\"data\":%s}",
                         tpl->id, tpl->label, tn, tpl->hz_per_bin, data);
                mqtt_pub("template_shape", msg);
                free(msg);
            }
            free(data);
        }
        a->shape_pub_id = tpl->id; a->shape_pub_hz = t->prepared_hz; a->shape_pub_live_n = t->prepared_live_n;
    }
    if (!resid || m->bin_offset < 0) return;
    float *w = malloc(tn * sizeof(float));
    if (!w) return;
    memcpy(w, resid + m->bin_offset, tn * sizeof(float));
    if (normalize(w, tn)) {
        char *data = json_floats(w, tn);
        if (data) {
            size_t len = strlen(data) + 64;
            char *msg = malloc(len);
            if (msg) {
                snprintf(msg, len, "{\"id\":%u,\"bins\":%u,\"data\":%s}", tpl->id, tn, data);
                mqtt_pub("window", msg);
                free(msg);
            }
            free(data);
        }
    }
    free(w);
}

static void publish_result(struct app *a, const struct match_result *hits, const struct templates *t,
                           uint32_t live_n, const char *state, double bg_age_s, const float *resid)
{
    char buf[256];
    double thr = a->threshold, m = a->hold_margin;

    /* Rank only the hits that clear the SNR and prominence gates; a
     * template whose best window is a gated-out blip must not shadow
     * another template's genuine hit. */
    const struct match_result *best = NULL, *second = NULL, *held = NULL;
    for (uint32_t i = 0; i < t->count; i++) {
        const struct match_result *h = &hits[i];
        if (!h->found) continue;
        if (h->snr_db < a->snr_min_db) continue;
        if (h->bandwidth_bins < live_n && h->prom_db < a->prom_min_db) continue;
        if (!best || h->score > best->score) { second = best; best = h; }
        else if (!second || h->score > second->score) second = h;
        if (a->locked && t->items[i].id == a->locked_template_id) held = h;
    }
    double margin = (best && second) ? best->score - second->score : (best ? best->score : 0.0);

    const struct match_result *chosen = NULL;
    bool held_ok = held && held->score >= thr - m;
    bool best_ok = best && best->score >= thr;
    if (held_ok) {
        chosen = held;
        if (best_ok && best != held && best->score >= held->score + m) chosen = best;
    } else if (best_ok) {
        chosen = best;
    }

    if (!chosen) {
        a->locked = false;
        mqtt_pub("label", "unknown");
        mqtt_pub("confidence", "0.0");
        snprintf(buf, sizeof(buf),
                 "{\"label\":\"unknown\",\"confidence\":0,\"best\":%.3f,\"best_snr_db\":%.1f,\"best_prom_db\":%.1f,"
                 "\"margin\":%.3f,\"state\":\"%s\",\"bg_age_s\":%.0f,\"online\":true}",
                 best ? best->score : 0.0, best ? best->snr_db : 0.0, best ? best->prom_db : 0.0,
                 margin, state, bg_age_s);
        mqtt_pub("result", buf);
        if (a->verbose) fprintf(stderr, "classify: unknown (best=%.3f snr=%.1f dB prom=%.1f dB %s)\n",
                                best ? best->score : 0.0, best ? best->snr_db : 0.0, best ? best->prom_db : 0.0, state);
        return;
    }

    const struct template *tpl = &t->items[chosen->template_idx];
    a->locked = true;
    a->locked_template_id = tpl->id;
    mqtt_pub("label", tpl->label);
    snprintf(buf, sizeof(buf), "%.3f", chosen->score);
    mqtt_pub("confidence", buf);
    snprintf(buf, sizeof(buf), "%d", chosen->bin_offset);
    mqtt_pub("freq_offset", buf);
    snprintf(buf, sizeof(buf), "%u", chosen->bandwidth_bins);
    mqtt_pub("bandwidth_bins", buf);
    snprintf(buf, sizeof(buf), "%u", live_n);
    mqtt_pub("frame_bins", buf);
    snprintf(buf, sizeof(buf), "%u", tpl->id);
    mqtt_pub("template_id", buf);

    /* The same fields as one message, so a subscriber sees a result
     * atomically instead of reassembling a burst (the Dashboard currently
     * debounces the per-field topics for exactly that reason). */
    snprintf(buf, sizeof(buf),
             "{\"label\":\"%s\",\"confidence\":%.3f,\"freq_offset\":%d,\"bandwidth_bins\":%u,"
             "\"frame_bins\":%u,\"template_id\":%u,\"snr_db\":%.1f,\"prom_db\":%.1f,\"margin\":%.3f,"
             "\"hz_per_bin\":%.1f,\"state\":\"%s\",\"bg_age_s\":%.0f,\"online\":true}",
             tpl->label, chosen->score, chosen->bin_offset, chosen->bandwidth_bins, live_n, tpl->id,
             chosen->snr_db, chosen->prom_db, margin, a->live_hz, state, bg_age_s);
    mqtt_pub("result", buf);
    publish_shapes(a, tpl, t, resid, chosen);

    if (a->verbose)
        fprintf(stderr, "classify: %s (id=%u) conf=%.3f snr=%.1f dB prom=%.1f dB margin=%.3f bin_offset=%d\n",
                tpl->label, tpl->id, chosen->score, chosen->snr_db, chosen->prom_db, margin, chosen->bin_offset);
}

/* Published every cycle regardless of match outcome — these describe the
 * live signal itself, not a match against a template, so they're useful
 * even when nothing matched ("unknown, but here's what we can tell about
 * it"). bandwidth is reported in bins (matching the existing
 * bandwidth_bins convention for an actual match) so the Dashboard can
 * convert it to Hz with the same math it already has. */
static void publish_features(const struct frame_features *ff, uint32_t live_n)
{
    char buf[64];
    if (!ff->valid) {
        mqtt_pub("feature_bandwidth_bins", "0");
        mqtt_pub("feature_papr_db", "0.0");
        mqtt_pub("feature_flatness", "0.0");
        mqtt_pub("feature_peak_count", "0");
        return;
    }
    snprintf(buf, sizeof(buf), "%u", (uint32_t)(ff->bandwidth_frac * live_n + 0.5));
    mqtt_pub("feature_bandwidth_bins", buf);
    snprintf(buf, sizeof(buf), "%.2f", ff->papr_db);
    mqtt_pub("feature_papr_db", buf);
    snprintf(buf, sizeof(buf), "%.4f", ff->flatness);
    mqtt_pub("feature_flatness", buf);
    snprintf(buf, sizeof(buf), "%u", (uint32_t)ff->peak_count);
    mqtt_pub("feature_peak_count", buf);
}

/* "n/a" (not a number the Dashboard should try to parse) until the history
 * ring has enough samples — see history_compute(). */
static void publish_temporal(const struct temporal_features *tf)
{
    char buf[64];
    if (tf->duty_valid) {
        snprintf(buf, sizeof(buf), "%.1f", tf->duty_cycle_pct);
        mqtt_pub("feature_duty_cycle_pct", buf);
    } else {
        mqtt_pub("feature_duty_cycle_pct", "n/a");
    }
    if (tf->drift_valid) {
        snprintf(buf, sizeof(buf), "%.3f", tf->drift_bins_per_s);
        mqtt_pub("feature_drift_bins_s", buf);
    } else {
        mqtt_pub("feature_drift_bins_s", "n/a");
    }
}

/* ------------------------------------------------------------------ */
/* WebSocket client                                                    */
/* ------------------------------------------------------------------ */

static void ensure_msg_cap(struct app *a, size_t need)
{
    if (a->msg_cap >= need) return;
    size_t newcap = a->msg_cap ? a->msg_cap * 2 : 65536;
    while (newcap < need) newcap *= 2;
    a->msg_buf = realloc(a->msg_buf, newcap);
    a->msg_cap = newcap;
}


/* A frame counts as "occupied" (something standing out from the noise
 * floor, not just thermal noise) once the residual's peak-to-average
 * ratio clears this floor. Measured on real captures with power units:
 * the residual of an empty band sits at 9.5-10.5 dB (clipping at zero
 * makes the mean small), BLE/Wi-Fi fragments at 15-20 dB, a broadcast
 * band at 24 dB. The old value of 6 dB, set when the ratio was being
 * computed in doubled dB, was below the noise and marked every frame
 * occupied. */
#define OCCUPIED_PAPR_DB 14.0

/* Minimum ring-buffer occupancy/time-span before history_compute() will
 * report an actual number instead of "n/a" — avoids a spurious slope
 * estimate from 2-3 samples right after (re)start. */
#define HISTORY_MIN_SAMPLES 4
#define HISTORY_MIN_SPAN_S  3.0

static void history_push(struct app *a, double t, double peak_bin_frac, bool occupied)
{
    uint32_t idx = a->history_next % FEATURE_HISTORY;
    a->history[idx].t             = t;
    a->history[idx].peak_bin_frac = peak_bin_frac;
    a->history[idx].occupied      = occupied;
    a->history_next++;
    if (a->history_filled < FEATURE_HISTORY) a->history_filled++;
}

/* Regression runs in fractional-bin space (0..1), not raw bin indices —
 * that stays valid even if live_n happened to change mid-history (e.g. a
 * /waterfall FFT-size change), then gets scaled to bins/s using the
 * *current* live_n only at the end, for the reader. */
static struct temporal_features history_compute(const struct app *a, uint32_t live_n)
{
    struct temporal_features out;
    memset(&out, 0, sizeof(out));
    uint32_t n = a->history_filled;
    if (n < HISTORY_MIN_SAMPLES) return out;

    double t0 = a->history[0].t;
    double sum_t = 0, sum_y = 0, sum_tt = 0, sum_ty = 0;
    double t_min = 1e300, t_max = -1e300;
    uint32_t occupied_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        double t = a->history[i].t - t0;
        double y = a->history[i].peak_bin_frac;
        sum_t += t; sum_y += y; sum_tt += t * t; sum_ty += t * y;
        if (t < t_min) t_min = t;
        if (t > t_max) t_max = t;
        if (a->history[i].occupied) occupied_count++;
    }

    out.duty_valid     = true;
    out.duty_cycle_pct = 100.0 * occupied_count / n;

    double span = t_max - t_min;
    if (span >= HISTORY_MIN_SPAN_S) {
        double denom = (double)n * sum_tt - sum_t * sum_t;
        if (fabs(denom) > 1e-9) {
            double slope_frac_per_s = ((double)n * sum_ty - sum_t * sum_y) / denom;
            out.drift_valid       = true;
            out.drift_bins_per_s  = slope_frac_per_s * (double)live_n;
        }
    }
    return out;
}

static void background_reset(struct app *a, uint32_t live_n, const char *why)
{
    free(a->bg); free(a->base); free(a->mask);
    a->bg   = calloc(live_n, sizeof(float));
    a->base = calloc(live_n, sizeof(float));
    a->mask = calloc(live_n, sizeof(uint8_t));
    a->bg_n = live_n;
    a->bg_learned = false;
    a->bg_frames = 0;
    a->bg_t0 = a->bg_last_t = now_s();
    a->locked = false;
    a->history_filled = 0; a->history_next = 0;
    if (a->verbose) fprintf(stderr, "background: reset (%s), learning for %.0f s\n", why, a->learn_s);
}

/* Fold one raw frame into the background lower-envelope tracker. A bin
 * below the estimate pulls it down with BG_ALPHA_DOWN per frame; a bin
 * above it pushes it up slowly: during learning at a rate that settles
 * within learn_s, afterwards with time constant bg_tau_s (0 = never).
 * Because noise is chi-square distributed around its mean, the envelope
 * sits a little below the noise mean, which is why the SNR gate, not a
 * zero residual, is what rejects an empty band. */
#define BG_ALPHA_DOWN 0.3
static void background_update(struct app *a, const float *live, uint32_t live_n)
{
    double t = now_s();
    if (!a->bg || a->bg_n != live_n) background_reset(a, live_n, "frame size");
    if (!a->bg) return;
    double dt = t - a->bg_last_t;
    double up;
    if (a->bg_frames == 0) { memcpy(a->bg, live, live_n * sizeof(float)); a->bg_frames = 1; a->bg_last_t = t; return; }
    if (t - a->bg_t0 < a->learn_s) up = dt / (a->learn_s / 4.0);           /* settles in ~learn_s */
    else if (a->bg_tau_s > 0)       up = dt / a->bg_tau_s;
    else                            up = 0.0;
    if (up > 1.0) up = 1.0;
    float fu = (float)up, fd = (float)BG_ALPHA_DOWN;
    for (uint32_t i = 0; i < live_n; i++) {
        float d = live[i] - a->bg[i];
        a->bg[i] += (d < 0.0f ? fd : fu) * d;
    }
    a->bg_frames++;
    a->bg_last_t = t;
}

/* Receiver baseline: morphological opening of the envelope (rolling
 * minimum then rolling mean, both over 2*half+1 bins). Smooth structure
 * wider than the element survives, narrower lines are removed. */
static void baseline_compute(const float *env, float *base, uint32_t n, uint32_t half)
{
    float *mn = malloc(n * sizeof(float));
    if (!mn) { memcpy(base, env, n * sizeof(float)); return; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t lo = i > half ? i - half : 0, hi = i + half < n ? i + half : n - 1;
        float m = env[lo];
        for (uint32_t j = lo + 1; j <= hi; j++) if (env[j] < m) m = env[j];
        mn[i] = m;
    }
    double *pre = malloc((n + 1) * sizeof(double));
    if (!pre) { memcpy(base, mn, n * sizeof(float)); free(mn); return; }
    pre[0] = 0.0;
    for (uint32_t i = 0; i < n; i++) pre[i + 1] = pre[i] + mn[i];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t lo = i > half ? i - half : 0, hi = i + half + 1 < n ? i + half + 1 : n;
        base[i] = (float)((pre[hi] - pre[lo]) / (hi - lo));
    }
    free(pre); free(mn);
}

/* Called once when the learning period ends: build the spur mask. The
 * centre bins are always masked (DC); other bins hot in the envelope are
 * counted against the RX frequency, and bins hot on >= 2 frequencies are
 * masked as receiver spurs. */
static void background_learned(struct app *a, const char *freq)
{
    uint32_t n = a->bg_n;
    if (a->spur_count && a->spur_n != n) {
        free(a->spur_count); free(a->spur_last_hz);
        a->spur_count = NULL; a->spur_last_hz = NULL;
    }
    if (!a->spur_count) {
        a->spur_count = calloc(n, sizeof(uint8_t));
        a->spur_last_hz = calloc(n, sizeof(double));
        a->spur_n = n;
    }
    double fhz = freq[0] ? atof(freq) : 0.0;
    double far = a->span_hz > 0 ? a->span_hz : 1e12;   /* unknown span: never recount */
    double thr = pow(10.0, a->spur_db / 10.0);
    uint32_t hot = 0, masked = 0;
    for (uint32_t i = 0; i < n; i++) {
        bool is_hot = a->base[i] > 0 && a->bg[i] > a->base[i] * thr;
        if (is_hot) hot++;
        if (is_hot && fhz > 0 && a->spur_count && a->spur_count[i] < 255 &&
            (a->spur_last_hz[i] == 0.0 || fabs(fhz - a->spur_last_hz[i]) > far)) {
            a->spur_count[i]++;
            a->spur_last_hz[i] = fhz;
        }
        a->mask[i] = (a->spur_count && a->spur_count[i] >= 2) ? 1 : 0;
        if (i + 2 >= n / 2 && i <= n / 2 + 2) a->mask[i] = 1;   /* DC */
        if (a->mask[i]) masked++;
    }
    a->bg_learned = true;
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"frequency\":\"%s\",\"bins\":%u,\"hz_per_bin\":%.1f,\"hot\":%u,\"masked\":%u}",
             freq[0] ? freq : "", n, a->live_hz, hot, masked);
    mqtt_pub("background", buf);
    if (a->verbose) fprintf(stderr, "background: learned (%s), %u hot bins, %u masked\n",
                            freq[0] ? freq : "no frequency", hot, masked);
}

static void handle_frame(struct app *a, const float *f, size_t n_floats)
{
    if (n_floats < 2) return; /* need at least the step index + 1 bin */
    uint32_t live_n = (uint32_t)(n_floats - 1);
    const float *live = f + 1;

    /* Retune or explicit relearn requested from the MQTT thread? */
    bool reset = false; const char *why = "";
    char freq[64];
    pthread_mutex_lock(&a->lock);
    if (a->tune_changed) { reset = true; why = "RX settings changed"; a->tune_changed = false; }
    if (a->relearn)      { reset = true; why = "cmd/classifier/background learn"; a->relearn = false; }
    if (a->span_hz_mqtt > 0) a->span_hz = a->span_hz_mqtt;
    snprintf(freq, sizeof(freq), "%s", a->freq_mqtt);
    pthread_mutex_unlock(&a->lock);
    if (reset) background_reset(a, live_n, why);
    a->live_hz = a->span_hz > 0 ? a->span_hz / live_n : 0.0;

    /* The background sees every frame (cheap: one multiply-add per bin);
     * only the correlate+publish cycle below is throttled. */
    background_update(a, live, live_n);

    double t_now = now_s();
    if (a->last_run_t != 0.0 && t_now - a->last_run_t < a->min_interval_s)
        return;
    a->last_run_t = t_now;

    double bg_age = t_now - a->bg_t0;
    bool learning = bg_age < a->learn_s;

    /* Baseline from the envelope (once per cycle), spur mask at the end
     * of learning. */
    uint32_t half = 1;
    if (a->live_hz > 0) half = (uint32_t)(a->base_width_hz / a->live_hz / 2.0 + 0.5);
    else half = live_n / 16;
    if (half < 1) half = 1;
    if (half > live_n / 2) half = live_n / 2;
    baseline_compute(a->bg, a->base, live_n, half);
    if (!learning && !a->bg_learned) background_learned(a, freq);
    templates_prepare(&a->templates, a->live_hz, live_n);

    /* Masked live: spur bins pinned to the baseline so they count as
     * empty in the residual, SNR and prominence. */
    float *lv = malloc(live_n * sizeof(float));
    float *resid = malloc(live_n * sizeof(float));
    if (!lv || !resid) { free(lv); free(resid); return; }
    float peak = 0.0f;
    for (uint32_t i = 0; i < live_n; i++) {
        lv[i] = a->mask[i] ? a->base[i] : live[i];
        float r = lv[i] - a->base[i];
        resid[i] = r > 0.0f ? r : 0.0f;
        if (resid[i] > peak) peak = resid[i];
    }
    if (peak > 0.0f) for (uint32_t i = 0; i < live_n; i++) resid[i] /= peak;

    /* Features describe the residual, i.e. whatever is on air beyond the
     * learned background, not the receiver's own passband. Published
     * every cycle whether or not anything matches. */
    struct frame_features ff = extract_frame_features(resid, live_n);
    publish_features(&ff, live_n);

    double peak_bin_frac = ff.valid && live_n > 1 ? (double)ff.peak_bin / (double)(live_n - 1) : 0.0;
    bool occupied = ff.valid && ff.papr_db >= OCCUPIED_PAPR_DB;
    history_push(a, t_now, peak_bin_frac, occupied);
    struct temporal_features tf = history_compute(a, live_n);
    publish_temporal(&tf);

    if (learning || !a->templates.items || a->templates.count == 0 || peak <= 0.0f) {
        struct match_result none = { .found = false };
        publish_result(a, &none, &(struct templates){ .count = 0 }, live_n,
                       learning ? "learning" : "idle", bg_age, NULL);
        free(resid); free(lv);
        return;
    }

    struct match_result *hits = calloc(a->templates.count, sizeof(*hits));
    if (hits) {
        classify_all(resid, lv, a->base, live_n, &a->templates, &ff, a->snr_min_db, hits);
        publish_result(a, hits, &a->templates, live_n, "ok", bg_age, resid);
        free(hits);
    }
    free(resid); free(lv);
}

/* -r FILE: replay raw float32 frames (the exact /waterfall payload layout,
 * replay_floats per frame) at a virtual clock of replay_fps, then exit.
 * With -B none nothing is published, results go to stdout. */
static int replay_file(struct app *a)
{
    FILE *f = fopen(a->replay_path, "rb");
    if (!f) { perror(a->replay_path); return 1; }
    size_t n = a->replay_floats;
    float *frame = malloc(n * sizeof(float));
    if (!frame) { fclose(f); return 1; }
    size_t count = 0;
    while (fread(frame, sizeof(float), n, f) == n) {
        a->replay_t = count / a->replay_fps;
        handle_frame(a, frame, n);
        count++;
    }
    fprintf(stderr, "replay: %zu frames processed\n", count);
    free(frame);
    fclose(f);
    return 0;
}

static int ws_client_cb(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    (void)user;
    struct app *a = &A;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        fprintf(stderr, "classifier: connected to /waterfall\n");
        a->msg_len = 0;
        a->connected = true;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        ensure_msg_cap(a, a->msg_len + len);
        memcpy(a->msg_buf + a->msg_len, in, len);
        a->msg_len += len;

        if (lws_is_final_fragment(wsi)) {
            if (a->msg_len >= sizeof(float) * 2 && (a->msg_len % sizeof(float)) == 0)
                handle_frame(a, (const float *)a->msg_buf, a->msg_len / sizeof(float));
            a->msg_len = 0;
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        fprintf(stderr, "classifier: connection error: %s\n", in ? (char *)in : "?");
        a->connected = false;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        fprintf(stderr, "classifier: /waterfall connection closed, will retry\n");
        a->connected = false;
        break;

    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "waterfall-client", ws_client_cb, 0, 0, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

static void on_signal(int s)
{
    (void)s;
    A.running = false;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -H HOST   maia-httpd host        (default: localhost)\n"
        "  -p PORT   maia-httpd port        (default: 80)\n"
        "  -P PATH   waterfall WS path      (default: /waterfall)\n"
        "  -f FILE   templates file         (default: /mnt/jffs2/classifier/templates.bin)\n"
        "  -B HOST   MQTT broker host       (default: localhost)\n"
        "  -q PORT   MQTT broker port       (default: 1883)\n"
        "  -c THRESH match confidence floor (default: 0.6)\n"
        "  -m MARGIN hysteresis hold margin (default: 0.15; holds a lock until\n"
        "            score < THRESH - MARGIN, so a borderline score doesn't\n"
        "            flip found/unknown every cycle)\n"
        "  -i SEC    min correlate interval (default: 1.0)\n"
        "  -S HZ     frame span in Hz when MQTT cannot supply it (replay)\n"
        "  -w HZ     receiver-baseline structuring element width (default: 4e6)\n"
        "  -x DB     spur candidate threshold over the baseline (default: 8)\n"
        "  -s DB     min window SNR over background (default: 6)\n"
        "  -g DB     min prominence over the window flanks (default: 4)\n"
        "  -l SEC    background learning time (default: 10)\n"
        "  -a SEC    background adaptation time constant (default: 300, 0 = frozen)\n"
        "  -r FILE   replay raw float32 frames from FILE (offline test)\n"
        "  -n N      floats per replayed frame (default: 4096)\n"
        "  -F FPS    virtual frame rate for replay (default: 7.5)\n"
        "  -v        verbose\n"
        "  -h        this help\n", prog);
}

int main(int argc, char **argv)
{
    const char *host = "localhost";
    int port = 80;
    const char *path = "/waterfall";
    const char *mqtt_host = "localhost";
    int mqtt_port = 1883;
    A.templates_path = "/mnt/jffs2/classifier/templates.bin";
    A.threshold       = 0.6;
    A.hold_margin     = 0.15;
    A.min_interval_s  = 1.0;
    A.snr_min_db      = 6.0;
    A.base_width_hz   = 4e6;
    A.spur_db         = 8.0;
    A.prom_min_db     = 4.0;
    A.learn_s         = 10.0;
    A.bg_tau_s        = 300.0;
    A.replay_floats   = 4096;
    A.replay_fps      = 7.5;
    A.verbose         = false;
    A.locked          = false;
    pthread_mutex_init(&A.lock, NULL);

    int opt;
    while ((opt = getopt(argc, argv, "H:p:P:f:B:q:c:m:i:s:g:l:a:r:n:F:S:w:x:vh")) != -1) {
        switch (opt) {
        case 'H': host              = optarg;      break;
        case 'p': port              = atoi(optarg); break;
        case 'P': path              = optarg;      break;
        case 'f': A.templates_path  = optarg;      break;
        case 'B': mqtt_host         = optarg;      break;
        case 'q': mqtt_port         = atoi(optarg); break;
        case 'c': A.threshold       = atof(optarg); break;
        case 'm': A.hold_margin     = atof(optarg); break;
        case 'i': A.min_interval_s  = atof(optarg); break;
        case 's': A.snr_min_db      = atof(optarg); break;
        case 'S': A.span_hz         = atof(optarg); break;
        case 'w': A.base_width_hz   = atof(optarg); break;
        case 'x': A.spur_db         = atof(optarg); break;
        case 'g': A.prom_min_db     = atof(optarg); break;
        case 'l': A.learn_s         = atof(optarg); break;
        case 'a': A.bg_tau_s        = atof(optarg); break;
        case 'r': A.replay_path     = optarg;      break;
        case 'n': A.replay_floats   = (uint32_t)atoi(optarg); break;
        case 'F': A.replay_fps      = atof(optarg); break;
        case 'v': A.verbose         = true;         break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    A.start_t = now_s();

    if (strcmp(mqtt_host, "none") != 0 && mqtt_init(mqtt_host, mqtt_port, A.verbose) != 0) return 1;

    if (templates_load(A.templates_path, &A.templates) != 0)
        fprintf(stderr, "classifier: no templates loaded yet (%s) — will publish \"unknown\" until "
                         "cmd/classifier/templates writes one and the daemon is restarted\n",
                A.templates_path);

    if (A.replay_path) {
        int rc = replay_file(&A);
        mqtt_shutdown();
        templates_free(&A.templates);
        free(A.bg); free(A.base); free(A.mask); free(A.spur_count); free(A.spur_last_hz);
        return rc;
    }

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid       = -1;
    info.uid       = -1;

    struct lws_context *ctx = lws_create_context(&info);
    if (!ctx) { fprintf(stderr, "classifier: lws context create failed\n"); return 1; }

    A.running = true;

    while (A.running) {
        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        ccinfo.context  = ctx;
        ccinfo.address  = host;
        ccinfo.port     = port;
        ccinfo.path     = path;
        ccinfo.host     = host;
        ccinfo.origin   = host;
        ccinfo.protocol = protocols[0].name;
        ccinfo.pwsi     = NULL;

        A.connected = false;
        struct lws *wsi = lws_client_connect_via_info(&ccinfo);
        if (!wsi) {
            fprintf(stderr, "classifier: connect failed, retrying in 5s\n");
            sleep(5);
            continue;
        }

        /* Pump the event loop until ESTABLISHED flips connected true, or
         * we give up and retry. Once connected, keep pumping until
         * CLOSED/CONNECTION_ERROR flips it back false — that's the
         * signal to break out and reconnect. */
        double connect_start = now_s();
        bool ever_connected = false;
        while (A.running) {
            lws_service(ctx, 200);
            if (A.connected) ever_connected = true;
            if (ever_connected && !A.connected) break;      /* dropped: reconnect */
            if (!ever_connected && now_s() - connect_start > 5.0) break; /* handshake never completed */
        }
        if (A.running && !ever_connected) sleep(5);
    }

    mqtt_shutdown();
    templates_free(&A.templates);
    free(A.bg); free(A.base); free(A.mask); free(A.spur_count); free(A.spur_last_hz);
    free(A.msg_buf);
    lws_context_destroy(ctx);
    return 0;
}
