#include "coordinate_converter.h"
#include "test_harness.h"

static void test_latitude_north(void)
{
    double v = 0;
    CHECK_EQ(coord_nmea_to_decimal("4807.038", 'N', COORD_AXIS_LATITUDE, &v), COORD_OK);
    CHECK_NEAR(v, 48.1173, 1e-9);
}

static void test_longitude_east(void)
{
    double v = 0;
    CHECK_EQ(coord_nmea_to_decimal("01131.000", 'E', COORD_AXIS_LONGITUDE, &v), COORD_OK);
    CHECK_NEAR(v, 11.516666667, 1e-8);
}

static void test_south_west_negative(void)
{
    double v = 0;
    CHECK_EQ(coord_nmea_to_decimal("3352.0000", 'S', COORD_AXIS_LATITUDE, &v), COORD_OK);
    CHECK_NEAR(v, -33.866666667, 1e-8);
    CHECK_EQ(coord_nmea_to_decimal("15112.0000", 'W', COORD_AXIS_LONGITUDE, &v), COORD_OK);
    CHECK_NEAR(v, -151.2, 1e-9);
}

static void test_boundaries(void)
{
    double v = 0;
    CHECK_EQ(coord_nmea_to_decimal("9000.0000", 'N', COORD_AXIS_LATITUDE, &v), COORD_OK);
    CHECK_NEAR(v, 90.0, 1e-12);
    CHECK_EQ(coord_nmea_to_decimal("9000.0000", 'S', COORD_AXIS_LATITUDE, &v), COORD_OK);
    CHECK_NEAR(v, -90.0, 1e-12);
    CHECK_EQ(coord_nmea_to_decimal("18000.0000", 'E', COORD_AXIS_LONGITUDE, &v), COORD_OK);
    CHECK_NEAR(v, 180.0, 1e-12);
    CHECK_EQ(coord_nmea_to_decimal("00000.0000", 'E', COORD_AXIS_LONGITUDE, &v), COORD_OK);
    CHECK_NEAR(v, 0.0, 1e-12);
}

static void test_out_of_range(void)
{
    double v = 123.0;
    CHECK_EQ(coord_nmea_to_decimal("9000.0001", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_RANGE);
    CHECK_EQ(coord_nmea_to_decimal("9100.0000", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_RANGE);
    CHECK_EQ(coord_nmea_to_decimal("4860.0000", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_RANGE);
    CHECK_EQ(coord_nmea_to_decimal("18100.0000", 'E', COORD_AXIS_LONGITUDE, &v), COORD_ERR_RANGE);
    CHECK_EQ(coord_nmea_to_decimal("18000.0001", 'W', COORD_AXIS_LONGITUDE, &v), COORD_ERR_RANGE);
    CHECK_NEAR(v, 123.0, 0.0); /* untouched on error */
}

static void test_format_errors(void)
{
    double v = 0;
    CHECK_EQ(coord_nmea_to_decimal("", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_EMPTY);
    CHECK_EQ(coord_nmea_to_decimal("807.038", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal("48070.38", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal("48O7.038", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal("4807.", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal("4807.03.8", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal("-4807.038", 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal("4807.038", 'N', COORD_AXIS_LONGITUDE, &v), COORD_ERR_FORMAT);
    CHECK_EQ(coord_nmea_to_decimal(NULL, 'N', COORD_AXIS_LATITUDE, &v), COORD_ERR_NULL);
}

static void test_hemisphere_errors(void)
{
    double v = 0;
    CHECK_EQ(coord_nmea_to_decimal("4807.038", 'E', COORD_AXIS_LATITUDE, &v), COORD_ERR_HEMISPHERE);
    CHECK_EQ(coord_nmea_to_decimal("01131.000", 'N', COORD_AXIS_LONGITUDE, &v), COORD_ERR_HEMISPHERE);
    CHECK_EQ(coord_nmea_to_decimal("4807.038", 'n', COORD_AXIS_LATITUDE, &v), COORD_ERR_HEMISPHERE);
    CHECK_EQ(coord_nmea_to_decimal("4807.038", '\0', COORD_AXIS_LATITUDE, &v), COORD_ERR_HEMISPHERE);
}

static void test_validators(void)
{
    CHECK(coord_is_valid_latitude(90.0));
    CHECK(!coord_is_valid_latitude(90.1));
    CHECK(coord_is_valid_longitude(-180.0));
    CHECK(!coord_is_valid_longitude(-180.1));
}

int main(void)
{
    RUN_TEST(test_latitude_north);
    RUN_TEST(test_longitude_east);
    RUN_TEST(test_south_west_negative);
    RUN_TEST(test_boundaries);
    RUN_TEST(test_out_of_range);
    RUN_TEST(test_format_errors);
    RUN_TEST(test_hemisphere_errors);
    RUN_TEST(test_validators);
    return test_finish("test_coordinates");
}
