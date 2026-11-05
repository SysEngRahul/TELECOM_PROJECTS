#include "nmea_checksum.h"

uint8_t nmea_checksum_compute(const char *payload, size_t len)
{
    uint8_t checksum = 0;
    if (payload == NULL) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        checksum ^= (uint8_t)payload[i];
    }
    return checksum;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

int nmea_checksum_parse_hex(const char *text, size_t len, uint8_t *value)
{
    if (text == NULL || value == NULL || len != 2) {
        return 0;
    }
    int hi = hex_value(text[0]);
    int lo = hex_value(text[1]);
    if (hi < 0 || lo < 0) {
        return 0;
    }
    *value = (uint8_t)((hi << 4) | lo);
    return 1;
}

void nmea_checksum_to_hex(uint8_t value, char out[3])
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = digits[value >> 4];
    out[1] = digits[value & 0x0F];
    out[2] = '\0';
}

nmea_checksum_status_t nmea_checksum_validate(const char *sentence, size_t len)
{
    if (sentence == NULL || len < 1 || sentence[0] != '$') {
        return NMEA_CS_ERR_FRAMING;
    }

    /* Locate '*'. A second '$' means two sentences were glued together. */
    size_t star = 0;
    for (size_t i = 1; i < len; i++) {
        if (sentence[i] == '$') {
            return NMEA_CS_ERR_FRAMING;
        }
        if (sentence[i] == '*') {
            star = i;
            break;
        }
    }
    if (star == 0) {
        return NMEA_CS_ERR_MISSING;
    }

    uint8_t transmitted = 0;
    if (!nmea_checksum_parse_hex(&sentence[star + 1], len - star - 1, &transmitted)) {
        return NMEA_CS_ERR_MALFORMED;
    }

    uint8_t computed = nmea_checksum_compute(&sentence[1], star - 1);
    return (computed == transmitted) ? NMEA_CS_OK : NMEA_CS_ERR_MISMATCH;
}

const char *nmea_checksum_status_str(nmea_checksum_status_t status)
{
    switch (status) {
    case NMEA_CS_OK: return "ok";
    case NMEA_CS_ERR_FRAMING: return "framing error";
    case NMEA_CS_ERR_MISSING: return "checksum missing";
    case NMEA_CS_ERR_MALFORMED: return "checksum malformed";
    case NMEA_CS_ERR_MISMATCH: return "checksum mismatch";
    }
    return "unknown";
}
