/*
 * cpdetect - blind cyclic-prefix (CP-OFDM) detector for the AD9361 RX stream.
 *
 * A CP-OFDM symbol of N useful samples is preceded by a copy of its last G
 * samples, so y[i] equals y[i+N] (up to a phase) inside every prefix. For each
 * hypothesis (N, G) the program forms cumulative sums of y[i]*conj(y[i+N]) and
 * of the energy, computes the normalised correlation of every window of G
 * samples, and folds it modulo the symbol length P = N + G. The right (N, G)
 * gives one sharp peak at the symbol phase; noise, a CW tone (which correlates
 * at every lag) and single-carrier signals give a flat curve and are rejected by
 * the peak/mean ratio test. The phase of the correlation at the peak is the
 * fractional carrier offset in subcarrier spacings.
 *
 * Same algorithm and thresholds as tools/cpdetect/cpdetect.py (the host tool and
 * test reference); it only needs time-domain IQ, so it does not depend on the
 * FPGA spectrometer.
 *
 * Input: libiio device (default local:, cf-ad9361-lpc) or an int16 interleaved
 * I/Q file (-f). Output: one JSON line per analysis on stdout, optionally also
 * published to MQTT as state/cpdetect/<key> (-M).
 */
#define _GNU_SOURCE
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NO_IIO
#include <iio.h>
#endif

#define MIN_PEAK   0.12
#define MIN_RATIO  3.0
#define RATIO_CAP  12.0

static const int Ns[]  = { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };
static const int GIs[] = { 4, 8, 16, 32 };        /* guard = N / gi */
#define N_NS  ((int)(sizeof(Ns) / sizeof(Ns[0])))
#define N_GIS ((int)(sizeof(GIs) / sizeof(GIs[0])))

struct hyp {
    int N, G, start;
    double peak, ratio, eps, score;
    bool ok, valid;
};

struct work {
    double *csr, *csi, *ce;      /* cumulative sums, length Lmax + 1 */
    double *fold, *sr, *si;      /* folded curve and summed correlation, length Pmax */
    size_t cs_cap, fold_cap;
};

static volatile sig_atomic_t running = 1;

static void on_signal(int s)
{
    (void)s;
    running = 0;
}

static bool work_alloc(struct work *w, size_t cs_cap, size_t fold_cap)
{
    w->cs_cap = cs_cap;
    w->fold_cap = fold_cap;
    w->csr = malloc((cs_cap + 1) * sizeof(double));
    w->csi = malloc((cs_cap + 1) * sizeof(double));
    w->ce = malloc((cs_cap + 1) * sizeof(double));
    w->fold = malloc(fold_cap * sizeof(double));
    w->sr = malloc(fold_cap * sizeof(double));
    w->si = malloc(fold_cap * sizeof(double));
    return w->csr && w->csi && w->ce && w->fold && w->sr && w->si;
}

static void work_free(struct work *w)
{
    free(w->csr); free(w->csi); free(w->ce);
    free(w->fold); free(w->sr); free(w->si);
}

/*
 * Searches all (N, G) hypotheses over x (n complex samples, interleaved I,Q,
 * already scaled). nsym symbols are used per hypothesis. *best receives the
 * accepted hypothesis with the highest score (valid = false if none passed);
 * *top receives the highest scoring hypothesis whether accepted or not.
 */
static void detect(const float *x, size_t n, int nsym, struct work *w,
                   struct hyp *best, struct hyp *top)
{
    memset(best, 0, sizeof(*best));
    memset(top, 0, sizeof(*top));

    for (int a = 0; a < N_NS; a++) {
        const int N = Ns[a];
        const size_t lmax = (size_t)nsym * (N + N / GIs[0]);   /* longest symbol length first (gi = 4) */
        if (lmax + N > n || lmax > w->cs_cap)
            continue;

        /* y[i] * conj(y[i + N]) and the energy, as cumulative sums */
        double ar = 0, ai = 0, ae = 0;
        w->csr[0] = w->csi[0] = w->ce[0] = 0;
        for (size_t i = 0; i < lmax; i++) {
            const double yr = x[2 * i], yi = x[2 * i + 1];
            const double zr = x[2 * (i + N)], zi = x[2 * (i + N) + 1];
            ar += yr * zr + yi * zi;            /* Re(y conj(z)) */
            ai += yi * zr - yr * zi;            /* Im(y conj(z)) */
            ae += 0.5 * (yr * yr + yi * yi + zr * zr + zi * zi);
            w->csr[i + 1] = ar;
            w->csi[i + 1] = ai;
            w->ce[i + 1] = ae;
        }

        for (int b = 0; b < N_GIS; b++) {
            const int G = N / GIs[b];
            const int P = N + G;
            const size_t L = (size_t)nsym * P;
            const long D = (long)L - G + 1;
            const long nfull = D / P;
            if ((size_t)P > w->fold_cap || L < (size_t)(3 * P + G) || nfull < 2)
                continue;

            memset(w->fold, 0, P * sizeof(double));
            memset(w->sr, 0, P * sizeof(double));
            memset(w->si, 0, P * sizeof(double));
            long ph = 0;
            for (long d = 0; d < nfull * P; d++) {
                const double sr = w->csr[d + G] - w->csr[d];
                const double si = w->csi[d + G] - w->csi[d];
                double en = w->ce[d + G] - w->ce[d];
                if (en < 1e-12)
                    en = 1e-12;
                w->fold[ph] += sqrt(sr * sr + si * si) / en;
                w->sr[ph] += sr;
                w->si[ph] += si;
                if (++ph == P)
                    ph = 0;
            }

            int k = 0;
            double mean = 0;
            for (int i = 0; i < P; i++) {
                w->fold[i] /= (double)nfull;
                mean += w->fold[i];
                if (w->fold[i] > w->fold[k])
                    k = i;
            }
            mean /= P;
            struct hyp h;
            h.N = N;
            h.G = G;
            h.start = k;
            h.peak = w->fold[k];
            h.ratio = mean > 0 ? h.peak / mean : 0;
            h.eps = -atan2(w->si[k], w->sr[k]) / (2 * M_PI);
            h.score = h.peak * fmin(h.ratio, RATIO_CAP) / RATIO_CAP;
            h.ok = h.peak > MIN_PEAK && h.ratio > MIN_RATIO;
            h.valid = true;
            if (!top->valid || h.score > top->score)
                *top = h;
            if (h.ok && (!best->valid || h.score > best->score))
                *best = h;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */
static void mqtt_pub(const char *key, const char *value)
{
    char cmd[256];
    /* key and value are program-generated numbers/words, never untrusted input */
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/mosquitto_pub -i tezuka_cpdetect -t 'state/cpdetect/%s' -m '%s' 2>/dev/null", key, value);
    if (system(cmd) != 0) { /* publishing is best effort */ }
}

static void report(const struct hyp *best, const struct hyp *top, double fs, size_t n, bool mqtt)
{
    const struct hyp *h = best->valid ? best : top;
    char b[96];

    printf("{\"samples\": %zu, \"detected\": %s", n, best->valid ? "true" : "false");
    if (h->valid) {
        /* a rejected hypothesis is only for diagnosis, so it is nested apart from a real result */
        printf(best->valid ? ", " : ", \"best_rejected\": {");
        printf("\"N\": %d, \"G\": %d, \"peak\": %.4f, \"ratio\": %.3f, \"start\": %d, \"eps\": %.4f",
               h->N, h->G, h->peak, h->ratio, h->start, h->eps);
        if (fs > 0)
            printf(", \"spacing_hz\": %.2f, \"symbol_us\": %.2f, \"frac_cfo_hz\": %.1f",
                   fs / h->N, (double)(h->N + h->G) / fs * 1e6, h->eps * fs / h->N);
        if (!best->valid)
            printf("}");
    }
    printf("}\n");
    fflush(stdout);

    if (mqtt) {
        mqtt_pub("detected", best->valid ? "1" : "0");
        if (best->valid) {
            snprintf(b, sizeof(b), "%d", best->N);       mqtt_pub("fft_size", b);
            snprintf(b, sizeof(b), "%d", best->G);       mqtt_pub("guard_samples", b);
            snprintf(b, sizeof(b), "%.3f", best->peak);  mqtt_pub("peak", b);
            snprintf(b, sizeof(b), "%.2f", best->ratio); mqtt_pub("ratio", b);
            snprintf(b, sizeof(b), "%.4f", best->eps);   mqtt_pub("eps", b);
            if (fs > 0) {
                snprintf(b, sizeof(b), "%.2f", fs / best->N);
                mqtt_pub("spacing_hz", b);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sample sources                                                      */
/* ------------------------------------------------------------------ */
static void int16_to_float(const int16_t *src, float *dst, size_t n_complex)
{
    for (size_t i = 0; i < 2 * n_complex; i++)
        dst[i] = src[i] * (1.0f / 2048.0f);
}

static ssize_t read_file(const char *path, float *x, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }
    int16_t *tmp = malloc(n * 2 * sizeof(int16_t));
    if (!tmp) {
        fclose(f);
        return -1;
    }
    size_t got = fread(tmp, 2 * sizeof(int16_t), n, f);
    fclose(f);
    int16_to_float(tmp, x, got);
    free(tmp);
    return (ssize_t)got;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -f FILE   analyse an int16 interleaved I/Q file instead of the radio\n"
        "  -u URI    libiio context (default: local:)\n"
        "  -d DEV    RX device (default: cf-ad9361-lpc)\n"
        "  -s HZ     sample rate; read from ad9361-phy when capturing, needed with -f\n"
        "            only for the Hz figures\n"
        "  -n N      samples per analysis (default: 262144)\n"
        "  -y NSYM   symbols folded per hypothesis (default: 12)\n"
        "  -l        loop until interrupted\n"
        "  -i SEC    interval between analyses with -l (default: 1.0)\n"
        "  -M        also publish to MQTT as state/cpdetect/<key>\n"
        "  -h        this help\n", prog);
}

int main(int argc, char **argv)
{
    const char *file = NULL, *uri = "local:", *devname = "cf-ad9361-lpc";
    double fs = 0, interval = 1.0;
    size_t n = 262144;
    int nsym = 12, opt;
    bool loop = false, mqtt = false;

    while ((opt = getopt(argc, argv, "f:u:d:s:n:y:li:Mh")) != -1) {
        switch (opt) {
        case 'f': file = optarg; break;
        case 'u': uri = optarg; break;
        case 'd': devname = optarg; break;
        case 's': fs = atof(optarg); break;
        case 'n': n = (size_t)atol(optarg); break;
        case 'y': nsym = atoi(optarg); break;
        case 'l': loop = true; break;
        case 'i': interval = atof(optarg); break;
        case 'M': mqtt = true; break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 2;
        }
    }
    if (nsym < 4 || n < 1024) {
        fprintf(stderr, "bad -y or -n\n");
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    float *x = malloc(n * 2 * sizeof(float));
    struct work w;
    const size_t cs_cap = (size_t)nsym * (Ns[N_NS - 1] + Ns[N_NS - 1] / GIs[0]);
    const size_t fold_cap = Ns[N_NS - 1] + Ns[N_NS - 1] / GIs[0];   /* longest P: gi = 4 */
    if (!x || !work_alloc(&w, cs_cap, fold_cap)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    struct hyp best, top;
    int rc = 0;

    if (file) {
        ssize_t got = read_file(file, x, n);
        if (got < 0)
            return 1;
        detect(x, (size_t)got, nsym, &w, &best, &top);
        report(&best, &top, fs, (size_t)got, mqtt);
        work_free(&w);
        free(x);
        return 0;
    }

#ifdef NO_IIO
    (void)uri;
    (void)devname;
    fprintf(stderr, "built without libiio: use -f FILE\n");
    rc = 2;
#else
    struct iio_context *ctx = iio_create_context_from_uri(uri);
    if (!ctx) {
        fprintf(stderr, "cannot open libiio context %s\n", uri);
        return 1;
    }
    struct iio_device *dev = iio_context_find_device(ctx, devname);
    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!dev) {
        fprintf(stderr, "device %s not found\n", devname);
        iio_context_destroy(ctx);
        return 1;
    }
    struct iio_channel *ch0 = iio_device_find_channel(dev, "voltage0", false);
    struct iio_channel *ch1 = iio_device_find_channel(dev, "voltage1", false);
    if (!ch0 || !ch1) {
        fprintf(stderr, "RX channels voltage0/voltage1 not found\n");
        iio_context_destroy(ctx);
        return 1;
    }
    iio_channel_enable(ch0);
    iio_channel_enable(ch1);
    struct iio_buffer *buf = iio_device_create_buffer(dev, n, false);
    if (!buf) {
        fprintf(stderr, "cannot create a %zu sample buffer\n", n);
        iio_context_destroy(ctx);
        return 1;
    }

    while (running) {
        long long rate = 0;
        if (fs <= 0 && phy) {
            struct iio_channel *pc = iio_device_find_channel(phy, "voltage0", false);
            if (pc && iio_channel_attr_read_longlong(pc, "sampling_frequency", &rate) == 0)
                fs = (double)rate;
        }
        ssize_t bytes = iio_buffer_refill(buf);
        if (bytes < 0) {
            fprintf(stderr, "buffer refill failed: %zd\n", bytes);
            rc = 1;
            break;
        }
        const int16_t *p = (const int16_t *)iio_buffer_first(buf, ch0);
        size_t got = (size_t)((const char *)iio_buffer_end(buf) - (const char *)p) / iio_buffer_step(buf);
        if (got > n)
            got = n;
        int16_to_float(p, x, got);
        detect(x, got, nsym, &w, &best, &top);
        report(&best, &top, fs, got, mqtt);
        if (!loop)
            break;
        struct timespec ts = { (time_t)interval, (long)((interval - (long)interval) * 1e9) };
        nanosleep(&ts, NULL);
    }
    iio_buffer_destroy(buf);
    iio_context_destroy(ctx);
#endif
    work_free(&w);
    free(x);
    return rc;
}
