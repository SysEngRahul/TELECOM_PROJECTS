/*
 * gps_receiver.h - Byte-stream receiver layer.
 *
 * Models what UART receive firmware does: bytes arrive in arbitrary chunks,
 * a sentence may be split across reads or several sentences may arrive in one
 * read. The receiver accumulates bytes, finds '$' ... CR/LF frames, hands each
 * complete frame to the parser and only updates navigation state from
 * sentences that passed every validation step.
 *
 * Time is injected (now_ms) so the module is deterministic and testable; the
 * application supplies CLOCK_MONOTONIC.
 */
#ifndef GPS_RECEIVER_H
#define GPS_RECEIVER_H

#include <stddef.h>
#include <stdint.h>

#include "gps_types.h"
#include "nmea_parser.h"

#define GPS_DEFAULT_TIMEOUT_MS 5000u

typedef struct {
    uint32_t accepted;             /* sentences that passed all validation */
    uint32_t ignored_unsupported;  /* valid checksum, but not RMC/GGA/GSA/GSV */
    uint32_t rejected_checksum;    /* missing / malformed / mismatching */
    uint32_t rejected_format;      /* framing, field count, bad/missing field */
    uint32_t rejected_overflow;    /* longer than NMEA_MAX_BODY_LEN */
    uint32_t rejected_incomplete;  /* cut off by a new '$' before CR/LF */
    uint32_t bytes_discarded;      /* bytes seen outside any sentence */
} gps_rx_stats_t;

typedef enum { GPS_RX_IDLE = 0, GPS_RX_COLLECTING, GPS_RX_OVERFLOW } gps_rx_state_t;

typedef struct gps_receiver gps_receiver_t;

typedef void (*gps_accept_cb)(const gps_receiver_t *rx, const nmea_message_t *msg,
                              void *user);
typedef void (*gps_reject_cb)(const gps_receiver_t *rx, nmea_status_t reason,
                              const char *sentence, size_t len, void *user);

struct gps_receiver {
    char buffer[NMEA_MAX_SENTENCE_LEN + 1];
    size_t length;
    gps_rx_state_t state;
    int corrupt; /* non-printable byte seen in current sentence */

    gps_fix_t fix;
    gps_rx_stats_t stats;
    uint32_t timeout_ms;

    gps_accept_cb on_accept;
    gps_reject_cb on_reject;
    void *user;
};

void gps_receiver_init(gps_receiver_t *rx, uint32_t timeout_ms);

void gps_receiver_set_callbacks(gps_receiver_t *rx, gps_accept_cb on_accept,
                                gps_reject_cb on_reject, void *user);

/* Feed raw bytes. Returns the number of sentences accepted by this call. */
size_t gps_receiver_feed(gps_receiver_t *rx, const uint8_t *data, size_t len,
                         uint64_t now_ms);

/* Call at end of stream: a sentence still being collected is reported as
   incomplete (it can never be completed). */
void gps_receiver_finish(gps_receiver_t *rx);

/* Re-evaluate freshness against now_ms; returns the resulting state. */
gps_freshness_t gps_receiver_update_freshness(gps_receiver_t *rx, uint64_t now_ms);

/* Sum of all rejected_* counters ("invalid messages"). */
uint32_t gps_receiver_invalid_count(const gps_receiver_t *rx);

#endif /* GPS_RECEIVER_H */
