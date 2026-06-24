/*
 * iio_ws_proxy.c — libiio IQ stream ↔ WebSocket binary bridge
 *
 * Full-duplex single WebSocket connection:
 * IIO ADC samples  →  WS binary frames  (server push)
 * WS binary frames →  IIO DAC samples   (client push)
 *
 * WS sub-protocol negotiation (client selects one):
 * "iio-iq"   full-duplex   (Also acts as DEFAULT fallback if client provides NO subprotocol)
 * "iio-rx"   ADC→WS only
 * "iio-tx"   WS→DAC only
 *
 * TX framing: the proxy accumulates incoming WS data until a full IIO
 * buffer's worth has arrived, then hands it to the DAC thread.  WS
 * frame boundaries are ignored on the TX path for throughput.
 *
 * RX framing: each iio_buffer_refill() result becomes one binary WS frame.
 *
 * Build (cross-compile for Pluto):
 * $(CC) -O2 -o iio_ws_proxy iio_ws_proxy.c -liio -lwebsockets -lpthread
 *
 * Usage:
 * iio_ws_proxy [OPTIONS]
 * -u URI    IIO context URI        (default: local:)
 * -r DEV    RX IIO device          (default: cf-ad9361-lpc)
 * -t DEV    TX IIO device          (default: cf-ad9361-dds-core-lpc)
 * -p PORT   WebSocket listen port  (default: 8765)
 * -n N      Buffer samples         (default: 262144)
 * -R        ADC→WS only  (disable DAC path)
 * -T        WS→DAC only  (disable ADC path)
 * -s        Throughput stats every second
 */

#define _GNU_SOURCE
#include <libwebsockets.h>
#include <iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <netinet/tcp.h>
#ifndef TCP_NOTSENT_LOWAT          /* musl may not expose it yet */
#define TCP_NOTSENT_LOWAT 25
#endif
#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */
#define DEF_PORT         8765
#define DEF_BUF_SAMPLES  (1 << 18)   /* 262144 samples */
#define DEF_IIO_URI      "local:"
#define DEF_RX_DEV       "cf-ad9361-lpc"
#define DEF_TX_DEV       "cf-ad9361-dds-core-lpc"
#define RING_DEPTH       4            /* must be power of 2 */
#define IIO_TIMEOUT_MS   1000         /* lets RX thread notice shutdown */

/* ------------------------------------------------------------------ */
/* Lock-free SPSC ring buffer                                          */
/* ------------------------------------------------------------------ */
struct slot {
    uint8_t *buf;    /* LWS_PRE headroom + data payload */
    size_t   len;    /* payload bytes written */
};

struct ring {
    struct slot  s[RING_DEPTH];
    size_t       cap;
    atomic_uint  prod;
    atomic_uint  cons;
};

static int ring_init(struct ring *r, size_t cap)
{
    r->cap = cap;
    atomic_init(&r->prod, 0);
    atomic_init(&r->cons, 0);
    for (int i = 0; i < RING_DEPTH; i++) {
        r->s[i].buf = malloc(LWS_PRE + cap);
        r->s[i].len = 0;
        if (!r->s[i].buf) return -ENOMEM;
    }
    return 0;
}

static void ring_destroy(struct ring *r)
{
    for (int i = 0; i < RING_DEPTH; i++) free(r->s[i].buf);
}

/* Producer: get writable slot or NULL when full */
static struct slot *ring_write_begin(struct ring *r)
{
    unsigned p = atomic_load_explicit(&r->prod, memory_order_relaxed);
    unsigned c = atomic_load_explicit(&r->cons, memory_order_acquire);
    if (p - c >= RING_DEPTH) return NULL;
    return &r->s[p & (RING_DEPTH - 1)];
}
static void ring_write_end(struct ring *r)
{
    atomic_fetch_add_explicit(&r->prod, 1, memory_order_release);
}

/* Consumer: get readable slot or NULL when empty */
static struct slot *ring_read_begin(struct ring *r)
{
    unsigned c = atomic_load_explicit(&r->cons, memory_order_relaxed);
    unsigned p = atomic_load_explicit(&r->prod, memory_order_acquire);
    if (p == c) return NULL;
    return &r->s[c & (RING_DEPTH - 1)];
}
static void ring_read_end(struct ring *r)
{
    atomic_fetch_add_explicit(&r->cons, 1, memory_order_release);
}

static bool ring_has_data(struct ring *r)
{
    return atomic_load_explicit(&r->prod, memory_order_acquire) !=
           atomic_load_explicit(&r->cons, memory_order_relaxed);
}

/* ------------------------------------------------------------------ */
/* Application context                                                 */
/* ------------------------------------------------------------------ */
struct app {
    /* Config */
    const char  *iio_uri;
    const char  *rx_dev_name;
    const char  *tx_dev_name;
    int          port;
    size_t       buf_samples;
    bool         do_rx;
    bool         do_tx;
    bool         stats;
    bool         do_cs12;    /* -c: pack RX int16 -> 12-bit (3 bytes/complex), ~25% less link BW */
    bool         rx1_only;   /* -1: enable only RX1 (first 2 scan elements) for a clean I/Q stream */
    bool         lazy;       /* -L: hold cf-ad9361-lpc only while an RX WS client is connected */
    atomic_bool  want_rx;    /* lazy: an iio-rx client is currently connected */

    /* IIO */
    struct iio_context *iio;
    struct iio_device  *rx_dev;
    struct iio_device  *tx_dev;
    struct iio_buffer  *rx_buf;
    struct iio_buffer  *tx_buf;
    size_t              sample_size;
    size_t              buf_bytes;

    /* WebSocket */
    struct lws_context  *lws;
    /* Separate slots for the two directions so an iio-rx recorder and an
     * iio-tx player can be connected simultaneously.  iio-iq uses wsi_rx
     * for writes and wsi_tx for the same connection (set to the same wsi). */
    atomic_uintptr_t     wsi_rx;          /* (struct lws *) ADC→WS client */
    atomic_uintptr_t     wsi_tx;          /* (struct lws *) WS→DAC client */
    atomic_bool          tx_push_pending; /* push in flight; WS RX paused */

    /* IIO→WS ring (producer: rx_thread, consumer: ws_cb writeable) */
    struct ring rx_ring;
    /* TX: WS writes directly into iio_buffer_start(); sem signals push.
     * Client must send frames that are multiples of sample_size so that
     * tx_fill_len always reaches buf_bytes exactly (no straddle loss).
     * Dashboard sends 64 KB frames; default buf_bytes = 1 MB = 16×64 KB. */
    sem_t  tx_push_sem;
    size_t tx_fill_len;  /* bytes written into TX IIO buffer so far */

    /* Worker threads */
    pthread_t   rx_tid;
    pthread_t   tx_tid;

    /* Shutdown flag */
    volatile bool running;

    /* Stats counters */
    atomic_uint_fast64_t rx_bytes;
    atomic_uint_fast64_t rx_drop;    /* IIO buffers dropped (ring full, WS can't keep up) */
    atomic_uint_fast64_t tx_bytes;
    atomic_uint_fast64_t tx_drop;    /* bytes dropped (frame straddles buffer boundary) */
};

static struct app A;

/* ------------------------------------------------------------------ */
/* IIO helpers                                                         */
/* ------------------------------------------------------------------ */
/* Pack S12/16 int16 IQ -> tight 12-bit, 3 bytes per complex (2 int16). ~25% less
 * link bandwidth. Byte order matches the OpenRF MaiaSource host decoder. */
static size_t pack_cs12(uint8_t *restrict dst, const int16_t *restrict src, size_t n_int16)
{
    size_t o = 0;
    for (size_t i = 0; i + 1 < n_int16; i += 2) {
        uint16_t v0 = (uint16_t)src[i]     & 0x0FFF;
        uint16_t v1 = (uint16_t)src[i + 1] & 0x0FFF;
        dst[o++] = (uint8_t)(v0 & 0xFF);
        dst[o++] = (uint8_t)(((v1 & 0x0F) << 4) | ((v0 >> 8) & 0x0F));
        dst[o++] = (uint8_t)((v1 >> 4) & 0xFF);
    }
    return o;
}

/* Enable scan-element channels. max_scan>0 limits to the first max_scan scan
 * elements (RX1-only = 2: voltage0=I, voltage1=Q); 0 = all. The AD9361 exposes
 * 4 RX scan elements (RX1 I/Q + RX2 I/Q); enabling all packs 8-byte slots that a
 * single-RX consumer misreads, so RX1-only gives a clean 4-byte interleaved stream. */
static void enable_channels(struct iio_device *dev, unsigned max_scan)
{
    unsigned n = iio_device_get_channels_count(dev);
    unsigned en = 0;
    for (unsigned i = 0; i < n; i++) {
        struct iio_channel *ch = iio_device_get_channel(dev, i);
        if (iio_channel_is_scan_element(ch)) {
            if (max_scan && en >= max_scan) {
                iio_channel_disable(ch);
            } else {
                iio_channel_enable(ch);
                en++;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* DMA-aware memcpy                                                    */
/* ------------------------------------------------------------------ */
#ifdef __ARM_NEON__
static void memcpy_iio(void *restrict dst, const void *restrict src, size_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t       *d = (uint8_t *)dst;

    __builtin_prefetch(s,       0, 0);
    __builtin_prefetch(s + 64,  0, 0);
    __builtin_prefetch(s + 128, 0, 0);
    __builtin_prefetch(s + 192, 0, 0);

    while (n >= 64) {
        __builtin_prefetch(s + 256, 0, 0);
        uint8x16_t v0 = vld1q_u8(s);
        uint8x16_t v1 = vld1q_u8(s + 16);
        uint8x16_t v2 = vld1q_u8(s + 32);
        uint8x16_t v3 = vld1q_u8(s + 48);
        vst1q_u8(d,      v0);
        vst1q_u8(d + 16, v1);
        vst1q_u8(d + 32, v2);
        vst1q_u8(d + 48, v3);
        s += 64; d += 64; n -= 64;
    }
    if (n) memcpy(d, s, n);
}
#else
#define memcpy_iio memcpy
#endif

/* ------------------------------------------------------------------ */
/* IIO RX thread: refill buffer → ring → wake WS                      */
/* ------------------------------------------------------------------ */
static void *rx_thread(void *arg)
{
    struct app *a = arg;

    while (a->running) {
        /* Lazy device hold: own cf-ad9361-lpc only while an RX client is connected,
         * so ordinary libiio clients (SDR++/SDR#) can use the device when idle. All
         * rx_buf create/destroy stays in this one thread to avoid cross-thread races. */
        if (a->lazy) {
            bool want = atomic_load_explicit(&a->want_rx, memory_order_acquire);
            if (want && !a->rx_buf) {
                enable_channels(a->rx_dev, a->rx1_only ? 2 : 0);
                a->rx_buf = iio_device_create_buffer(a->rx_dev, a->buf_samples, false);
                if (!a->rx_buf) { usleep(50000); continue; }
                fprintf(stderr, "[rx] client present -> acquired %s\n", a->rx_dev_name);
            } else if (!want && a->rx_buf) {
                struct iio_buffer *b = a->rx_buf;
                a->rx_buf = NULL;
                iio_buffer_destroy(b);
                fprintf(stderr, "[rx] no client -> released %s\n", a->rx_dev_name);
            }
            if (!a->rx_buf) { usleep(20000); continue; }
        }

        ssize_t nb = iio_buffer_refill(a->rx_buf);
        if (nb < 0) {
            if (!a->running) break;
            if (-nb == ETIMEDOUT) continue;
            /* Survive a rate change / transient DMA error: drop the buffer and
             * recreate it instead of dying, so the stream resumes by itself. */
            fprintf(stderr, "[rx] refill: %s -> recreating buffer\n", strerror(-nb));
            iio_buffer_destroy(a->rx_buf);
            a->rx_buf = NULL;
            if (!a->lazy) {
                enable_channels(a->rx_dev, a->rx1_only ? 2 : 0);
                a->rx_buf = iio_device_create_buffer(a->rx_dev, a->buf_samples, false);
                if (!a->rx_buf) { fprintf(stderr, "[rx] recreate failed\n"); break; }
            }
            usleep(50000);
            continue;
        }

        if (!atomic_load_explicit(&a->wsi_rx, memory_order_acquire))
            continue;

        struct slot *sl = ring_write_begin(&a->rx_ring);
        if (!sl) {
            atomic_fetch_add_explicit(&a->rx_drop, 1, memory_order_relaxed);
            continue;
        }

        size_t bytes;
        if (a->do_cs12) {
            /* nb bytes = nb/2 int16 samples -> nb*3/4 packed bytes (< raw, fits cap) */
            bytes = pack_cs12(sl->buf + LWS_PRE,
                              (const int16_t *)iio_buffer_start(a->rx_buf),
                              (size_t)nb / sizeof(int16_t));
        } else {
            bytes = (size_t)nb < a->rx_ring.cap ? (size_t)nb : a->rx_ring.cap;
            memcpy_iio(sl->buf + LWS_PRE, iio_buffer_start(a->rx_buf), bytes);
        }
        sl->len = bytes;
        ring_write_end(&a->rx_ring);

        atomic_fetch_add_explicit(&a->rx_bytes, bytes, memory_order_relaxed);
        lws_cancel_service(a->lws);
    }
    if (a->lazy && a->rx_buf) { iio_buffer_destroy(a->rx_buf); a->rx_buf = NULL; }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* IIO TX thread: semaphore-driven push to DAC                         */
/* ------------------------------------------------------------------ */
static void *tx_thread(void *arg)
{
    struct app *a = arg;

    while (a->running) {
        while (sem_wait(&a->tx_push_sem) == -1 && errno == EINTR)
            continue;
        if (!a->running) break;

        ssize_t n = iio_buffer_push(a->tx_buf);
        if (n < 0 && a->running)
            fprintf(stderr, "[tx] push: %s\n", strerror(-n));
        else if (n > 0)
            atomic_fetch_add_explicit(&a->tx_bytes, (size_t)n, memory_order_relaxed);

        atomic_store_explicit(&a->tx_push_pending, false, memory_order_release);
        lws_cancel_service(a->lws);   /* wake main loop to re-enable WS RX flow */
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* WebSocket callback                                                  */
/* ------------------------------------------------------------------ */
static int ws_cb(struct lws *wsi, enum lws_callback_reasons reason,
                 void *user, void *in, size_t len)
{
    (void)user;

    const struct lws_protocols *lws_proto = lws_get_protocol(wsi);
    if (!lws_proto) return 0;
    const char *proto = lws_proto->name;
    
    bool is_rx = A.do_rx && (strcmp(proto, "iio-rx") == 0 || strcmp(proto, "iio-iq") == 0);
    bool is_tx = A.do_tx && (strcmp(proto, "iio-tx") == 0 || strcmp(proto, "iio-iq") == 0);

    switch (reason) {

    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
        /* Return 0 to bypass strict subprotocol enforcement and allow 
         * protocol-less handshakes to proceed into fallback mode. */
        return 0;
    }

    case LWS_CALLBACK_ESTABLISHED: {
        if (is_rx) {
            struct lws *ex = (struct lws *)atomic_load_explicit(&A.wsi_rx, memory_order_acquire);
            if (ex) {
                fprintf(stderr, "[ws] rejecting second RX client (%s)\n", proto);
                return -1;
            }
            int fd = lws_get_socket_fd(wsi);
            if (fd >= 0) {
                /* Limit unsent queue depth to reduce BBR RTT inflation. */
                int lowat = (int)A.buf_bytes;
                if (setsockopt(fd, IPPROTO_TCP, TCP_NOTSENT_LOWAT,
                               &lowat, sizeof(lowat)) < 0)
                    fprintf(stderr, "[ws] TCP_NOTSENT_LOWAT: %s\n", strerror(errno));
                /* Kill connections that stall (zero-window or dead peer) so
                 * the client can reconnect instead of hanging indefinitely. */
                unsigned int user_timeout_ms = 5000;
                if (setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT,
                               &user_timeout_ms, sizeof(user_timeout_ms)) < 0)
                    fprintf(stderr, "[ws] TCP_USER_TIMEOUT: %s\n", strerror(errno));
            }
            atomic_store_explicit(&A.wsi_rx, (uintptr_t)wsi, memory_order_release);
            if (A.lazy)
                atomic_store_explicit(&A.want_rx, true, memory_order_release);
        }
        if (is_tx) {
            struct lws *ex = (struct lws *)atomic_load_explicit(&A.wsi_tx, memory_order_acquire);
            if (ex) {
                fprintf(stderr, "[ws] rejecting second TX client (%s)\n", proto);
                if (is_rx)
                    atomic_store_explicit(&A.wsi_rx, (uintptr_t)NULL, memory_order_release);
                return -1;
            }
            A.tx_fill_len = 0;
            atomic_store_explicit(&A.tx_push_pending, false, memory_order_release);
            lws_rx_flow_control(wsi, 1);
            atomic_store_explicit(&A.wsi_tx, (uintptr_t)wsi, memory_order_release);
        }
        fprintf(stderr, "[ws] client connected (%s / fallback-mode)\n", proto);
        if (is_rx)
            lws_callback_on_writable(wsi);
        break;
    }

    case LWS_CALLBACK_CLOSED:
        if (atomic_load_explicit(&A.wsi_rx, memory_order_acquire) == (uintptr_t)wsi) {
            atomic_store_explicit(&A.wsi_rx, (uintptr_t)NULL, memory_order_release);
            if (A.lazy)
                atomic_store_explicit(&A.want_rx, false, memory_order_release);
            fprintf(stderr, "[ws] RX client disconnected\n");
        }
        if (atomic_load_explicit(&A.wsi_tx, memory_order_acquire) == (uintptr_t)wsi) {
            atomic_store_explicit(&A.wsi_tx, (uintptr_t)NULL, memory_order_release);
            A.tx_fill_len = 0;
            fprintf(stderr, "[ws] TX client disconnected\n");
        }
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        if (atomic_load_explicit(&A.wsi_rx, memory_order_acquire) != (uintptr_t)wsi)
            break;
        struct slot *sl = ring_read_begin(&A.rx_ring);
        if (!sl) break;

        int sent = lws_write(wsi, sl->buf + LWS_PRE, sl->len, LWS_WRITE_BINARY);
        if (sent < 0)
            return -1;

        ring_read_end(&A.rx_ring);
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        if (atomic_load_explicit(&A.wsi_tx, memory_order_acquire) != (uintptr_t)wsi)
            break;
        if (!len) break;

        /* iio_buffer_start() pointer may change after each push (kernel rotates
         * DMA regions), so always re-read it here. tx_push_pending + flow-control
         * guarantee no RECEIVE fires while a push is in flight. */
        uint8_t        *iio_dst  = iio_buffer_start(A.tx_buf);
        const uint8_t  *src      = in;
        size_t          space    = A.buf_bytes - A.tx_fill_len;
        size_t          chunk    = len < space ? len : space;

        memcpy(iio_dst + A.tx_fill_len, src, chunk);
        A.tx_fill_len += chunk;

        if (chunk < len)
            atomic_fetch_add_explicit(&A.tx_drop, len - chunk, memory_order_relaxed);

        if (A.tx_fill_len >= A.buf_bytes) {
            A.tx_fill_len = 0;
            atomic_store_explicit(&A.tx_push_pending, true, memory_order_release);
            lws_rx_flow_control(wsi, 0);
            sem_post(&A.tx_push_sem);
        }
        break;
    }

    default:
        break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Protocols                                                          */
/* ------------------------------------------------------------------ */
/* MODIFIED: "iio-iq" is placed first so protocol-less handshakes fall back to full-duplex mode */
static struct lws_protocols protocols[] = {
    { "iio-iq", ws_cb, 0, 0, 0, NULL, 0 },
    { "iio-rx", ws_cb, 0, 0, 0, NULL, 0 },
    { "iio-tx", ws_cb, 0, 0, 0, NULL, 0 },
    { NULL,     NULL,  0, 0, 0, NULL, 0 }
};

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */
static void show_stats(void)
{
    static uint64_t prx, prx_drop, ptx, pdrop;
    static time_t   pt;

    time_t now = time(NULL);
    if (!pt) { pt = now; return; }
    double dt = difftime(now, pt);
    if (dt < 1.0) return;

    uint64_t rx      = (uint64_t)atomic_load_explicit(&A.rx_bytes, memory_order_relaxed);
    uint64_t rx_drop = (uint64_t)atomic_load_explicit(&A.rx_drop,  memory_order_relaxed);
    uint64_t tx      = (uint64_t)atomic_load_explicit(&A.tx_bytes, memory_order_relaxed);
    uint64_t drop    = (uint64_t)atomic_load_explicit(&A.tx_drop,  memory_order_relaxed);

    fprintf(stderr,
            "stats: RX %.2f MB/s  RX-drop %.0f/s  TX %.2f MB/s  TX-drop %.2f MB/s\n",
            (double)(rx      - prx)      / dt / 1e6,
            (double)(rx_drop - prx_drop) / dt,
            (double)(tx      - ptx)      / dt / 1e6,
            (double)(drop    - pdrop)    / dt / 1e6);
    prx = rx; prx_drop = rx_drop; ptx = tx; pdrop = drop; pt = now;
}

/* ------------------------------------------------------------------ */
/* Signal handler / usage                                              */
/* ------------------------------------------------------------------ */
static void on_signal(int s)
{
    (void)s;
    A.running = false;
    if (A.rx_buf) iio_buffer_cancel(A.rx_buf);
    if (A.lws)    lws_cancel_service(A.lws);
    sem_post(&A.tx_push_sem);   /* unblock tx_thread if waiting */
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -u URI    IIO URI         (default: %s)\n"
        "  -r DEV    RX device       (default: %s)\n"
        "  -t DEV    TX device       (default: %s)\n"
        "  -p PORT   WS port         (default: %d)\n"
        "  -n N      buffer samples  (default: %d)\n"
        "  -R        ADC→WS only\n"
        "  -T        WS→DAC only\n"
        "  -s        throughput stats\n"
        "  -c        pack RX as CS12 (12-bit, 3 bytes/complex)\n"
        "  -1        RX1 only (first 2 scan elements: clean 4-byte I/Q)\n"
        "  -L        lazy device hold (own RX device only while a WS client is connected)\n"
        "  -h        this help\n",
        prog,
        DEF_IIO_URI, DEF_RX_DEV, DEF_TX_DEV,
        DEF_PORT, DEF_BUF_SAMPLES);
}

/* ------------------------------------------------------------------ */
/* Thread scheduling                                                   */
/* ------------------------------------------------------------------ */
static void set_realtime(pthread_t tid, int prio)
{
    struct sched_param sp = { .sched_priority = prio };
    int r = pthread_setschedparam(tid, SCHED_FIFO, &sp);
    if (r)
        fprintf(stderr, "warn: SCHED_FIFO prio %d failed: %s (run as root?)\n",
                prio, strerror(r));
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    A.iio_uri     = DEF_IIO_URI;
    A.rx_dev_name = DEF_RX_DEV;
    A.tx_dev_name = DEF_TX_DEV;
    A.port        = DEF_PORT;
    A.buf_samples = DEF_BUF_SAMPLES;
    A.do_rx       = true;
    A.do_tx       = true;

    int opt;
    while ((opt = getopt(argc, argv, "u:r:t:p:n:RTsc1Lh")) != -1) {
        switch (opt) {
        case 'u': A.iio_uri     = optarg;           break;
        case 'r': A.rx_dev_name = optarg;           break;
        case 't': A.tx_dev_name = optarg;           break;
        case 'p': A.port        = atoi(optarg);     break;
        case 'n': A.buf_samples = (size_t)atoi(optarg); break;
        case 'R': A.do_rx = true;  A.do_tx = false; break;
        case 'T': A.do_rx = false; A.do_tx = true;  break;
        case 's': A.stats = true;                   break;
        case 'c': A.do_cs12  = true;                break;
        case '1': A.rx1_only = true;                break;
        case 'L': A.lazy     = true;                break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "iio: connecting %s\n", A.iio_uri);
    A.iio = iio_create_context_from_uri(A.iio_uri);
    if (!A.iio) { fprintf(stderr, "iio context failed\n"); return 1; }

    iio_context_set_timeout(A.iio, IIO_TIMEOUT_MS);

    if (A.do_rx) {
        A.rx_dev = iio_context_find_device(A.iio, A.rx_dev_name);
        if (!A.rx_dev) {
            fprintf(stderr, "device not found: %s\n", A.rx_dev_name);
            return 1;
        }
        enable_channels(A.rx_dev, A.rx1_only ? 2 : 0);
        iio_device_set_kernel_buffers_count(A.rx_dev, 8);
        A.sample_size = iio_device_get_sample_size(A.rx_dev);
        A.buf_bytes   = A.buf_samples * A.sample_size;
        if (A.lazy) {
            /* Defer buffer creation to the RX thread (created on first WS client),
             * so the device stays free for ordinary libiio clients while idle. */
            fprintf(stderr, "iio: RX dev=%s  lazy-hold (acquired on WS connect)  sample_size=%zu\n",
                    A.rx_dev_name, A.sample_size);
        } else {
            A.rx_buf = iio_device_create_buffer(A.rx_dev, A.buf_samples, false);
            if (!A.rx_buf) { fprintf(stderr, "rx buffer create failed\n"); return 1; }
            fprintf(stderr, "iio: RX dev=%s  samples=%zu  sample_size=%zu  buf=%zu B\n",
                    A.rx_dev_name, A.buf_samples, A.sample_size, A.buf_bytes);
        }
    }

    if (A.do_tx) {
        A.tx_dev = iio_context_find_device(A.iio, A.tx_dev_name);
        if (!A.tx_dev) {
            fprintf(stderr, "device not found: %s\n", A.tx_dev_name);
            return 1;
        }
        enable_channels(A.tx_dev, 0);
        iio_device_set_kernel_buffers_count(A.tx_dev, 4);
        if (!A.sample_size) {
            A.sample_size = iio_device_get_sample_size(A.tx_dev);
            A.buf_bytes   = A.buf_samples * A.sample_size;
        }
        A.tx_buf = iio_device_create_buffer(A.tx_dev, A.buf_samples, false);
        if (!A.tx_buf) { fprintf(stderr, "tx buffer create failed\n"); return 1; }
        fprintf(stderr, "iio: TX dev=%s\n", A.tx_dev_name);
    }

    if (!A.buf_bytes)
        A.buf_bytes = A.buf_samples * 4;

    if (A.do_rx && ring_init(&A.rx_ring, A.buf_bytes) < 0) {
        fprintf(stderr, "rx ring alloc failed\n"); return 1;
    }
    if (A.do_tx) {
        sem_init(&A.tx_push_sem, 0, 0);
        atomic_init(&A.tx_push_pending, false);
        A.tx_fill_len = 0;
    }

    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, NULL);

    size_t ws_rx_sz = A.buf_bytes < (1u << 20) ? A.buf_bytes : (1u << 20);
    for (int i = 0; protocols[i].name; i++) {
        protocols[i].rx_buffer_size = ws_rx_sz;
    }

    struct lws_context_creation_info info = {
        .port      = A.port,
        .protocols = protocols,
        .gid       = -1,
        .uid       = -1,
        .options   = 0,
    };
    A.lws = lws_create_context(&info);
    if (!A.lws) { fprintf(stderr, "lws context failed\n"); return 1; }
    fprintf(stderr, "ws: listening on port %d  (default fallback: iio-iq full duplex enabled)\n", A.port);

    A.running = true;
    atomic_init(&A.wsi_rx, (uintptr_t)NULL);
    atomic_init(&A.wsi_tx, (uintptr_t)NULL);
    atomic_init(&A.want_rx, false);
    if (A.do_rx) { pthread_create(&A.rx_tid, NULL, rx_thread, &A); set_realtime(A.rx_tid, 10); }
    if (A.do_tx) { pthread_create(&A.tx_tid, NULL, tx_thread, &A); set_realtime(A.tx_tid,  5); }

    while (A.running) {
        int timeout_ms = (A.do_rx && ring_has_data(&A.rx_ring)) ? 0 : 50;
        lws_service(A.lws, timeout_ms);

        if (A.do_rx) {
            struct lws *wsi = (struct lws *)atomic_load_explicit(&A.wsi_rx, memory_order_acquire);
            if (wsi && ring_has_data(&A.rx_ring))
                lws_callback_on_writable(wsi);
        }

        if (A.do_tx && !atomic_load_explicit(&A.tx_push_pending, memory_order_acquire)) {
            struct lws *wsi_tx = (struct lws *)atomic_load_explicit(&A.wsi_tx, memory_order_acquire);
            if (wsi_tx)
                lws_rx_flow_control(wsi_tx, 1);
        }

        if (A.stats) show_stats();
    }

    A.running = false;
    if (A.do_rx) pthread_join(A.rx_tid, NULL);
    if (A.do_tx) pthread_join(A.tx_tid, NULL);

    lws_context_destroy(A.lws);
    if (A.rx_buf) iio_buffer_destroy(A.rx_buf);
    if (A.tx_buf) iio_buffer_destroy(A.tx_buf);
    iio_context_destroy(A.iio);
    if (A.do_rx) ring_destroy(&A.rx_ring);
    if (A.do_tx) sem_destroy(&A.tx_push_sem);

    return 0;
}