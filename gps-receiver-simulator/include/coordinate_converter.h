/*
 * coordinate_converter.h - NMEA (d)ddmm.mmmm to signed decimal degrees.
 *
 *   decimal = degrees + minutes / 60       (negative for S and W)
 */
#ifndef COORDINATE_CONVERTER_H
#define COORDINATE_CONVERTER_H

typedef enum { COORD_AXIS_LATITUDE = 0, COORD_AXIS_LONGITUDE = 1 } coord_axis_t;

typedef enum {
    COORD_OK = 0,
    COORD_ERR_NULL,
    COORD_ERR_EMPTY,      /* empty field */
    COORD_ERR_FORMAT,     /* wrong digit count, non-digit characters */
    COORD_ERR_RANGE,      /* degrees/minutes out of range */
    COORD_ERR_HEMISPHERE  /* hemisphere is not N/S (lat) or E/W (lon) */
} coord_status_t;

/*
 * field: "ddmm.mmmm" for latitude, "dddmm.mmmm" for longitude.
 * hemisphere: 'N','S' (latitude) or 'E','W' (longitude).
 * *out is only written on COORD_OK.
 */
coord_status_t coord_nmea_to_decimal(const char *field, char hemisphere,
                                     coord_axis_t axis, double *out);

int coord_is_valid_latitude(double degrees);
int coord_is_valid_longitude(double degrees);

const char *coord_status_str(coord_status_t status);

#endif /* COORDINATE_CONVERTER_H */
