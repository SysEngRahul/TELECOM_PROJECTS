/*
 * nmea_checksum.h - NMEA 0183 XOR checksum generation and validation.
 *
 * The checksum is the XOR of every character between '$' and '*' (both
 * excluded), transmitted as two hexadecimal digits after '*'.
 */
#ifndef NMEA_CHECKSUM_H
#define NMEA_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NMEA_CS_OK = 0,
    NMEA_CS_ERR_FRAMING,   /* NULL, empty, no leading '$', or stray '$' inside */
    NMEA_CS_ERR_MISSING,   /* no '*' delimiter present */
    NMEA_CS_ERR_MALFORMED, /* '*' not followed by exactly two hex digits */
    NMEA_CS_ERR_MISMATCH   /* well-formed checksum that does not match */
} nmea_checksum_status_t;

/* XOR of payload[0..len-1]. payload excludes '$' and '*'. */
uint8_t nmea_checksum_compute(const char *payload, size_t len);

/* Parse exactly two hex digits (either case). Returns 1 on success, 0 else. */
int nmea_checksum_parse_hex(const char *text, size_t len, uint8_t *value);

/* Write two uppercase hex digits plus NUL into out[3]. */
void nmea_checksum_to_hex(uint8_t value, char out[3]);

/*
 * Validate a sentence of the form "$<payload>*HH" (no CR/LF) of length len.
 * Never reads beyond len bytes.
 */
nmea_checksum_status_t nmea_checksum_validate(const char *sentence, size_t len);

const char *nmea_checksum_status_str(nmea_checksum_status_t status);

#endif /* NMEA_CHECKSUM_H */
