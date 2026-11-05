import math
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "simulator"))
from route_generator import RouteError, RouteGenerator  # noqa: E402


def distance_m(lat1, lon1, lat2, lon2):
    """Haversine reference distance."""
    r = 6_371_000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


class RouteTests(unittest.TestCase):
    def test_first_sample_is_start(self):
        r = RouteGenerator(28.6, 77.2)
        self.assertEqual((r.current.latitude, r.current.longitude), (28.6, 77.2))

    def test_heading_north_increases_latitude_only(self):
        r = RouteGenerator(10.0, 20.0, speed_knots=60, heading_deg=0)
        s = r.step(60)
        self.assertGreater(s.latitude, 10.0)
        self.assertAlmostEqual(s.longitude, 20.0, places=9)

    def test_heading_east_increases_longitude(self):
        s = RouteGenerator(0.0, 0.0, 60, 90).step(60)
        self.assertGreater(s.longitude, 0.0)
        self.assertAlmostEqual(s.latitude, 0.0, places=9)

    def test_distance_matches_speed(self):
        r = RouteGenerator(28.6, 77.2, speed_knots=20, heading_deg=45)
        s = r.step(300)
        expected = 20 * 1852 / 3600 * 300
        got = distance_m(28.6, 77.2, s.latitude, s.longitude)
        self.assertAlmostEqual(got, expected, delta=expected * 0.005)

    def test_time_advances(self):
        r = RouteGenerator(0, 0)
        t0 = r.current.utc
        self.assertEqual((r.step(2.5).utc - t0).total_seconds(), 2.5)

    def test_deterministic(self):
        a = [RouteGenerator(1, 2, 10, 33).step(1) for _ in range(3)]
        self.assertEqual(a[0], a[1])

    def test_longitude_wraps_at_antimeridian(self):
        r = RouteGenerator(0.0, 179.9999, speed_knots=600, heading_deg=90)
        s = r.step(60)
        self.assertTrue(-180.0 <= s.longitude < 0.0)

    def test_invalid_parameters(self):
        for kwargs in ({"latitude": 91, "longitude": 0}, {"latitude": 0, "longitude": 181},
                       {"latitude": 0, "longitude": 0, "speed_knots": -1},
                       {"latitude": 0, "longitude": 0, "heading_deg": 361},
                       {"latitude": 90, "longitude": 0}):
            with self.assertRaises(RouteError, msg=str(kwargs)):
                RouteGenerator(**kwargs)

    def test_polar_limit(self):
        r = RouteGenerator(89.8, 0, speed_knots=600, heading_deg=0)
        with self.assertRaises(RouteError):
            r.step(3600)

    def test_negative_dt(self):
        with self.assertRaises(RouteError):
            RouteGenerator(0, 0).step(-1)


if __name__ == "__main__":
    unittest.main()
