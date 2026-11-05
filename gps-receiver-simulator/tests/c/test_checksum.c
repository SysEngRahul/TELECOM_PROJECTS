#include <string.h>

#include "nmea_checksum.h"
#include "test_harness.h"

#define RMC "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"

static nmea_checksum_status_t validate(const char *s)
{
    return nmea_checksum_validate(s, strlen(s));
}

static void test_compute_known(void)
{
    const char *payload = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
    CHECK_EQ(nmea_checksum_compute(payload, strlen(payload)), 0x47);
    CHECK_EQ(nmea_checksum_compute("", 0), 0);
    CHECK_EQ(nmea_checksum_compute(NULL, 5), 0);
}

static void test_valid(void)
{
    CHECK_EQ(validate(RMC), NMEA_CS_OK);
    CHECK_EQ(validate("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"),
             NMEA_CS_OK);
}

static void test_lowercase_hex_accepted(void)
{
    const char *s = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    CHECK_EQ(validate(s), NMEA_CS_OK);
    /* 0x6A -> "6a" */
    char r[] = RMC;
    r[strlen(r) - 1] = 'a';
    CHECK_EQ(validate(r), NMEA_CS_OK);
}

static void test_mismatch(void)
{
    CHECK_EQ(validate("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B"),
             NMEA_CS_ERR_MISMATCH);
    CHECK_EQ(validate("$GPRMC,123519,A,4807.039,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"),
             NMEA_CS_ERR_MISMATCH);
}

static void test_missing(void)
{
    CHECK_EQ(validate("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"),
             NMEA_CS_ERR_MISSING);
}

static void test_malformed(void)
{
    CHECK_EQ(validate("$GPRMC,1,A*"), NMEA_CS_ERR_MALFORMED);
    CHECK_EQ(validate("$GPRMC,1,A*6"), NMEA_CS_ERR_MALFORMED);
    CHECK_EQ(validate("$GPRMC,1,A*6AA"), NMEA_CS_ERR_MALFORMED);
    CHECK_EQ(validate("$GPRMC,1,A*ZZ"), NMEA_CS_ERR_MALFORMED);
}

static void test_framing(void)
{
    CHECK_EQ(nmea_checksum_validate(NULL, 0), NMEA_CS_ERR_FRAMING);
    CHECK_EQ(validate(""), NMEA_CS_ERR_FRAMING);
    CHECK_EQ(validate("GPRMC,1,A*00"), NMEA_CS_ERR_FRAMING);
    CHECK_EQ(validate("$GPRMC,1$GPGGA,2*00"), NMEA_CS_ERR_FRAMING);
}

static void test_hex_helpers(void)
{
    uint8_t v = 0;
    char out[3];
    CHECK_EQ(nmea_checksum_parse_hex("6A", 2, &v), 1);
    CHECK_EQ(v, 0x6A);
    CHECK_EQ(nmea_checksum_parse_hex("6", 1, &v), 0);
    CHECK_EQ(nmea_checksum_parse_hex("G0", 2, &v), 0);
    nmea_checksum_to_hex(0x0A, out);
    CHECK(strcmp(out, "0A") == 0);
}

static void test_length_is_respected(void)
{
    /* Trailing bytes beyond len must not be read as part of the checksum. */
    const char *s = RMC "GARBAGE";
    CHECK_EQ(nmea_checksum_validate(s, strlen(RMC)), NMEA_CS_OK);
}

int main(void)
{
    RUN_TEST(test_compute_known);
    RUN_TEST(test_valid);
    RUN_TEST(test_lowercase_hex_accepted);
    RUN_TEST(test_mismatch);
    RUN_TEST(test_missing);
    RUN_TEST(test_malformed);
    RUN_TEST(test_framing);
    RUN_TEST(test_hex_helpers);
    RUN_TEST(test_length_is_respected);
    return test_finish("test_checksum");
}
