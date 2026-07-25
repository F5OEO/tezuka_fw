/*
 * mqtt_pubd.c — persistent-connection MQTT publisher for state echoes
 *
 * Replaces api_controller.sh's "spawn mosquitto_pub per line" FIFO reader.
 * That pattern forks a brand-new mosquitto_pub process (full TCP connect +
 * MQTT CONNECT/CONNACK handshake + PUBLISH + DISCONNECT) for every single
 * state publish. On a 2-core embedded target, a burst of commands (e.g.
 * rapid frequency tuning, each triggering 2-3 state echoes) queues up
 * dozens of these short-lived connections, and the wall-clock cost of the
 * handshake — not the actual publish — dominates, visible as multi-second
 * lag/"buffering" of status updates behind a fast burst of commands.
 *
 * This tool opens ONE MQTT connection at startup and keeps it open for its
 * entire lifetime (auto-reconnecting if the broker restarts), publishing
 * each line read from stdin as a separate retained message. No new process,
 * no new TCP/MQTT handshake per message — just a lightweight PUBLISH packet
 * write on an already-open socket.
 *
 * Input protocol (stdin, one message per line):
 *   <topic><TAB><payload>\n
 * Split on the FIRST tab only; everything after it (including further tabs)
 * is the payload verbatim. This matches the existing FIFO contract written
 * by api_controller.sh's mqtt_publish() — no change needed on that side.
 *
 * Usage:
 *   mqtt_pubd [-h host] [-p port] [-i client_id] [-q qos] [-n]
 *   -h HOST   Broker host        (default: localhost)
 *   -p PORT   Broker port        (default: 1883)
 *   -i ID     MQTT client id     (default: tezuka_pubd)
 *   -q QOS    Publish QoS 0/1/2  (default: 0)
 *   -n        Do not set the retain flag (default: retained)
 *
 * Build (cross-compile):
 * $(CC) -O2 -o mqtt_pubd mqtt_pubd.c -lmosquitto
 */

#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <poll.h>
#include <errno.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int s) { (void)s; g_running = 0; }

/* Split "topic\tpayload" on the first tab and publish. Malformed lines
 * (no tab) are silently skipped. */
static void publish_line(struct mosquitto *mosq, char *line, size_t len,
                          int qos, bool retain, bool verbose) {
    char *tab = memchr(line, '\t', len);
    if (!tab) return;
    *tab = '\0';
    const char *topic   = line;
    const char *payload = tab + 1;
    size_t payload_len  = len - (size_t)(payload - line);

    int rc = mosquitto_publish(mosq, NULL, topic, (int)payload_len, payload, qos, retain);
    if (rc != MOSQ_ERR_SUCCESS && verbose)
        fprintf(stderr, "mqtt_pubd: publish '%s' failed: %s\n", topic, mosquitto_strerror(rc));
}

static void on_log(struct mosquitto *mosq, void *ud, int level, const char *str) {
    (void)mosq; (void)ud; (void)level;
    fprintf(stderr, "[mqtt_pubd] %s\n", str);
}

static void usage(const char *prog, const char *host, int port, const char *id, int qos) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -h HOST   Broker host        (default: %s)\n"
        "  -p PORT   Broker port        (default: %d)\n"
        "  -i ID     MQTT client id     (default: %s)\n"
        "  -q QOS    Publish QoS 0/1/2  (default: %d)\n"
        "  -n        Do not set the retain flag (default: retained)\n"
        "  -v        Verbose (log connect/disconnect/publish errors)\n"
        "  -H        This help\n",
        prog, host, port, id, qos);
}

int main(int argc, char **argv) {
    const char *host = "localhost";
    int port = 1883;
    const char *client_id = "tezuka_pubd";
    int qos = 0;
    bool retain = true;
    bool verbose = false;

    int opt;
    while ((opt = getopt(argc, argv, "h:p:i:q:nvH")) != -1) {
        switch (opt) {
        case 'h': host      = optarg;       break;
        case 'p': port      = atoi(optarg); break;
        case 'i': client_id = optarg;       break;
        case 'q': qos       = atoi(optarg); break;
        case 'n': retain    = false;        break;
        case 'v': verbose   = true;         break;
        case 'H': usage(argv[0], host, port, client_id, qos); return 0;
        default:  usage(argv[0], host, port, client_id, qos); return 1;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(client_id, true, NULL);
    if (!mosq) { fprintf(stderr, "mqtt_pubd: mosquitto_new failed\n"); return 1; }

    if (verbose) mosquitto_log_callback_set(mosq, on_log);
    /* Auto-reconnect with capped exponential backoff — keeps the single
     * connection alive across broker restarts without our involvement. */
    mosquitto_reconnect_delay_set(mosq, 1, 10, true);

    while (g_running && mosquitto_connect(mosq, host, port, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mqtt_pubd: connect to %s:%d failed, retrying\n", host, port);
        sleep(1);
    }
    if (!g_running) { mosquitto_destroy(mosq); mosquitto_lib_cleanup(); return 0; }

    /* Background thread owns the socket: handles keepalives, reconnects,
     * and lets mosquitto_publish() below just enqueue + return immediately. */
    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mqtt_pubd: loop_start failed\n");
        mosquitto_destroy(mosq); mosquitto_lib_cleanup(); return 1;
    }

    /* Bounded-timeout poll() instead of a blocking getline(): mosquitto_loop_start()
     * runs its network I/O on a separate thread, and kill(pid, SIGTERM) is delivered
     * to an arbitrary unblocked thread in the process — if it lands on that thread
     * instead of this one, a blocking read would never notice g_running went false
     * and this process would leak as an orphan. Re-checking g_running on every
     * poll timeout, independent of signal delivery, guarantees prompt clean exit. */
    char rbuf[4096];
    size_t rlen = 0;
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };

    while (g_running) {
        int pr = poll(&pfd, 1, 500);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) continue; /* timeout — just recheck g_running */

        if (pfd.revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, rbuf + rlen, sizeof(rbuf) - rlen - 1);
            if (n <= 0) break; /* EOF (writer closed) or error */
            rlen += (size_t)n;
            rbuf[rlen] = '\0';

            char *start = rbuf, *nl;
            while ((nl = memchr(start, '\n', rlen - (size_t)(start - rbuf)))) {
                *nl = '\0';
                publish_line(mosq, start, (size_t)(nl - start), qos, retain, verbose);
                start = nl + 1;
            }
            size_t remaining = rlen - (size_t)(start - rbuf);
            memmove(rbuf, start, remaining);
            rlen = remaining;
            if (rlen == sizeof(rbuf) - 1) rlen = 0; /* pathological: no '\n', drop */
        } else if (pfd.revents & (POLLHUP | POLLERR)) {
            break;
        }
    }

    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}
