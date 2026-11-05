/* Parser tests plus receiver stream-handling tests (framing, fragmentation). */
#include <stdio.h>
#include <string.h>

#include "gps_receiver.h"
#include "nmea_checksum.h"
#include "nmea_parser.h"
#include "test_harness.h"

#define RMC_STD "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"
#define GGA_STD "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"

/* Build "$payload*HH" so tests do not depend on hand-computed checksums. */
static size_t make(char *out, size_t cap, const char *payload)
{
    char hex[3];
    nmea_checksum_to_hex(nmea_checksum_compute(payload, strlen(payload)), hex);
    int n = snprintf(out, cap, "$%s*%s", payload, hex);
    return (size_t)n;
}

static nmea_status_t parse_payload(const char *payload, nmea_message_t *m)
{
    char s[256];
    size_t n = make(s, sizeof s, payload);
    return nmea_parse_sentence(s, n, m);
}

static nmea_status_t parse_str(const char *s, nmea_message_t *m)
{
    return nmea_parse_sentence(s, strlen(s), m);
}

/* --------------------------------------------------------- parser tests */

static void test_valid_rmc(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_str(RMC_STD, &m), NMEA_OK);
    CHECK_EQ(m.type, NMEA_TYPE_RMC);
    CHECK(strcmp(m.talker, "GP") == 0);
    CHECK(m.u.rmc.valid);
    CHECK_NEAR(m.u.rmc.latitude_deg, 48.1173, 1e-9);
    CHECK_NEAR(m.u.rmc.longitude_deg, 11.516666667, 1e-8);
    CHECK_NEAR(m.u.rmc.speed_knots, 22.4, 1e-9);
    CHECK_NEAR(m.u.rmc.course_deg, 84.4, 1e-9);
    CHECK_EQ(m.u.rmc.time.hour, 12);
    CHECK_EQ(m.u.rmc.time.minute, 35);
    CHECK_EQ(m.u.rmc.time.second, 19);
    CHECK_EQ(m.u.rmc.date.day, 23);
    CHECK_EQ(m.u.rmc.date.month, 3);
    CHECK_EQ(m.u.rmc.date.year, 1994);
}

static void test_valid_gga(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_str(GGA_STD, &m), NMEA_OK);
    CHECK_EQ(m.type, NMEA_TYPE_GGA);
    CHECK_EQ(m.u.gga.quality, GPS_FIX_GPS);
    CHECK_EQ(m.u.gga.satellites_used, 8);
    CHECK_NEAR(m.u.gga.hdop, 0.9, 1e-9);
    CHECK_NEAR(m.u.gga.altitude_m, 545.4, 1e-9);
    CHECK_NEAR(m.u.gga.latitude_deg, 48.1173, 1e-9);
}

static void test_gnss_talker_and_fractional_time(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GNRMC,010203.250,A,2836.8340,N,07712.5400,E,0.0,,010124,,,A", &m),
             NMEA_OK);
    CHECK(strcmp(m.talker, "GN") == 0);
    CHECK_EQ(m.u.rmc.time.millisecond, 250);
    CHECK(!m.u.rmc.has_course); /* empty course field is allowed */
    CHECK_EQ(m.u.rmc.date.year, 2024);
    CHECK_EQ(parse_payload("GNGGA,010203,2836.8340,N,07712.5400,E,1,10,0.8,215.0,M,,M,,", &m),
             NMEA_OK);
    CHECK(strcmp(m.talker, "GN") == 0);
}

static void test_south_west(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GPRMC,010203,A,3352.0000,S,15112.0000,W,5.0,90.0,010124,,", &m),
             NMEA_OK);
    CHECK_NEAR(m.u.rmc.latitude_deg, -33.866666667, 1e-8);
    CHECK_NEAR(m.u.rmc.longitude_deg, -151.2, 1e-8);
}

static void test_void_rmc_with_empty_fields(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GPRMC,123519,V,,,,,,,230394,,", &m), NMEA_OK);
    CHECK(!m.u.rmc.valid);
    CHECK(!m.u.rmc.has_position);
    CHECK_EQ(parse_payload("GPGGA,123519,,,,,0,00,,,M,,M,,", &m), NMEA_OK);
    CHECK_EQ(m.u.gga.quality, GPS_FIX_INVALID);
}

static void test_empty_required_fields_rejected(void)
{
    nmea_message_t m;
    /* status A but no position */
    CHECK_EQ(parse_payload("GPRMC,123519,A,,,,,022.4,084.4,230394,,", &m),
             NMEA_ERR_MISSING_FIELD);
    /* status A but no time */
    CHECK_EQ(parse_payload("GPRMC,,A,4807.038,N,01131.000,E,022.4,084.4,230394,,", &m),
             NMEA_ERR_MISSING_FIELD);
    /* quality 1 but no position */
    CHECK_EQ(parse_payload("GPGGA,123519,,,,,1,08,0.9,545.4,M,,M,,", &m),
             NMEA_ERR_MISSING_FIELD);
    /* partial position */
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,,,022.4,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD);
}

static void test_invalid_fields_rejected(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GPRMC,123519,X,4807.038,N,01131.000,E,022.4,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* bad status */
    CHECK_EQ(parse_payload("GPRMC,253519,A,4807.038,N,01131.000,E,022.4,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* hour 25 */
    CHECK_EQ(parse_payload("GPRMC,123519,A,9961.500,N,01131.000,E,022.4,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* latitude 99 */
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,Q,01131.000,E,022.4,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* bad hemisphere */
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,01131.000,E,abc,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* non-numeric speed */
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,400.0,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* course > 360 */
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,310294,,", &m),
             NMEA_ERR_BAD_FIELD); /* 31 Feb */
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,01131.000,E,1e3,084.4,230394,,", &m),
             NMEA_ERR_BAD_FIELD); /* exponent notation not allowed */
    CHECK_EQ(parse_payload("GPGGA,123519,4807.038,N,01131.000,E,9,08,0.9,545.4,M,,M,,", &m),
             NMEA_ERR_BAD_FIELD); /* quality 9 */
    CHECK_EQ(parse_payload("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,F,,M,,", &m),
             NMEA_ERR_BAD_FIELD); /* altitude unit feet */
    /* failure must leave the message zeroed */
    CHECK_EQ(m.type, NMEA_TYPE_NONE);
}

static void test_leap_day(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,01131.000,E,1,1,290224,,", &m), NMEA_OK);
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N,01131.000,E,1,1,290223,,", &m),
             NMEA_ERR_BAD_FIELD);
}

static void test_too_few_fields(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GPRMC,123519,A,4807.038,N", &m), NMEA_ERR_TOO_FEW_FIELDS);
    CHECK_EQ(parse_payload("GPGGA,123519", &m), NMEA_ERR_TOO_FEW_FIELDS);
    CHECK_EQ(parse_payload("GPGSA,A,3", &m), NMEA_ERR_TOO_FEW_FIELDS);
}

static void test_too_many_fields(void)
{
    nmea_message_t m;
    /* 40 commas fit in 80 chars, exceeding MAX_FIELDS */
    CHECK_EQ(parse_payload("GPRMC,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,", &m),
             NMEA_ERR_TOO_MANY_FIELDS);
}

static void test_checksum_errors_through_parser(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_str("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00", &m),
             NMEA_ERR_CHECKSUM_MISMATCH);
    CHECK_EQ(parse_str("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", &m),
             NMEA_ERR_CHECKSUM_MISSING);
    CHECK_EQ(parse_str("$GPRMC,123519,A*6", &m), NMEA_ERR_CHECKSUM_MALFORMED);
    CHECK_EQ(parse_str("GPRMC,123519*00", &m), NMEA_ERR_FRAMING);
    CHECK_EQ(nmea_parse_sentence(NULL, 0, &m), NMEA_ERR_NULL);
}

static void test_unsupported_and_gsa_gsv(void)
{
    nmea_message_t m;
    CHECK_EQ(parse_payload("GPGLL,4807.038,N,01131.000,E,123519,A", &m), NMEA_ERR_UNSUPPORTED);
    CHECK_EQ(parse_payload("XXRMC,1,2,3,4,5,6,7,8,9,10", &m), NMEA_ERR_UNSUPPORTED);
    CHECK_EQ(parse_payload("GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1", &m), NMEA_OK);
    CHECK_EQ(m.u.gsa.fix_mode, 3);
    CHECK_NEAR(m.u.gsa.hdop, 1.3, 1e-9);
    CHECK_EQ(parse_payload("GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45",
                           &m), NMEA_OK);
    CHECK_EQ(m.u.gsv.satellites_in_view, 8);
    CHECK_EQ(parse_payload("GPGSV,2,3,08", &m), NMEA_ERR_BAD_FIELD); /* number > total */
}

static void test_too_long_rejected(void)
{
    char payload[200];
    memset(payload, '1', sizeof payload);
    memcpy(payload, "GPRMC,", 6);
    payload[120] = '\0';
    nmea_message_t m;
    CHECK_EQ(parse_payload(payload, &m), NMEA_ERR_TOO_LONG);
}

/* Sentence of exactly `total` characters (excluding CR/LF), valid GSV. */
static size_t make_sized(char *out, size_t cap, size_t total)
{
    char payload[128] = "GPGSV,1,1,01,";
    size_t have = strlen(payload);
    size_t want = total - 4; /* '$' + "*HH" */
    memset(payload + have, '0', want - have);
    payload[want] = '\0';
    return make(out, cap, payload);
}

static void test_length_boundary_parser(void)
{
    char s[128];
    nmea_message_t m;
    size_t n = make_sized(s, sizeof s, NMEA_MAX_BODY_LEN);
    CHECK_EQ(n, NMEA_MAX_BODY_LEN);
    CHECK_EQ(nmea_parse_sentence(s, n, &m), NMEA_OK); /* 80: accepted */
    n = make_sized(s, sizeof s, NMEA_MAX_BODY_LEN + 1);
    CHECK_EQ(nmea_parse_sentence(s, n, &m), NMEA_ERR_TOO_LONG); /* 81: rejected */
}

static void test_length_boundary_receiver(void)
{
    char s[128];
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    size_t n = make_sized(s, sizeof s, NMEA_MAX_BODY_LEN);
    s[n++] = '\r';
    s[n++] = '\n';
    CHECK_EQ(gps_receiver_feed(&rx, (const uint8_t *)s, n, 1), 1); /* 80 chars OK */
    CHECK_EQ(rx.stats.rejected_overflow, 0);

    n = make_sized(s, sizeof s, NMEA_MAX_BODY_LEN + 1);
    s[n++] = '\r';
    s[n++] = '\n';
    CHECK_EQ(gps_receiver_feed(&rx, (const uint8_t *)s, n, 2), 0); /* 81 chars */
    CHECK_EQ(rx.stats.rejected_overflow, 1);
    CHECK_EQ(rx.stats.accepted, 1);
}

/* -------------------------------------------------------- receiver tests */

static size_t feed_str(gps_receiver_t *rx, const char *s)
{
    return gps_receiver_feed(rx, (const uint8_t *)s, strlen(s), 1000);
}

static void test_receiver_single_sentence(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    CHECK_EQ(feed_str(&rx, RMC_STD "\r\n"), 1);
    CHECK(rx.fix.nav_valid);
    CHECK_NEAR(rx.fix.latitude_deg, 48.1173, 1e-9);
    CHECK_NEAR(rx.fix.speed_kmh, 22.4 * 1.852, 1e-9);
    CHECK_EQ(rx.stats.accepted, 1);
    CHECK_EQ(gps_receiver_invalid_count(&rx), 0);
}

static void test_receiver_multiple_in_one_read(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    CHECK_EQ(feed_str(&rx, RMC_STD "\r\n" GGA_STD "\r\n"), 2);
    CHECK_NEAR(rx.fix.altitude_m, 545.4, 1e-9);
    CHECK_EQ(rx.fix.satellites_used, 8);
    CHECK_EQ(rx.stats.accepted, 2);
}

static void test_receiver_fragmented_every_split(void)
{
    const char *stream = RMC_STD "\r\n" GGA_STD "\r\n";
    size_t total = strlen(stream);
    /* Split the stream into two reads at every possible position. */
    for (size_t cut = 0; cut <= total; cut++) {
        gps_receiver_t rx;
        gps_receiver_init(&rx, 5000);
        gps_receiver_feed(&rx, (const uint8_t *)stream, cut, 1);
        gps_receiver_feed(&rx, (const uint8_t *)stream + cut, total - cut, 2);
        CHECK_EQ(rx.stats.accepted, 2);
        CHECK_EQ(gps_receiver_invalid_count(&rx), 0);
    }
}

static void test_receiver_byte_at_a_time(void)
{
    const char *stream = RMC_STD "\r\n" GGA_STD "\r\n";
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    for (size_t i = 0; stream[i] != '\0'; i++) {
        gps_receiver_feed(&rx, (const uint8_t *)&stream[i], 1, 1);
    }
    CHECK_EQ(rx.stats.accepted, 2);
}

static void test_receiver_bad_checksum_does_not_update_state(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    feed_str(&rx, RMC_STD "\r\n");
    double lat = rx.fix.latitude_deg;
    /* Same sentence but latitude changed and checksum left as-is. */
    feed_str(&rx, "$GPRMC,123520,A,5000.000,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");
    CHECK_EQ(rx.stats.rejected_checksum, 1);
    CHECK_EQ(rx.stats.accepted, 1);
    CHECK_NEAR(rx.fix.latitude_deg, lat, 0.0);
    CHECK_EQ(rx.fix.time.second, 19);
}

static void test_receiver_missing_checksum_and_garbage(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    feed_str(&rx, "noise before\r\n");
    CHECK(rx.stats.bytes_discarded > 0);
    feed_str(&rx, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W\r\n");
    CHECK_EQ(rx.stats.rejected_checksum, 1);
    CHECK_EQ(rx.stats.accepted, 0);
    CHECK(!rx.fix.has_position);
}

static void test_receiver_incomplete_then_recovers(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    /* Truncated sentence (no CR/LF) followed directly by a good one. */
    CHECK_EQ(feed_str(&rx, "$GPRMC,123519,A,4807.0" RMC_STD "\r\n"), 1);
    CHECK_EQ(rx.stats.rejected_incomplete, 1);
    CHECK_EQ(rx.stats.accepted, 1);
}

static void test_receiver_finish_reports_partial(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    feed_str(&rx, "$GPRMC,123519,A,4807");
    CHECK_EQ(rx.stats.rejected_incomplete, 0);
    gps_receiver_finish(&rx);
    CHECK_EQ(rx.stats.rejected_incomplete, 1);
    gps_receiver_finish(&rx); /* idempotent */
    CHECK_EQ(rx.stats.rejected_incomplete, 1);
}

static void test_receiver_oversized_sentence(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    char big[400];
    memset(big, 'A', sizeof big);
    big[0] = '$';
    big[sizeof big - 3] = '\r';
    big[sizeof big - 2] = '\n';
    big[sizeof big - 1] = '\0';
    CHECK_EQ(feed_str(&rx, big), 0);
    CHECK_EQ(rx.stats.rejected_overflow, 1); /* reported once, not per byte */
    CHECK_EQ(feed_str(&rx, RMC_STD "\r\n"), 1); /* resynchronised afterwards */
    CHECK_EQ(rx.stats.accepted, 1);
}

static void test_receiver_oversized_without_terminator_then_new_sentence(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    char big[300];
    memset(big, 'B', sizeof big);
    big[0] = '$';
    big[sizeof big - 1] = '\0';
    feed_str(&rx, big);
    /* Invariants: buffer never exceeds its bound, receiver is discarding. */
    CHECK_EQ(rx.state, GPS_RX_OVERFLOW);
    CHECK(rx.length <= NMEA_MAX_BODY_LEN);
    CHECK_EQ(rx.stats.rejected_overflow, 1);
    CHECK_EQ(feed_str(&rx, RMC_STD "\r\n"), 1); /* '$' resynchronises */
}

static void test_receiver_nonprintable_rejected(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    const uint8_t bad[] = "$GPRMC,\x01\x02*00\r\n";
    CHECK_EQ(gps_receiver_feed(&rx, bad, sizeof bad - 1, 1), 0);
    CHECK_EQ(rx.stats.rejected_format, 1);
}

static void test_receiver_lf_only_and_crlf_both_work(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    CHECK_EQ(feed_str(&rx, RMC_STD "\n" GGA_STD "\r\n"), 2);
}

static void test_receiver_no_fix_keeps_last_position(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 5000);
    feed_str(&rx, RMC_STD "\r\n");
    char s[128];
    make(s, sizeof s, "GPRMC,123520,V,,,,,,,230394,,");
    feed_str(&rx, s);
    feed_str(&rx, "\r\n");
    CHECK(!rx.fix.nav_valid);
    CHECK(rx.fix.has_position);
    CHECK_NEAR(rx.fix.latitude_deg, 48.1173, 1e-9);
}

static void test_receiver_timeout(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 2000);
    CHECK_EQ(gps_receiver_update_freshness(&rx, 100), GPS_DATA_NONE);
    gps_receiver_feed(&rx, (const uint8_t *)RMC_STD "\r\n", strlen(RMC_STD) + 2, 1000);
    CHECK_EQ(gps_receiver_update_freshness(&rx, 2999), GPS_DATA_FRESH);
    CHECK_EQ(gps_receiver_update_freshness(&rx, 3001), GPS_DATA_TIMEOUT);
    /* New valid data recovers the state. */
    gps_receiver_feed(&rx, (const uint8_t *)GGA_STD "\r\n", strlen(GGA_STD) + 2, 4000);
    CHECK_EQ(gps_receiver_update_freshness(&rx, 4100), GPS_DATA_FRESH);
}

static void test_receiver_rejected_data_does_not_refresh(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 2000);
    gps_receiver_feed(&rx, (const uint8_t *)RMC_STD "\r\n", strlen(RMC_STD) + 2, 1000);
    const char *bad = "$GPRMC,1,A*00\r\n";
    gps_receiver_feed(&rx, (const uint8_t *)bad, strlen(bad), 2900);
    CHECK_EQ(gps_receiver_update_freshness(&rx, 3500), GPS_DATA_TIMEOUT);
}

static void test_receiver_null_safety(void)
{
    gps_receiver_t rx;
    gps_receiver_init(&rx, 0);
    CHECK_EQ(rx.timeout_ms, GPS_DEFAULT_TIMEOUT_MS);
    CHECK_EQ(gps_receiver_feed(&rx, NULL, 10, 0), 0);
    CHECK_EQ(gps_receiver_feed(NULL, (const uint8_t *)"x", 1, 0), 0);
}

int main(void)
{
    RUN_TEST(test_valid_rmc);
    RUN_TEST(test_valid_gga);
    RUN_TEST(test_gnss_talker_and_fractional_time);
    RUN_TEST(test_south_west);
    RUN_TEST(test_void_rmc_with_empty_fields);
    RUN_TEST(test_empty_required_fields_rejected);
    RUN_TEST(test_invalid_fields_rejected);
    RUN_TEST(test_leap_day);
    RUN_TEST(test_too_few_fields);
    RUN_TEST(test_too_many_fields);
    RUN_TEST(test_checksum_errors_through_parser);
    RUN_TEST(test_unsupported_and_gsa_gsv);
    RUN_TEST(test_too_long_rejected);
    RUN_TEST(test_length_boundary_parser);
    RUN_TEST(test_length_boundary_receiver);
    RUN_TEST(test_receiver_single_sentence);
    RUN_TEST(test_receiver_multiple_in_one_read);
    RUN_TEST(test_receiver_fragmented_every_split);
    RUN_TEST(test_receiver_byte_at_a_time);
    RUN_TEST(test_receiver_bad_checksum_does_not_update_state);
    RUN_TEST(test_receiver_missing_checksum_and_garbage);
    RUN_TEST(test_receiver_incomplete_then_recovers);
    RUN_TEST(test_receiver_finish_reports_partial);
    RUN_TEST(test_receiver_oversized_sentence);
    RUN_TEST(test_receiver_oversized_without_terminator_then_new_sentence);
    RUN_TEST(test_receiver_nonprintable_rejected);
    RUN_TEST(test_receiver_lf_only_and_crlf_both_work);
    RUN_TEST(test_receiver_no_fix_keeps_last_position);
    RUN_TEST(test_receiver_timeout);
    RUN_TEST(test_receiver_rejected_data_does_not_refresh);
    RUN_TEST(test_receiver_null_safety);
    return test_finish("test_parser");
}
