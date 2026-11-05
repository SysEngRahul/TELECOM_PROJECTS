#include "nmea_parser.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "coordinate_converter.h"
#include "nmea_checksum.h"

#define MAX_FIELDS 24 /* GSV has at most 20 fields, GSA 19 */

typedef struct {
    const char *field[MAX_FIELDS];
    size_t count;
} field_list_t;

/* ---------------------------------------------------------------- helpers */

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/*
 * Split "a,b,,d" into fields in-place (commas become NUL). Empty fields are
 * preserved as empty strings; the caller must check field counts.
 */
static nmea_status_t split_fields(char *text, field_list_t *list)
{
    list->count = 0;
    list->field[list->count++] = text;
    for (char *p = text; *p != '\0'; p++) {
        if (*p == ',') {
            *p = '\0';
            if (list->count >= MAX_FIELDS) {
                return NMEA_ERR_TOO_MANY_FIELDS;
            }
            list->field[list->count++] = p + 1;
        }
    }
    return NMEA_OK;
}

/* Strict decimal: [+-]digits[.digits]; rejects "", "1e5", "nan", "1.2.3". */
static bool parse_double(const char *s, double *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    const char *p = s;
    if (*p == '-' || *p == '+') {
        p++;
    }
    bool digits = false;
    bool dot = false;
    for (; *p != '\0'; p++) {
        if (is_digit(*p)) {
            digits = true;
        } else if (*p == '.' && !dot) {
            dot = true;
        } else {
            return false;
        }
    }
    if (!digits) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    double value = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0' || !isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_uint(const char *s, unsigned max_value, unsigned *out)
{
    if (s == NULL || *s == '\0' || strlen(s) > 9) {
        return false;
    }
    unsigned value = 0;
    for (const char *p = s; *p != '\0'; p++) {
        if (!is_digit(*p)) {
            return false;
        }
        value = value * 10u + (unsigned)(*p - '0');
    }
    if (value > max_value) {
        return false;
    }
    *out = value;
    return true;
}

static unsigned two_digits(const char *p)
{
    return (unsigned)((p[0] - '0') * 10 + (p[1] - '0'));
}

/* hhmmss or hhmmss.sss */
static bool parse_time(const char *s, gps_utc_time_t *t)
{
    size_t len = strlen(s);
    if (len < 6) {
        return false;
    }
    for (size_t i = 0; i < 6; i++) {
        if (!is_digit(s[i])) {
            return false;
        }
    }
    unsigned millis = 0;
    if (len > 6) {
        if (s[6] != '.' || len == 7) {
            return false;
        }
        unsigned scale = 100;
        for (size_t i = 7; i < len; i++) {
            if (!is_digit(s[i])) {
                return false;
            }
            if (scale > 0) {
                millis += (unsigned)(s[i] - '0') * scale;
                scale /= 10;
            }
        }
    }
    unsigned hour = two_digits(s);
    unsigned minute = two_digits(s + 2);
    unsigned second = two_digits(s + 4);
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    t->hour = (uint8_t)hour;
    t->minute = (uint8_t)minute;
    t->second = (uint8_t)second;
    t->millisecond = (uint16_t)millis;
    t->valid = true;
    return true;
}

static bool is_leap_year(unsigned year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* ddmmyy */
static bool parse_date(const char *s, gps_utc_date_t *d)
{
    if (strlen(s) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; i++) {
        if (!is_digit(s[i])) {
            return false;
        }
    }
    unsigned day = two_digits(s);
    unsigned month = two_digits(s + 2);
    unsigned yy = two_digits(s + 4);
    unsigned year = (yy >= 80) ? 1900u + yy : 2000u + yy;

    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                              31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    unsigned max_day = days_in_month[month - 1];
    if (month == 2 && is_leap_year(year)) {
        max_day = 29;
    }
    if (day > max_day) {
        return false;
    }
    d->day = (uint8_t)day;
    d->month = (uint8_t)month;
    d->year = (uint16_t)year;
    d->valid = true;
    return true;
}

/*
 * lat, N/S, lon, E/W. All four empty -> *present = false (not an error).
 * Some but not all empty -> BAD_FIELD. Otherwise convert and range-check.
 */
static nmea_status_t parse_position(const char *lat, const char *ns,
                                    const char *lon, const char *ew,
                                    bool *present, double *lat_out, double *lon_out)
{
    *present = false;
    bool any = lat[0] || ns[0] || lon[0] || ew[0];
    bool all = lat[0] && ns[0] && lon[0] && ew[0];
    if (!any) {
        return NMEA_OK;
    }
    if (!all || strlen(ns) != 1 || strlen(ew) != 1) {
        return NMEA_ERR_BAD_FIELD;
    }
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    if (coord_nmea_to_decimal(lat, ns[0], COORD_AXIS_LATITUDE, &lat_deg) != COORD_OK ||
        coord_nmea_to_decimal(lon, ew[0], COORD_AXIS_LONGITUDE, &lon_deg) != COORD_OK) {
        return NMEA_ERR_BAD_FIELD;
    }
    *lat_out = lat_deg;
    *lon_out = lon_deg;
    *present = true;
    return NMEA_OK;
}

/* ------------------------------------------------------------ sentences */

static nmea_status_t parse_rmc(const field_list_t *fl, nmea_rmc_t *r)
{
    /* 0 id, 1 time, 2 status, 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed(kn),
       8 course, 9 date, [10 magvar, 11 dir, 12 mode, 13 navstatus] */
    if (fl->count < 10) {
        return NMEA_ERR_TOO_FEW_FIELDS;
    }
    const char *const *f = fl->field;

    if (f[1][0] != '\0' && !parse_time(f[1], &r->time)) {
        return NMEA_ERR_BAD_FIELD;
    }
    if (strcmp(f[2], "A") == 0) {
        r->valid = true;
    } else if (strcmp(f[2], "V") == 0) {
        r->valid = false;
    } else {
        return NMEA_ERR_BAD_FIELD;
    }

    nmea_status_t st = parse_position(f[3], f[4], f[5], f[6], &r->has_position,
                                      &r->latitude_deg, &r->longitude_deg);
    if (st != NMEA_OK) {
        return st;
    }
    if (f[7][0] != '\0') {
        if (!parse_double(f[7], &r->speed_knots) || r->speed_knots < 0.0 ||
            r->speed_knots > 5000.0) {
            return NMEA_ERR_BAD_FIELD;
        }
        r->has_speed = true;
    }
    if (f[8][0] != '\0') {
        if (!parse_double(f[8], &r->course_deg) || r->course_deg < 0.0 ||
            r->course_deg > 360.0) {
            return NMEA_ERR_BAD_FIELD;
        }
        r->has_course = true;
    }
    if (f[9][0] != '\0' && !parse_date(f[9], &r->date)) {
        return NMEA_ERR_BAD_FIELD;
    }

    /* A claimed valid fix must carry the data that makes it a fix. */
    if (r->valid && (!r->has_position || !r->time.valid || !r->date.valid)) {
        return NMEA_ERR_MISSING_FIELD;
    }
    return NMEA_OK;
}

static nmea_status_t parse_gga(const field_list_t *fl, nmea_gga_t *g)
{
    /* 0 id, 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 quality, 7 sats, 8 hdop,
       9 altitude, 10 'M', [11 geoid sep, 12 'M', 13 age, 14 station] */
    if (fl->count < 10) {
        return NMEA_ERR_TOO_FEW_FIELDS;
    }
    const char *const *f = fl->field;

    if (f[1][0] != '\0' && !parse_time(f[1], &g->time)) {
        return NMEA_ERR_BAD_FIELD;
    }
    nmea_status_t st = parse_position(f[2], f[3], f[4], f[5], &g->has_position,
                                      &g->latitude_deg, &g->longitude_deg);
    if (st != NMEA_OK) {
        return st;
    }

    unsigned quality = 0;
    if (!parse_uint(f[6], 8, &quality)) {
        return NMEA_ERR_BAD_FIELD;
    }
    g->quality = (gps_fix_quality_t)quality;

    if (f[7][0] != '\0') {
        unsigned sats = 0;
        if (!parse_uint(f[7], 99, &sats)) {
            return NMEA_ERR_BAD_FIELD;
        }
        g->satellites_used = (uint8_t)sats;
        g->has_satellites = true;
    }
    if (f[8][0] != '\0') {
        if (!parse_double(f[8], &g->hdop) || g->hdop < 0.0 || g->hdop > 999.0) {
            return NMEA_ERR_BAD_FIELD;
        }
        g->has_hdop = true;
    }
    if (f[9][0] != '\0') {
        if (!parse_double(f[9], &g->altitude_m) || fabs(g->altitude_m) > 100000.0) {
            return NMEA_ERR_BAD_FIELD;
        }
        if (fl->count > 10 && f[10][0] != '\0' && strcmp(f[10], "M") != 0) {
            return NMEA_ERR_BAD_FIELD; /* altitude unit must be metres */
        }
        g->has_altitude = true;
    }

    if (g->quality != GPS_FIX_INVALID &&
        (!g->has_position || !g->time.valid)) {
        return NMEA_ERR_MISSING_FIELD;
    }
    return NMEA_OK;
}

static nmea_status_t parse_gsa(const field_list_t *fl, nmea_gsa_t *g)
{
    /* 0 id, 1 mode M/A, 2 fix type, 3..14 PRNs, 15 PDOP, 16 HDOP, 17 VDOP */
    if (fl->count < 18) {
        return NMEA_ERR_TOO_FEW_FIELDS;
    }
    const char *const *f = fl->field;

    if (strcmp(f[1], "A") != 0 && strcmp(f[1], "M") != 0) {
        return NMEA_ERR_BAD_FIELD;
    }
    unsigned mode = 0;
    if (!parse_uint(f[2], 3, &mode) || mode < 1) {
        return NMEA_ERR_BAD_FIELD;
    }
    g->fix_mode = (uint8_t)mode;

    double *values[3] = {&g->pdop, &g->hdop, &g->vdop};
    bool *flags[3] = {&g->has_pdop, &g->has_hdop, &g->has_vdop};
    for (size_t i = 0; i < 3; i++) {
        const char *text = f[15 + i];
        if (text[0] != '\0') {
            if (!parse_double(text, values[i]) || *values[i] < 0.0) {
                return NMEA_ERR_BAD_FIELD;
            }
            *flags[i] = true;
        }
    }
    return NMEA_OK;
}

static nmea_status_t parse_gsv(const field_list_t *fl, nmea_gsv_t *g)
{
    /* 0 id, 1 total msgs, 2 msg number, 3 sats in view, then 4 per satellite */
    if (fl->count < 4) {
        return NMEA_ERR_TOO_FEW_FIELDS;
    }
    unsigned total = 0, number = 0, in_view = 0;
    if (!parse_uint(fl->field[1], 99, &total) || total < 1 ||
        !parse_uint(fl->field[2], 99, &number) || number < 1 || number > total ||
        !parse_uint(fl->field[3], 99, &in_view)) {
        return NMEA_ERR_BAD_FIELD;
    }
    g->total_messages = (uint8_t)total;
    g->message_number = (uint8_t)number;
    g->satellites_in_view = (uint8_t)in_view;
    return NMEA_OK;
}

/* -------------------------------------------------------------- dispatch */

static bool talker_supported(const char *id)
{
    static const char *const talkers[] = {"GP", "GN", "GL", "GA", "GB", "BD"};
    for (size_t i = 0; i < sizeof talkers / sizeof talkers[0]; i++) {
        if (id[0] == talkers[i][0] && id[1] == talkers[i][1]) {
            return true;
        }
    }
    return false;
}

static nmea_status_t map_checksum_status(nmea_checksum_status_t cs)
{
    switch (cs) {
    case NMEA_CS_OK: return NMEA_OK;
    case NMEA_CS_ERR_FRAMING: return NMEA_ERR_FRAMING;
    case NMEA_CS_ERR_MISSING: return NMEA_ERR_CHECKSUM_MISSING;
    case NMEA_CS_ERR_MALFORMED: return NMEA_ERR_CHECKSUM_MALFORMED;
    case NMEA_CS_ERR_MISMATCH: return NMEA_ERR_CHECKSUM_MISMATCH;
    }
    return NMEA_ERR_FRAMING;
}

nmea_status_t nmea_parse_sentence(const char *sentence, size_t len, nmea_message_t *msg)
{
    if (sentence == NULL || msg == NULL) {
        return NMEA_ERR_NULL;
    }
    memset(msg, 0, sizeof *msg);

    if (len > NMEA_MAX_BODY_LEN) {
        return NMEA_ERR_TOO_LONG;
    }
    nmea_status_t status = map_checksum_status(nmea_checksum_validate(sentence, len));
    if (status != NMEA_OK) {
        return status;
    }

    /* Payload sits between '$' and '*'; the checksum check proved '*' exists. */
    const char *star = memchr(sentence, '*', len);
    size_t payload_len = (size_t)(star - sentence) - 1;

    char work[NMEA_MAX_SENTENCE_LEN + 1];
    for (size_t i = 0; i < payload_len; i++) {
        unsigned char c = (unsigned char)sentence[1 + i];
        if (c < 0x20 || c > 0x7E) {
            return NMEA_ERR_FRAMING; /* non-printable inside a sentence */
        }
        work[i] = (char)c;
    }
    work[payload_len] = '\0';

    field_list_t fl;
    status = split_fields(work, &fl);
    if (status != NMEA_OK) {
        return status;
    }

    const char *id = fl.field[0];
    if (strlen(id) != 5) {
        return NMEA_ERR_UNSUPPORTED;
    }
    if (!talker_supported(id)) {
        return NMEA_ERR_UNSUPPORTED;
    }
    msg->talker[0] = id[0];
    msg->talker[1] = id[1];
    msg->talker[2] = '\0';

    const char *type = id + 2;
    if (strcmp(type, "RMC") == 0) {
        msg->type = NMEA_TYPE_RMC;
        status = parse_rmc(&fl, &msg->u.rmc);
    } else if (strcmp(type, "GGA") == 0) {
        msg->type = NMEA_TYPE_GGA;
        status = parse_gga(&fl, &msg->u.gga);
    } else if (strcmp(type, "GSA") == 0) {
        msg->type = NMEA_TYPE_GSA;
        status = parse_gsa(&fl, &msg->u.gsa);
    } else if (strcmp(type, "GSV") == 0) {
        msg->type = NMEA_TYPE_GSV;
        status = parse_gsv(&fl, &msg->u.gsv);
    } else {
        status = NMEA_ERR_UNSUPPORTED;
    }

    if (status != NMEA_OK) {
        memset(msg, 0, sizeof *msg); /* never leave partial data behind */
    }
    return status;
}

const char *nmea_status_str(nmea_status_t status)
{
    switch (status) {
    case NMEA_OK: return "ok";
    case NMEA_ERR_NULL: return "null argument";
    case NMEA_ERR_FRAMING: return "framing error";
    case NMEA_ERR_TOO_LONG: return "sentence too long";
    case NMEA_ERR_CHECKSUM_MISSING: return "checksum missing";
    case NMEA_ERR_CHECKSUM_MALFORMED: return "checksum malformed";
    case NMEA_ERR_CHECKSUM_MISMATCH: return "checksum mismatch";
    case NMEA_ERR_TOO_FEW_FIELDS: return "too few fields";
    case NMEA_ERR_TOO_MANY_FIELDS: return "too many fields";
    case NMEA_ERR_BAD_FIELD: return "invalid field value";
    case NMEA_ERR_MISSING_FIELD: return "required field missing";
    case NMEA_ERR_UNSUPPORTED: return "unsupported sentence type";
    case NMEA_ERR_INCOMPLETE: return "incomplete sentence";
    case NMEA_ERR_OVERFLOW: return "sentence overflow";
    }
    return "unknown";
}

const char *nmea_type_str(nmea_type_t type)
{
    switch (type) {
    case NMEA_TYPE_RMC: return "RMC";
    case NMEA_TYPE_GGA: return "GGA";
    case NMEA_TYPE_GSA: return "GSA";
    case NMEA_TYPE_GSV: return "GSV";
    case NMEA_TYPE_NONE: break;
    }
    return "NONE";
}
