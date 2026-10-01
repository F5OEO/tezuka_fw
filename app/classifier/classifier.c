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
 * $(CC) -O2 -o classifier classifier.c -lwebsockets -lm
 *
 * Usage:
 * classifier [OPTIONS]
 * -H HOST   maia-httpd host          (default: localhost)
 * -p PORT   maia-httpd port          (default: 80)
 * -P PATH   waterfall WS path        (default: /waterfall)
 * -f FILE   templates file           (default: /mnt/jffs2/classifier/templates.bin)
 * -c THRESH match confidence floor   (default: 0.6, below this publishes "unknown")
 * -m MARGIN hysteresis hold margin   (default: 0.15 — once locked onto a
 *           template, only drops back to "unknown" below THRESH - MARGIN,
 *           so a borderline score doesn't flip found/unknown every cycle)
 * -i SEC    min seconds between correlate+publish cycles (default: 1.0)
 * -v        verbose (log every publish to stderr too)
 */

#define _GNU_SOURCE
#include <libwebsockets.h>
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

    /* version 2 only; has_gate is false (all ranges unbounded) for
     * version-1 files, which then behave exactly as before. */
    bool     has_gate;
    struct feature_range bandwidth_frac, papr_db, flatness, peak_count;
};

struct templates {
    uint32_t          canonical_n;
    uint32_t          count;
    struct template   *items;
};

static void templates_free(struct templates *t)
{
    if (!t->items) return;
    for (uint32_t i = 0; i < t->count; i++) free(t->items[i].data);
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
    if (fread(&version, sizeof(version), 1, f) != 1 || (version != 1 && version != 2)) {
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
 * once per cycle from the RAW linear-amplitude bins (before normalize()
 * mean-centers them, which would make these power ratios meaningless).
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

static struct frame_features extract_frame_features(const float *raw, uint32_t n)
{
    struct frame_features ff;
    memset(&ff, 0, sizeof(ff));
    if (n == 0) return ff;

    uint32_t peak_i = 0;
    double peak_amp = raw[0] > 0 ? raw[0] : 0.0;
    double sum_power = 0.0, sum_log_power = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double amp = raw[i] > 0 ? raw[i] : 0.0;
        double power = amp * amp;
        sum_power     += power;
        sum_log_power += log(power > 1e-20 ? power : 1e-20);
        if (amp > peak_amp) { peak_amp = amp; peak_i = i; }
    }
    if (peak_amp <= 0.0 || sum_power <= 0.0) return ff; /* leaves valid=false */

    double peak_power = peak_amp * peak_amp;
    double mean_power = sum_power / n;

    ff.papr_db  = 10.0 * log10(peak_power / (mean_power > 1e-20 ? mean_power : 1e-20));
    double geomean_power = exp(sum_log_power / n);
    ff.flatness = geomean_power / (mean_power > 1e-20 ? mean_power : 1e-20);
    if (ff.flatness > 1.0) ff.flatness = 1.0; /* numerical guard: should be <=1 by AM-GM */

    double bw_gate = peak_power * BW_GATE_RATIO;
    uint32_t lo = peak_i, hi = peak_i;
    while (lo > 0     && (double)raw[lo - 1] * raw[lo - 1] >= bw_gate) lo--;
    while (hi + 1 < n && (double)raw[hi + 1] * raw[hi + 1] >= bw_gate) hi++;
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
        double p = (double)raw[i] * raw[i];
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
};

/* Correlates `live` (already normalized, length live_n) against every
 * loaded template. If a template is narrower than the live frame, slides
 * it across all valid start positions and keeps the best-scoring one
 * (re-normalizing each window, since a raw sub-slice of an
 * already-whole-frame-normalized vector isn't itself unit-norm).
 *
 * `ff` is the live frame's own scalar features (see extract_frame_features)
 * — used only for gating a version-2 template's score (template_gate_factor),
 * the same frame-level ff for every candidate window rather than
 * recomputing per-window, which would be considerably more expensive for
 * a gate that's meant to be a coarse sanity check, not a second matcher. */
static struct match_result classify(const float *live, uint32_t live_n, const struct templates *t,
                                     const struct frame_features *ff)
{
    struct match_result best = { .found = false, .score = -2.0 };
    if (!t->items || t->count == 0) return best;

    uint32_t tn = t->canonical_n;
    float *window = NULL;
    if (tn < live_n) window = malloc(tn * sizeof(float));

    for (uint32_t ti = 0; ti < t->count; ti++) {
        const struct template *tpl = &t->items[ti];
        double gate = template_gate_factor(tpl, ff);

        if (tn >= live_n) {
            /* Template is at least as wide as the live frame: resample
             * the whole live frame down to canonical_n and compare once
             * (offset is meaningless in this case — whole-span match). */
            float *resampled = malloc(tn * sizeof(float));
            if (!resampled) continue;
            resample_linear(live, live_n, resampled, tn);
            if (normalize(resampled, tn)) {
                double s = cosine_sim(resampled, tpl->data, tn) * gate;
                if (s > best.score) {
                    best.found = true;
                    best.template_idx = ti;
                    best.score = s;
                    best.bin_offset = 0;
                    best.bandwidth_bins = live_n; /* covers the whole frame in this branch */
                }
            }
            free(resampled);
            continue;
        }

        /* Template narrower than the live frame: slide it across every
         * valid start position in bin space. O(live_n * tn) per
         * template — fine at the throttled ~1/s call rate this runs at. */
        for (uint32_t start = 0; start + tn <= live_n; start++) {
            memcpy(window, live + start, tn * sizeof(float));
            if (!normalize(window, tn)) continue;
            double s = cosine_sim(window, tpl->data, tn) * gate;
            if (s > best.score) {
                best.found = true;
                best.template_idx = ti;
                best.score = s;
                best.bin_offset = (int32_t)start;
                best.bandwidth_bins = tn; /* the sliding window's own width */
            }
        }
    }
    free(window);

    if (best.score < 0.0) best.score = 0.0;
    if (best.score > 1.0) best.score = 1.0;
    return best;
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

    /* Rolling history of recent peak positions, for the two cross-cycle
     * features a single frame can't show on its own (see history_push() /
     * history_compute()): peak-bin drift (a genuinely sweeping/chirping
     * carrier) and duty cycle (a bursty/pulsed emitter). A fixed-size ring
     * covering FEATURE_HISTORY cycles at the ~1/s throttle rate below —
     * about 16s of history by default, plenty for both without unbounded
     * growth. */
    struct {
        double   t;             /* seconds since start_time */
        double   peak_bin_frac; /* 0..1, scale-invariant across frame-size changes */
        bool     occupied;
    } history[16];
    uint32_t history_next;
    uint32_t history_filled;
    struct timespec start_time;

    /* Per-connection message reassembly buffer (WS fragments can arrive
     * across multiple RECEIVE callbacks; browsers hide this, raw lws
     * doesn't) */
    uint8_t  *msg_buf;
    size_t    msg_len;
    size_t    msg_cap;

    struct timespec last_run;
    volatile bool running;
    volatile bool connected;   /* set true on ESTABLISHED, false on CLOSED/ERROR */
};

#define FEATURE_HISTORY ((uint32_t)(sizeof(((struct app *)0)->history) / sizeof(((struct app *)0)->history[0])))

static struct app A;

/* ------------------------------------------------------------------ */
/* MQTT publish (shells out to mosquitto_pub, matching the convention
 * already used throughout board/tezuka/common/overlay_tezuka/root/
 * api_controller.sh — publishes here are throttled to ~1/s, so the
 * fork+exec overhead is a non-issue) */
/* ------------------------------------------------------------------ */
static void mqtt_pub(const char *topic, const char *value)
{
    char cmd[512];
    /* value is always daemon-generated (label text or a formatted
     * number), never untrusted input, so this is safe to shell out. */
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/mosquitto_pub -i tezuka_classifier -t 'state/classifier/%s' -m '%s' 2>/dev/null",
             topic, value);
    system(cmd);
}

/* live_n is published alongside bin_offset/bandwidth_bins rather than
 * assumed shared knowledge: it's what those two are measured in units
 * of, and publishing it removes any need for a subscriber (the
 * Dashboard) to assume its own /waterfall connection saw the exact same
 * frame size the daemon did at this instant.
 *
 * Hysteresis (a->locked / a->locked_template_id): a borderline score that
 * sits right around `threshold` would otherwise cross it in either
 * direction on essentially every ~1s cycle, publishing found/"unknown"
 * alternately — visible on the Dashboard as the label chip and reference-
 * match panel blinking every second. Once locked onto a template id, we
 * only let go if the score for that same template drops below
 * (threshold - hold_margin); a clearly-better match for a *different*
 * template still overrides immediately. */
static void publish_result(struct app *a, const struct match_result *r, const struct templates *t, uint32_t live_n)
{
    char buf[64];
    double threshold = a->threshold;
    bool verbose = a->verbose;

    bool accept = r->found && r->score >= threshold;
    if (!accept && a->locked && r->found &&
        r->template_idx < t->count && t->items[r->template_idx].id == a->locked_template_id &&
        r->score >= threshold - a->hold_margin) {
        accept = true; /* holding the existing lock through a brief dip */
    }

    if (!accept) {
        a->locked = false;
        mqtt_pub("label", "unknown");
        mqtt_pub("confidence", "0.0");
        if (verbose) fprintf(stderr, "classify: unknown (best=%.3f)\n", r->found ? r->score : 0.0);
        return;
    }

    const struct template *tpl = &t->items[r->template_idx];
    a->locked = true;
    a->locked_template_id = tpl->id;
    mqtt_pub("label", tpl->label);
    snprintf(buf, sizeof(buf), "%.3f", r->score);
    mqtt_pub("confidence", buf);
    snprintf(buf, sizeof(buf), "%d", r->bin_offset);
    mqtt_pub("freq_offset", buf);
    snprintf(buf, sizeof(buf), "%u", r->bandwidth_bins);
    mqtt_pub("bandwidth_bins", buf);
    snprintf(buf, sizeof(buf), "%u", live_n);
    mqtt_pub("frame_bins", buf);
    snprintf(buf, sizeof(buf), "%u", tpl->id);
    mqtt_pub("template_id", buf);

    if (verbose)
        fprintf(stderr, "classify: %s (id=%u) conf=%.3f bin_offset=%d\n",
                tpl->label, tpl->id, r->score, r->bin_offset);
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

static double elapsed_since(const struct timespec *then)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - then->tv_sec) + (now.tv_nsec - then->tv_nsec) / 1e9;
}

/* A frame counts as "occupied" (something standing out from the noise
 * floor, not just thermal noise) once its peak-to-average ratio clears
 * this floor. A heuristic, not a calibrated detector — same spirit as the
 * synthetic template shapes in tools/classifier_templates already being
 * documented idealizations rather than physically calibrated. */
#define OCCUPIED_PAPR_DB 6.0

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

static void handle_frame(struct app *a, const float *f, size_t n_floats)
{
    if (n_floats < 2) return; /* need at least the step index + 1 bin */

    if (a->last_run.tv_sec != 0 && elapsed_since(&a->last_run) < a->min_interval_s)
        return; /* throttle: discard this frame, correlation is not free */
    clock_gettime(CLOCK_MONOTONIC, &a->last_run);

    uint32_t live_n = (uint32_t)(n_floats - 1);
    float *live = malloc(live_n * sizeof(float));
    if (!live) return;
    memcpy(live, f + 1, live_n * sizeof(float));

    /* Features come from the RAW copy, before normalize() mean-centers it
     * below — and are published unconditionally, whether or not anything
     * ends up matching. */
    struct frame_features ff = extract_frame_features(live, live_n);
    publish_features(&ff, live_n);

    double t_now = elapsed_since(&a->start_time);
    double peak_bin_frac = ff.valid && live_n > 1 ? (double)ff.peak_bin / (double)(live_n - 1) : 0.0;
    bool occupied = ff.valid && ff.papr_db >= OCCUPIED_PAPR_DB;
    history_push(a, t_now, peak_bin_frac, occupied);
    struct temporal_features tf = history_compute(a, live_n);
    publish_temporal(&tf);

    if (!normalize(live, live_n)) {
        /* No energy in this frame at all — publish unknown rather than
         * a meaningless correlation against noise. */
        mqtt_pub("label", "unknown");
        mqtt_pub("confidence", "0.0");
        a->locked = false;
        free(live);
        return;
    }

    struct match_result r = classify(live, live_n, &a->templates, &ff);
    publish_result(a, &r, &a->templates, live_n);
    free(live);
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
        "  -c THRESH match confidence floor (default: 0.6)\n"
        "  -m MARGIN hysteresis hold margin (default: 0.15; holds a lock until\n"
        "            score < THRESH - MARGIN, so a borderline score doesn't\n"
        "            flip found/unknown every cycle)\n"
        "  -i SEC    min correlate interval (default: 1.0)\n"
        "  -v        verbose\n"
        "  -h        this help\n", prog);
}

int main(int argc, char **argv)
{
    const char *host = "localhost";
    int port = 80;
    const char *path = "/waterfall";
    A.templates_path = "/mnt/jffs2/classifier/templates.bin";
    A.threshold       = 0.6;
    A.hold_margin     = 0.15;
    A.min_interval_s  = 1.0;
    A.verbose         = false;
    A.locked          = false;

    int opt;
    while ((opt = getopt(argc, argv, "H:p:P:f:c:m:i:vh")) != -1) {
        switch (opt) {
        case 'H': host              = optarg;      break;
        case 'p': port              = atoi(optarg); break;
        case 'P': path              = optarg;      break;
        case 'f': A.templates_path  = optarg;      break;
        case 'c': A.threshold       = atof(optarg); break;
        case 'm': A.hold_margin     = atof(optarg); break;
        case 'i': A.min_interval_s  = atof(optarg); break;
        case 'v': A.verbose         = true;         break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    clock_gettime(CLOCK_MONOTONIC, &A.start_time);

    if (templates_load(A.templates_path, &A.templates) != 0)
        fprintf(stderr, "classifier: no templates loaded yet (%s) — will publish \"unknown\" until "
                         "cmd/classifier/templates writes one and the daemon is restarted\n",
                A.templates_path);

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
        struct timespec connect_start;
        clock_gettime(CLOCK_MONOTONIC, &connect_start);
        bool ever_connected = false;
        while (A.running) {
            lws_service(ctx, 200);
            if (A.connected) ever_connected = true;
            if (ever_connected && !A.connected) break;      /* dropped: reconnect */
            if (!ever_connected && elapsed_since(&connect_start) > 5.0) break; /* handshake never completed */
        }
        if (A.running && !ever_connected) sleep(5);
    }

    templates_free(&A.templates);
    free(A.msg_buf);
    lws_context_destroy(ctx);
    return 0;
}
