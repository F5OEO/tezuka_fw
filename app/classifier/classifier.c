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
 *   uint32_t version       = 1
 *   uint32_t count
 *   uint32_t canonical_n
 *   then `count` entries of:
 *     char     label[32]   (NUL-padded, truncated if longer)
 *     uint32_t template_id
 *     float    data[canonical_n]   (pre-normalized: zero-mean, unit L2 norm)
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
struct template {
    char     label[32];
    uint32_t id;
    float   *data;   /* canonical_n floats, pre-normalized (zero-mean, unit L2 norm) */
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
    if (fread(&version, sizeof(version), 1, f) != 1 || version != 1) {
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
 * already-whole-frame-normalized vector isn't itself unit-norm). */
static struct match_result classify(const float *live, uint32_t live_n, const struct templates *t)
{
    struct match_result best = { .found = false, .score = -2.0 };
    if (!t->items || t->count == 0) return best;

    uint32_t tn = t->canonical_n;
    float *window = NULL;
    if (tn < live_n) window = malloc(tn * sizeof(float));

    for (uint32_t ti = 0; ti < t->count; ti++) {
        const struct template *tpl = &t->items[ti];

        if (tn >= live_n) {
            /* Template is at least as wide as the live frame: resample
             * the whole live frame down to canonical_n and compare once
             * (offset is meaningless in this case — whole-span match). */
            float *resampled = malloc(tn * sizeof(float));
            if (!resampled) continue;
            resample_linear(live, live_n, resampled, tn);
            if (normalize(resampled, tn)) {
                double s = cosine_sim(resampled, tpl->data, tn);
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
            double s = cosine_sim(window, tpl->data, tn);
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

    if (!normalize(live, live_n)) {
        /* No energy in this frame at all — publish unknown rather than
         * a meaningless correlation against noise. */
        mqtt_pub("label", "unknown");
        mqtt_pub("confidence", "0.0");
        free(live);
        return;
    }

    struct match_result r = classify(live, live_n, &a->templates);
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
