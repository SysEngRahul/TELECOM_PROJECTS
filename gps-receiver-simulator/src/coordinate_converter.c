#include "coordinate_converter.h"

#include <stdlib.h>

static int is_digit(char c) { return c >= '0' && c <= '9'; }

coord_status_t coord_nmea_to_decimal(const char *field, char hemisphere,
                                     coord_axis_t axis, double *out)
{
    if (field == NULL || out == NULL) {
        return COORD_ERR_NULL;
    }
    if (field[0] == '\0') {
        return COORD_ERR_EMPTY;
    }

    const size_t degree_digits = (axis == COORD_AXIS_LATITUDE) ? 2u : 3u;
    const double max_degrees = (axis == COORD_AXIS_LATITUDE) ? 90.0 : 180.0;

    /* Integer part must be exactly (d)dd + two minute digits. */
    size_t int_len = 0;
    while (is_digit(field[int_len])) {
        int_len++;
    }
    if (int_len != degree_digits + 2u) {
        return COORD_ERR_FORMAT;
    }

    const char *rest = field + int_len;
    if (*rest == '.') {
        rest++;
        if (!is_digit(*rest)) { /* "4807." or "4807.x" */
            return COORD_ERR_FORMAT;
        }
    }
    for (const char *p = rest; *p != '\0'; p++) {
        if (!is_digit(*p)) {
            return COORD_ERR_FORMAT;
        }
    }

    double degrees = 0.0;
    for (size_t i = 0; i < degree_digits; i++) {
        degrees = degrees * 10.0 + (double)(field[i] - '0');
    }
    double minutes = strtod(field + degree_digits, NULL);

    if (minutes >= 60.0 || degrees > max_degrees ||
        (degrees == max_degrees && minutes > 0.0)) {
        return COORD_ERR_RANGE;
    }

    int negative = 0;
    if (axis == COORD_AXIS_LATITUDE) {
        if (hemisphere == 'S') {
            negative = 1;
        } else if (hemisphere != 'N') {
            return COORD_ERR_HEMISPHERE;
        }
    } else {
        if (hemisphere == 'W') {
            negative = 1;
        } else if (hemisphere != 'E') {
            return COORD_ERR_HEMISPHERE;
        }
    }

    double value = degrees + minutes / 60.0;
    *out = negative ? -value : value;
    return COORD_OK;
}

int coord_is_valid_latitude(double degrees)
{
    return degrees >= -90.0 && degrees <= 90.0;
}

int coord_is_valid_longitude(double degrees)
{
    return degrees >= -180.0 && degrees <= 180.0;
}

const char *coord_status_str(coord_status_t status)
{
    switch (status) {
    case COORD_OK: return "ok";
    case COORD_ERR_NULL: return "null argument";
    case COORD_ERR_EMPTY: return "empty coordinate";
    case COORD_ERR_FORMAT: return "bad coordinate format";
    case COORD_ERR_RANGE: return "coordinate out of range";
    case COORD_ERR_HEMISPHERE: return "bad hemisphere indicator";
    }
    return "unknown";
}
