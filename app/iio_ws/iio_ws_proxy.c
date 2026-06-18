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
#include <stdatomic.h>
#include <stdbool.h>
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
    atomic_uintptr_t     wsi_rx;     /* (struct lws *) ADC→WS client */
    atomic_uintptr_t     wsi_tx;     /* (struct lws *) WS→DAC client */
    atomic_bool          tx_flow_paused; /* lws RX flow disabled on wsi_tx (ring full) */

    /* IIO→WS ring (producer: rx_thread, consumer: ws_cb writeable) */
    struct ring rx_ring;
    /* WS→IIO ring (producer: ws_cb receive, consumer: tx_thread) */
    struct ring tx_ring;
    /* TX accumulation: fill directly into the current ring slot.
     * Eliminates the tx_assem intermediate buffer and one memcpy.
     * tx_fill_slot is NULL while no slot is open (between commits). */
    struct slot *tx_fill_slot;
    size_t       tx_fill_len;

    /* Worker threads */
    pthread_t   rx_tid;
    pthread_t   tx_tid;

    /* Shutdown flag */
    volatile bool running;

    /* Stats counters */
    atomic_uint_fast64_t rx_bytes;
    atomic_uint_fast64_t tx_bytes;
    atomic_uint_fast64_t tx_drop;    /* bytes dropped (ring full or no client) */
    atomic_uint_fast64_t tx_under;   /* underrun events (ring empty at push time) */
};

static struct app A;

/* ------------------------------------------------------------------ */
/* IIO helpers                                                         */
/* ------------------------------------------------------------------ */
static void enable_channels(struct iio_device *dev)
{
    unsigned n = iio_device_get_channels_count(dev);
    for (unsigned i = 0; i < n; i++) {
        struct iio_channel *ch = iio_device_get_channel(dev, i);
        if (iio_channel_is_scan_element(ch))
            iio_channel_enable(ch);
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
        ssize_t nb = iio_buffer_refill(a->rx_buf);
        if (nb < 0) {
            if (!a->running) break;
            if (-nb == ETIMEDOUT) continue;
            fprintf(stderr, "[rx] refill: %s\n", strerror(-nb));
            break;
        }

        if (!atomic_load_explicit(&a->wsi_rx, memory_order_acquire))
            continue;

        struct slot *sl = ring_write_begin(&a->rx_ring);
        if (!sl) continue;

        size_t bytes = (size_t)nb < a->rx_ring.cap ? (size_t)nb : a->rx_ring.cap;
        memcpy_iio(sl->buf + LWS_PRE, iio_buffer_start(a->rx_buf), bytes);
        sl->len = bytes;
        ring_write_end(&a->rx_ring);

        atomic_fetch_add_explicit(&a->rx_bytes, bytes, memory_order_relaxed);
        lws_cancel_service(a->lws);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* IIO TX thread: ring → push to DAC                                   */
/* ------------------------------------------------------------------ */
static void *tx_thread(void *arg)
{
    struct app *a = arg;

    while (a->running) {
        struct slot *sl = ring_read_begin(&a->tx_ring);
        if (!sl) {
            atomic_fetch_add_explicit(&a->tx_under, 1, memory_order_relaxed);
            usleep(200);
            continue;
        }

        uint8_t *dst  = iio_buffer_start(a->tx_buf);
        size_t   copy = sl->len < a->buf_bytes ? sl->len : a->buf_bytes;
        memcpy(dst, sl->buf, copy);
        if (copy < a->buf_bytes)
            memset(dst + copy, 0, a->buf_bytes - copy);
        ring_read_end(&a->tx_ring);
        
        if (atomic_load_explicit(&a->tx_flow_paused, memory_order_acquire))
            lws_cancel_service(a->lws);

        ssize_t n = iio_buffer_push(a->tx_buf);
        if (n < 0 && a->running)
            fprintf(stderr, "[tx] push: %s\n", strerror(-n));
        else if (n > 0)
            atomic_fetch_add_explicit(&a->tx_bytes, (size_t)n, memory_order_relaxed);
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
            atomic_store_explicit(&A.wsi_rx, (uintptr_t)wsi, memory_order_release);
        }
        if (is_tx) {
            struct lws *ex = (struct lws *)atomic_load_explicit(&A.wsi_tx, memory_order_acquire);
            if (ex) {
                fprintf(stderr, "[ws] rejecting second TX client (%s)\n", proto);
                if (is_rx)
                    atomic_store_explicit(&A.wsi_rx, (uintptr_t)NULL, memory_order_release);
                return -1;
            }
            A.tx_fill_slot = NULL;
            A.tx_fill_len  = 0;
            atomic_store_explicit(&A.tx_flow_paused, false, memory_order_release);
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
            fprintf(stderr, "[ws] RX client disconnected\n");
        }
        if (atomic_load_explicit(&A.wsi_tx, memory_order_acquire) == (uintptr_t)wsi) {
            atomic_store_explicit(&A.wsi_tx, (uintptr_t)NULL, memory_order_release);
            A.tx_fill_slot = NULL;
            A.tx_fill_len  = 0;
            atomic_store_explicit(&A.tx_flow_paused, false, memory_order_release);
            fprintf(stderr, "[ws] TX client disconnected\n");
        }
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        if (atomic_load_explicit(&A.wsi_rx, memory_order_acquire) != (uintptr_t)wsi)
            break;
        struct slot *sl = ring_read_begin(&A.rx_ring);
        if (!sl) break;

        int sent = lws_write(wsi, sl->buf + LWS_PRE, sl->len, LWS_WRITE_BINARY);
        ring_read_end(&A.rx_ring);
        if (sent < 0) return -1;

        if (ring_has_data(&A.rx_ring))
            lws_callback_on_writable(wsi);
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        if (atomic_load_explicit(&A.wsi_tx, memory_order_acquire) != (uintptr_t)wsi)
            break;
        if (!len) break;

        const uint8_t *src = in;
        size_t remaining = len;

        if (!A.tx_fill_slot && A.tx_fill_len == 0) {
            A.tx_fill_slot = ring_write_begin(&A.tx_ring);
            if (!A.tx_fill_slot) {
                atomic_store_explicit(&A.tx_flow_paused, true, memory_order_release);
                lws_rx_flow_control(wsi, 0);
            }
        }

        while (remaining > 0) {
            size_t space = A.buf_bytes - A.tx_fill_len;
            size_t chunk = remaining < space ? remaining : space;

            if (A.tx_fill_slot)
                memcpy(A.tx_fill_slot->buf + A.tx_fill_len, src, chunk);
            else
                atomic_fetch_add_explicit(&A.tx_drop, chunk, memory_order_relaxed);

            A.tx_fill_len += chunk;
            src           += chunk;
            remaining     -= chunk;

            if (A.tx_fill_len >= A.buf_bytes) {
                if (A.tx_fill_slot) {
                    A.tx_fill_slot->len = A.buf_bytes;
                    ring_write_end(&A.tx_ring);
                }
                A.tx_fill_slot = ring_write_begin(&A.tx_ring);
                A.tx_fill_len  = 0;
                if (!A.tx_fill_slot) {
                    atomic_store_explicit(&A.tx_flow_paused, true, memory_order_release);
                    lws_rx_flow_control(wsi, 0);
                    break;
                }
            }
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
    static uint64_t prx, ptx, pdrop, punder;
    static time_t   pt;

    time_t now = time(NULL);
    if (!pt) { pt = now; return; }
    double dt = difftime(now, pt);
    if (dt < 1.0) return;

    uint64_t rx    = (uint64_t)atomic_load_explicit(&A.rx_bytes, memory_order_relaxed);
    uint64_t tx    = (uint64_t)atomic_load_explicit(&A.tx_bytes, memory_order_relaxed);
    uint64_t drop  = (uint64_t)atomic_load_explicit(&A.tx_drop,  memory_order_relaxed);
    uint64_t under = (uint64_t)atomic_load_explicit(&A.tx_under, memory_order_relaxed);

    fprintf(stderr,
            "stats: RX %.2f MB/s  TX %.2f MB/s  TX-drop %.2f MB/s  TX-under %.0f/s\n",
            (double)(rx    - prx)   / dt / 1e6,
            (double)(tx    - ptx)   / dt / 1e6,
            (double)(drop  - pdrop) / dt / 1e6,
            (double)(under - punder) / dt);
    prx = rx; ptx = tx; pdrop = drop; punder = under; pt = now;
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
        "  -h        this help\n",
        prog,
        DEF_IIO_URI, DEF_RX_DEV, DEF_TX_DEV,
        DEF_PORT, DEF_BUF_SAMPLES);
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
    while ((opt = getopt(argc, argv, "u:r:t:p:n:RTsh")) != -1) {
        switch (opt) {
        case 'u': A.iio_uri     = optarg;           break;
        case 'r': A.rx_dev_name = optarg;           break;
        case 't': A.tx_dev_name = optarg;           break;
        case 'p': A.port        = atoi(optarg);     break;
        case 'n': A.buf_samples = (size_t)atoi(optarg); break;
        case 'R': A.do_rx = true;  A.do_tx = false; break;
        case 'T': A.do_rx = false; A.do_tx = true;  break;
        case 's': A.stats = true;                   break;
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
        enable_channels(A.rx_dev);
        iio_device_set_kernel_buffers_count(A.rx_dev, 8);
        A.sample_size = iio_device_get_sample_size(A.rx_dev);
        A.buf_bytes   = A.buf_samples * A.sample_size;
        A.rx_buf = iio_device_create_buffer(A.rx_dev, A.buf_samples, false);
        if (!A.rx_buf) { fprintf(stderr, "rx buffer create failed\n"); return 1; }
        fprintf(stderr, "iio: RX dev=%s  samples=%zu  sample_size=%zu  buf=%zu B\n",
                A.rx_dev_name, A.buf_samples, A.sample_size, A.buf_bytes);
    }

    if (A.do_tx) {
        A.tx_dev = iio_context_find_device(A.iio, A.tx_dev_name);
        if (!A.tx_dev) {
            fprintf(stderr, "device not found: %s\n", A.tx_dev_name);
            return 1;
        }
        enable_channels(A.tx_dev);
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
        if (ring_init(&A.tx_ring, A.buf_bytes) < 0) {
            fprintf(stderr, "tx ring alloc failed\n"); return 1;
        }
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
    atomic_init(&A.tx_flow_paused, false);
    if (A.do_rx) pthread_create(&A.rx_tid, NULL, rx_thread, &A);
    if (A.do_tx) pthread_create(&A.tx_tid, NULL, tx_thread, &A);

    while (A.running) {
        lws_service(A.lws, 50);

        if (A.do_rx) {
            struct lws *wsi = (struct lws *)atomic_load_explicit(&A.wsi_rx, memory_order_acquire);
            if (wsi && ring_has_data(&A.rx_ring))
                lws_callback_on_writable(wsi);
        }

        if (A.do_tx && atomic_load_explicit(&A.tx_flow_paused, memory_order_acquire)) {
            struct lws *wsi_tx = (struct lws *)atomic_load_explicit(&A.wsi_tx, memory_order_acquire);
            if (wsi_tx && ring_write_begin(&A.tx_ring) != NULL) {
                atomic_store_explicit(&A.tx_flow_paused, false, memory_order_release);
                lws_rx_flow_control(wsi_tx, 1);
            }
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
    if (A.do_tx) ring_destroy(&A.tx_ring);

    return 0;
}