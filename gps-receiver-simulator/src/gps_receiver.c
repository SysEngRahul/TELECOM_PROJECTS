#include "gps_receiver.h"

#include <string.h>

void gps_receiver_init(gps_receiver_t *rx, uint32_t timeout_ms)
{
    memset(rx, 0, sizeof *rx);
    rx->state = GPS_RX_IDLE;
    rx->timeout_ms = (timeout_ms == 0) ? GPS_DEFAULT_TIMEOUT_MS : timeout_ms;
    rx->fix.freshness = GPS_DATA_NONE;
}

void gps_receiver_set_callbacks(gps_receiver_t *rx, gps_accept_cb on_accept,
                                gps_reject_cb on_reject, void *user)
{
    rx->on_accept = on_accept;
    rx->on_reject = on_reject;
    rx->user = user;
}

uint32_t gps_receiver_invalid_count(const gps_receiver_t *rx)
{
    return rx->stats.rejected_checksum + rx->stats.rejected_format +
           rx->stats.rejected_overflow + rx->stats.rejected_incomplete;
}

/* ------------------------------------------------------- state updates */

static void apply_rmc(gps_fix_t *fix, const nmea_rmc_t *r)
{
    if (r->time.valid) {
        fix->time = r->time;
    }
    if (r->date.valid) {
        fix->date = r->date;
    }
    fix->nav_valid = r->valid;
    if (r->valid) {
        fix->latitude_deg = r->latitude_deg;
        fix->longitude_deg = r->longitude_deg;
        fix->has_position = true;
        if (r->has_speed) {
            fix->speed_knots = r->speed_knots;
            fix->speed_kmh = r->speed_knots * GPS_KNOTS_TO_KMH;
        }
        if (r->has_course) {
            fix->course_deg = r->course_deg;
        }
    }
}

static void apply_gga(gps_fix_t *fix, const nmea_gga_t *g)
{
    if (g->time.valid) {
        fix->time = g->time;
    }
    fix->fix_quality = g->quality;
    fix->nav_valid = (g->quality != GPS_FIX_INVALID);
    if (g->has_satellites) {
        fix->satellites_used = g->satellites_used;
    }
    if (g->quality != GPS_FIX_INVALID) {
        if (g->has_position) {
            fix->latitude_deg = g->latitude_deg;
            fix->longitude_deg = g->longitude_deg;
            fix->has_position = true;
        }
        if (g->has_hdop) {
            fix->hdop = g->hdop;
        }
        if (g->has_altitude) {
            fix->altitude_m = g->altitude_m;
        }
    }
}

static void apply_message(gps_fix_t *fix, const nmea_message_t *msg)
{
    switch (msg->type) {
    case NMEA_TYPE_RMC:
        apply_rmc(fix, &msg->u.rmc);
        break;
    case NMEA_TYPE_GGA:
        apply_gga(fix, &msg->u.gga);
        break;
    case NMEA_TYPE_GSA:
        fix->fix_mode = msg->u.gsa.fix_mode;
        if (msg->u.gsa.has_hdop) {
            fix->hdop = msg->u.gsa.hdop;
        }
        break;
    case NMEA_TYPE_GSV:
        fix->satellites_in_view = msg->u.gsv.satellites_in_view;
        break;
    case NMEA_TYPE_NONE:
        break;
    }
}

/* ------------------------------------------------------ sentence handling */

static void reject(gps_receiver_t *rx, nmea_status_t reason, const char *sentence,
                   size_t len)
{
    switch (reason) {
    case NMEA_ERR_CHECKSUM_MISSING:
    case NMEA_ERR_CHECKSUM_MALFORMED:
    case NMEA_ERR_CHECKSUM_MISMATCH:
        rx->stats.rejected_checksum++;
        break;
    case NMEA_ERR_OVERFLOW:
    case NMEA_ERR_TOO_LONG:
        rx->stats.rejected_overflow++;
        break;
    case NMEA_ERR_INCOMPLETE:
        rx->stats.rejected_incomplete++;
        break;
    default:
        rx->stats.rejected_format++;
        break;
    }
    if (rx->on_reject != NULL) {
        rx->on_reject(rx, reason, sentence, len, rx->user);
    }
}

/* Returns 1 if the sentence was accepted. */
static size_t process_sentence(gps_receiver_t *rx, uint64_t now_ms)
{
    nmea_message_t msg;
    nmea_status_t status;

    if (rx->corrupt) {
        status = NMEA_ERR_FRAMING;
    } else {
        status = nmea_parse_sentence(rx->buffer, rx->length, &msg);
    }

    if (status == NMEA_OK) {
        apply_message(&rx->fix, &msg);
        rx->fix.freshness = GPS_DATA_FRESH;
        rx->fix.last_update_ms = now_ms;
        rx->stats.accepted++;
        if (rx->on_accept != NULL) {
            rx->on_accept(rx, &msg, rx->user);
        }
        return 1;
    }
    if (status == NMEA_ERR_UNSUPPORTED) {
        rx->stats.ignored_unsupported++;
        return 0;
    }
    reject(rx, status, rx->buffer, rx->length);
    return 0;
}

static void start_sentence(gps_receiver_t *rx)
{
    rx->buffer[0] = '$';
    rx->length = 1;
    rx->corrupt = 0;
    rx->state = GPS_RX_COLLECTING;
}

static size_t handle_byte(gps_receiver_t *rx, uint8_t byte, uint64_t now_ms)
{
    size_t accepted = 0;

    if (byte == '$') {
        /* '$' always restarts framing: a sentence in progress was cut off. */
        if (rx->state == GPS_RX_COLLECTING) {
            reject(rx, NMEA_ERR_INCOMPLETE, rx->buffer, rx->length);
        }
        start_sentence(rx);
        return 0;
    }

    switch (rx->state) {
    case GPS_RX_IDLE:
        rx->stats.bytes_discarded++;
        break;

    case GPS_RX_OVERFLOW:
        if (byte == '\r' || byte == '\n') {
            rx->state = GPS_RX_IDLE; /* resynchronised at end of line */
        }
        break;

    case GPS_RX_COLLECTING:
        if (byte == '\r' || byte == '\n') {
            accepted = process_sentence(rx, now_ms);
            rx->length = 0;
            rx->state = GPS_RX_IDLE;
        } else if (rx->length >= NMEA_MAX_BODY_LEN) {
            /* Buffer is full: report once, then drop bytes until the line ends. */
            reject(rx, NMEA_ERR_OVERFLOW, rx->buffer, rx->length);
            rx->length = 0;
            rx->state = GPS_RX_OVERFLOW;
        } else {
            if (byte < 0x20 || byte > 0x7E) {
                rx->corrupt = 1; /* rejected when the line ends */
            }
            rx->buffer[rx->length++] = (char)byte;
        }
        break;
    }
    return accepted;
}

size_t gps_receiver_feed(gps_receiver_t *rx, const uint8_t *data, size_t len,
                         uint64_t now_ms)
{
    size_t accepted = 0;
    if (rx == NULL || data == NULL) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        accepted += handle_byte(rx, data[i], now_ms);
    }
    return accepted;
}

void gps_receiver_finish(gps_receiver_t *rx)
{
    if (rx != NULL && rx->state == GPS_RX_COLLECTING) {
        reject(rx, NMEA_ERR_INCOMPLETE, rx->buffer, rx->length);
    }
    if (rx != NULL) {
        rx->length = 0;
        rx->state = GPS_RX_IDLE;
    }
}

gps_freshness_t gps_receiver_update_freshness(gps_receiver_t *rx, uint64_t now_ms)
{
    if (rx->fix.freshness == GPS_DATA_NONE) {
        return GPS_DATA_NONE;
    }
    /* Unsigned subtraction is safe: now_ms is monotonic and >= last_update_ms;
       guard anyway so a misbehaving caller cannot wrap around. */
    if (now_ms >= rx->fix.last_update_ms &&
        (now_ms - rx->fix.last_update_ms) > rx->timeout_ms) {
        rx->fix.freshness = GPS_DATA_TIMEOUT;
    } else {
        rx->fix.freshness = GPS_DATA_FRESH;
    }
    return rx->fix.freshness;
}
