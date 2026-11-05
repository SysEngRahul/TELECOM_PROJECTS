/*
 * nmea_parser.h - Validate and parse one complete NMEA 0183 sentence.
 *
 * Input is a single sentence "$....*HH" WITHOUT the CR/LF terminator.
 * Stream framing (finding sentence boundaries) is the receiver's job.
 *
 * Supported: RMC, GGA, GSA, GSV with talker IDs GP, GN, GL, GA, GB, BD.
 */
#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gps_types.h"

/* NMEA 0183 limit: 82 characters including '$' and CR/LF. */
#define NMEA_MAX_SENTENCE_LEN 82
/* Longest sentence we accept once CR/LF is stripped. */
#define NMEA_MAX_BODY_LEN (NMEA_MAX_SENTENCE_LEN - 2)

typedef enum {
    NMEA_OK = 0,
    NMEA_ERR_NULL,
    NMEA_ERR_FRAMING,            /* missing '$', stray '$', non-printable chars */
    NMEA_ERR_TOO_LONG,
    NMEA_ERR_CHECKSUM_MISSING,
    NMEA_ERR_CHECKSUM_MALFORMED,
    NMEA_ERR_CHECKSUM_MISMATCH,
    NMEA_ERR_TOO_FEW_FIELDS,
    NMEA_ERR_TOO_MANY_FIELDS,
    NMEA_ERR_BAD_FIELD,          /* present but unparsable / out of range */
    NMEA_ERR_MISSING_FIELD,      /* required (for this fix state) but empty */
    NMEA_ERR_UNSUPPORTED,        /* well-formed, checksum OK, unknown type */
    NMEA_ERR_INCOMPLETE,         /* (receiver) sentence cut off by a new '$' */
    NMEA_ERR_OVERFLOW            /* (receiver) sentence exceeded max length */
} nmea_status_t;

typedef enum {
    NMEA_TYPE_NONE = 0,
    NMEA_TYPE_RMC,
    NMEA_TYPE_GGA,
    NMEA_TYPE_GSA,
    NMEA_TYPE_GSV
} nmea_type_t;

typedef struct {
    gps_utc_time_t time;
    gps_utc_date_t date;
    bool valid;         /* status field 'A' */
    bool has_position;
    double latitude_deg;
    double longitude_deg;
    bool has_speed;
    double speed_knots;
    bool has_course;
    double course_deg;
} nmea_rmc_t;

typedef struct {
    gps_utc_time_t time;
    bool has_position;
    double latitude_deg;
    double longitude_deg;
    gps_fix_quality_t quality;
    bool has_satellites;
    uint8_t satellites_used;
    bool has_hdop;
    double hdop;
    bool has_altitude;
    double altitude_m;
} nmea_gga_t;

typedef struct {
    uint8_t fix_mode; /* 1 = no fix, 2 = 2D, 3 = 3D */
    bool has_pdop, has_hdop, has_vdop;
    double pdop, hdop, vdop;
} nmea_gsa_t;

typedef struct {
    uint8_t total_messages;
    uint8_t message_number;
    uint8_t satellites_in_view;
} nmea_gsv_t;

typedef struct {
    nmea_type_t type;
    char talker[3]; /* e.g. "GP", "GN" */
    union {
        nmea_rmc_t rmc;
        nmea_gga_t gga;
        nmea_gsa_t gsa;
        nmea_gsv_t gsv;
    } u;
} nmea_message_t;

/*
 * Parse one sentence. On anything other than NMEA_OK, *msg is zeroed and
 * must not be used. The checksum is mandatory.
 */
nmea_status_t nmea_parse_sentence(const char *sentence, size_t len,
                                  nmea_message_t *msg);

const char *nmea_status_str(nmea_status_t status);
const char *nmea_type_str(nmea_type_t type);

#endif /* NMEA_PARSER_H */
