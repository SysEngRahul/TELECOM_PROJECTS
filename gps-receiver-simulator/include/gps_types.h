/*
 * gps_types.h - Navigation data model shared by all receiver modules.
 *
 * All units are documented per field. The structure holds the *latest valid*
 * navigation state; it is only ever modified by gps_receiver.c after a
 * sentence has passed framing, checksum and field validation.
 */
#ifndef GPS_TYPES_H
#define GPS_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define GPS_KNOTS_TO_KMH 1.852 /* 1 knot = 1 nautical mile/h = 1.852 km/h */

/* UTC time of day as reported by the receiver (hhmmss.sss). */
typedef struct {
    uint8_t hour;         /* 0..23 */
    uint8_t minute;       /* 0..59 */
    uint8_t second;       /* 0..59 */
    uint16_t millisecond; /* 0..999 */
    bool valid;           /* false if the field was absent */
} gps_utc_time_t;

/* UTC calendar date (ddmmyy in RMC, expanded to a four digit year). */
typedef struct {
    uint8_t day;   /* 1..31 */
    uint8_t month; /* 1..12 */
    uint16_t year; /* e.g. 2024 (two digit year pivot: >=80 -> 19xx, else 20xx) */
    bool valid;
} gps_utc_date_t;

/* GGA fix quality indicator (NMEA 0183 field). */
typedef enum {
    GPS_FIX_INVALID = 0,
    GPS_FIX_GPS = 1,
    GPS_FIX_DGPS = 2,
    GPS_FIX_PPS = 3,
    GPS_FIX_RTK = 4,
    GPS_FIX_FLOAT_RTK = 5,
    GPS_FIX_ESTIMATED = 6,
    GPS_FIX_MANUAL = 7,
    GPS_FIX_SIMULATION = 8
} gps_fix_quality_t;

/* Data freshness, driven by a monotonic clock supplied by the caller. */
typedef enum {
    GPS_DATA_NONE = 0,   /* nothing valid received yet */
    GPS_DATA_FRESH = 1,  /* a valid sentence arrived within the timeout */
    GPS_DATA_TIMEOUT = 2 /* no valid sentence for longer than the timeout */
} gps_freshness_t;

typedef struct {
    double latitude_deg;   /* decimal degrees, +N / -S, -90..90 */
    double longitude_deg;  /* decimal degrees, +E / -W, -180..180 */
    double altitude_m;     /* metres above mean sea level (GGA) */
    double speed_knots;    /* speed over ground, knots (RMC) */
    double speed_kmh;      /* speed over ground, km/h (derived) */
    double course_deg;     /* course over ground, degrees true, 0..360 */
    double hdop;           /* horizontal dilution of precision (unitless) */

    gps_utc_time_t time;
    gps_utc_date_t date;

    gps_fix_quality_t fix_quality; /* from GGA */
    uint8_t fix_mode;              /* from GSA: 1 = no fix, 2 = 2D, 3 = 3D */
    uint8_t satellites_used;       /* from GGA */
    uint8_t satellites_in_view;    /* from GSV */

    bool nav_valid;    /* latest fix-status sentence says "valid" */
    bool has_position; /* a valid position was received at least once; when
                          nav_valid is false the position is "last known" */
    gps_freshness_t freshness;
    uint64_t last_update_ms; /* monotonic ms of the last accepted sentence */
} gps_fix_t;

#endif /* GPS_TYPES_H */
