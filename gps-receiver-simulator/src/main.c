/*
 * main.c - GPS receiver application.
 *
 * Reads a raw NMEA byte stream from stdin (pipe, file, or a PTY slave), feeds
 * it to the receiver layer and reports results.
 *
 *   default  : human-readable summary block at EOF, errors on stderr
 *   --stream : machine-readable "FIX ..." lines on stdout as data is accepted,
 *              consumed by dashboard/gps_dashboard.py and the tests
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gps_receiver.h"

#define READ_CHUNK_BYTES 256
#define POLL_INTERVAL_MS 250

/* Set from the signal handler; sig_atomic_t + volatile is the portable way. */
static volatile sig_atomic_t g_stop_requested = 0;

static void on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

typedef struct {
    int stream_mode;
    uint32_t timeout_ms;
} app_options_t;

/* ------------------------------------------------------------ output */

static void print_stream_fix(const gps_receiver_t *rx, const nmea_message_t *msg,
                             void *user)
{
    (void)user;
    const gps_fix_t *f = &rx->fix;
    printf("FIX source=%s valid=%d lat=%.6f lon=%.6f alt=%.1f speed_kn=%.1f "
           "speed_kmh=%.1f course=%.1f utc=%02u:%02u:%02u date=%04u-%02u-%02u "
           "quality=%d sats=%u hdop=%.1f\n",
           nmea_type_str(msg->type), f->nav_valid ? 1 : 0, f->latitude_deg,
           f->longitude_deg, f->altitude_m, f->speed_knots, f->speed_kmh,
           f->course_deg, f->time.hour, f->time.minute, f->time.second,
           f->date.year, f->date.month, f->date.day, (int)f->fix_quality,
           (unsigned)f->satellites_used, f->hdop);
    fflush(stdout);
}

/* Print the offending sentence with non-printable bytes made visible. */
static void report_reject(const gps_receiver_t *rx, nmea_status_t reason,
                          const char *sentence, size_t len, void *user)
{
    (void)rx;
    (void)user;
    char shown[NMEA_MAX_SENTENCE_LEN + 1];
    size_t n = (len < NMEA_MAX_SENTENCE_LEN) ? len : NMEA_MAX_SENTENCE_LEN;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)sentence[i];
        shown[i] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
    }
    shown[n] = '\0';
    fprintf(stderr, "REJECT reason=\"%s\" sentence=\"%s\"\n", nmea_status_str(reason),
            shown);
}

static const char *freshness_str(gps_freshness_t f)
{
    switch (f) {
    case GPS_DATA_FRESH: return "FRESH";
    case GPS_DATA_TIMEOUT: return "TIMEOUT";
    case GPS_DATA_NONE: break;
    }
    return "NO DATA";
}

static void print_stats_line(const gps_receiver_t *rx)
{
    const gps_rx_stats_t *s = &rx->stats;
    printf("STATS valid=%u invalid=%u checksum=%u format=%u overflow=%u "
           "incomplete=%u ignored=%u timeout=%d\n",
           s->accepted, gps_receiver_invalid_count(rx), s->rejected_checksum,
           s->rejected_format, s->rejected_overflow, s->rejected_incomplete,
           s->ignored_unsupported, rx->fix.freshness == GPS_DATA_TIMEOUT ? 1 : 0);
}

static void print_summary(const gps_receiver_t *rx)
{
    const gps_fix_t *f = &rx->fix;
    printf("\n========== GPS RECEIVER ==========\n\n");
    printf("GPS FIX STATUS   : %s\n", f->nav_valid ? "VALID" : "NO FIX");
    printf("DATA STATUS      : %s\n", freshness_str(f->freshness));
    if (f->has_position) {
        printf("LATITUDE         : %.6f%s\n", f->latitude_deg,
               f->nav_valid ? "" : " (last known)");
        printf("LONGITUDE        : %.6f%s\n", f->longitude_deg,
               f->nav_valid ? "" : " (last known)");
    } else {
        printf("LATITUDE         : N/A\nLONGITUDE        : N/A\n");
    }
    printf("ALTITUDE         : %.1f m\n", f->altitude_m);
    printf("SPEED            : %.1f knots (%.1f km/h)\n", f->speed_knots, f->speed_kmh);
    printf("COURSE           : %05.1f degrees\n", f->course_deg);
    printf("UTC              : %04u-%02u-%02u %02u:%02u:%02u\n", f->date.year,
           f->date.month, f->date.day, f->time.hour, f->time.minute, f->time.second);
    printf("SATELLITES USED  : %u (in view: %u)\n", (unsigned)f->satellites_used,
           (unsigned)f->satellites_in_view);
    printf("HDOP             : %.1f\n\n", f->hdop);
    printf("VALID MESSAGES   : %u\n", rx->stats.accepted);
    printf("INVALID MESSAGES : %u\n", gps_receiver_invalid_count(rx));
    printf("  checksum       : %u\n", rx->stats.rejected_checksum);
    printf("  format/fields  : %u\n", rx->stats.rejected_format);
    printf("  overflow       : %u\n", rx->stats.rejected_overflow);
    printf("  incomplete     : %u\n", rx->stats.rejected_incomplete);
    printf("IGNORED (type)   : %u\n\n", rx->stats.ignored_unsupported);
    printf("==================================\n");
}

/* ----------------------------------------------------------- arguments */

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: gps_receiver [--stream] [--timeout-ms N] [--help]\n"
            "Reads NMEA 0183 from stdin until EOF.\n"
            "  --stream         print FIX/STATS lines instead of a summary block\n"
            "  --timeout-ms N   data timeout in ms (default %u)\n",
            GPS_DEFAULT_TIMEOUT_MS);
}

static int parse_args(int argc, char **argv, app_options_t *opt)
{
    opt->stream_mode = 0;
    opt->timeout_ms = GPS_DEFAULT_TIMEOUT_MS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stream") == 0) {
            opt->stream_mode = 1;
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (*end != '\0' || v <= 0 || v > 3600000L) {
                fprintf(stderr, "invalid --timeout-ms value\n");
                return -1;
            }
            opt->timeout_ms = (uint32_t)v;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 1;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(stderr);
            return -1;
        }
    }
    return 0;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* ------------------------------------------------------------ main loop */

static void check_timeout(gps_receiver_t *rx, const app_options_t *opt)
{
    gps_freshness_t before = rx->fix.freshness;
    gps_freshness_t after = gps_receiver_update_freshness(rx, monotonic_ms());
    if (before != GPS_DATA_TIMEOUT && after == GPS_DATA_TIMEOUT) {
        if (opt->stream_mode) {
            printf("EVENT timeout\n");
            fflush(stdout);
        } else {
            fprintf(stderr, "WARNING: no valid GPS data for %u ms\n", rx->timeout_ms);
        }
    }
}

/* Returns 0 on EOF/stop, -1 on a read error. */
static int run_receiver(gps_receiver_t *rx, const app_options_t *opt)
{
    uint8_t chunk[READ_CHUNK_BYTES];
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};

    while (!g_stop_requested) {
        int ready = poll(&pfd, 1, POLL_INTERVAL_MS);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            return -1;
        }
        if (ready == 0) {
            check_timeout(rx, opt); /* idle line: detect GPS data loss */
            continue;
        }

        ssize_t n = read(STDIN_FILENO, chunk, sizeof chunk);
        if (n > 0) {
            gps_receiver_feed(rx, chunk, (size_t)n, monotonic_ms());
        } else if (n == 0) {
            return 0; /* EOF */
        } else if (errno == EIO) {
            return 0; /* PTY master closed: the "device" went away */
        } else if (errno != EINTR && errno != EAGAIN) {
            perror("read");
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    app_options_t opt;
    int rc = parse_args(argc, argv, &opt);
    if (rc != 0) {
        return (rc > 0) ? 0 : 2;
    }

    gps_receiver_t rx;
    gps_receiver_init(&rx, opt.timeout_ms);
    gps_receiver_set_callbacks(&rx, opt.stream_mode ? print_stream_fix : NULL,
                               report_reject, NULL);
    install_signal_handlers();

    int status = run_receiver(&rx, &opt);
    gps_receiver_finish(&rx);
    gps_receiver_update_freshness(&rx, monotonic_ms());

    if (opt.stream_mode) {
        print_stats_line(&rx);
    } else {
        print_summary(&rx);
    }
    return (status == 0) ? 0 : 1;
}
