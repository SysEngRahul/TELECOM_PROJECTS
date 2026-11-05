import os
import sys
import unittest
from datetime import datetime, timezone

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "simulator"))
import nmea_generator as g  # noqa: E402
from route_generator import NavSample  # noqa: E402

UTC = datetime(1994, 3, 23, 12, 35, 19, tzinfo=timezone.utc)


def sample(**kw):
    base = dict(latitude=48.1173, longitude=11.516667, altitude_m=545.4,
                speed_knots=22.4, course_deg=84.4, utc=UTC, satellites=8, hdop=0.9)
    base.update(kw)
    return NavSample(**base)


def verify(testcase, sentence):
    testcase.assertTrue(sentence.endswith("\r\n"))
    body = sentence[:-2]
    testcase.assertEqual(body[0], "$")
    payload, _, cs = body[1:].rpartition("*")
    testcase.assertEqual(len(cs), 2)
    testcase.assertEqual(g.checksum(payload), int(cs, 16))
    return payload.split(",")


class CoordinateFormatTests(unittest.TestCase):
    def test_latitude(self):
        self.assertEqual(g.format_latitude(48.1173), ("4807.0380", "N"))
        self.assertEqual(g.format_latitude(-33.8667), ("3352.0020", "S"))

    def test_longitude_three_degree_digits(self):
        self.assertEqual(g.format_longitude(11.516667), ("01131.0000", "E"))
        self.assertEqual(g.format_longitude(-151.2), ("15112.0000", "W"))

    def test_zero_and_bounds(self):
        self.assertEqual(g.format_latitude(0.0), ("0000.0000", "N"))
        self.assertEqual(g.format_latitude(90.0), ("9000.0000", "N"))
        self.assertEqual(g.format_longitude(180.0), ("18000.0000", "E"))

    def test_minutes_never_round_to_sixty(self):
        text, _ = g.format_latitude(10 + 59.99999 / 60)
        self.assertEqual(text, "1100.0000")

    def test_out_of_range_rejected(self):
        with self.assertRaises(ValueError):
            g.format_latitude(90.1)
        with self.assertRaises(ValueError):
            g.format_longitude(-180.5)


class SentenceTests(unittest.TestCase):
    def test_rmc_fields(self):
        f = verify(self, g.rmc(sample()))
        self.assertEqual(f[0], "GPRMC")
        self.assertEqual(f[2], "A")
        self.assertEqual((f[3], f[4]), ("4807.0380", "N"))
        self.assertEqual((f[5], f[6]), ("01131.0000", "E"))
        self.assertEqual(f[7], "22.4")
        self.assertEqual(f[9], "230394")

    def test_gga_fields(self):
        f = verify(self, g.gga(sample()))
        self.assertEqual(f[0], "GPGGA")
        self.assertEqual(f[6], "1")
        self.assertEqual(f[7], "08")
        self.assertEqual(f[9], "545.4")
        self.assertEqual(f[10], "M")

    def test_gnss_talker(self):
        self.assertTrue(g.rmc(sample(), "GN").startswith("$GNRMC"))
        self.assertTrue(g.gga(sample(), "GN").startswith("$GNGGA"))

    def test_invalid_talker(self):
        with self.assertRaises(ValueError):
            g.rmc(sample(), "XX")

    def test_no_fix_variants(self):
        f = verify(self, g.rmc(sample(), fix=False))
        self.assertEqual(f[2], "V")
        f = verify(self, g.gga(sample(), fix=False))
        self.assertEqual(f[6], "0")

    def test_southern_western(self):
        f = verify(self, g.rmc(sample(latitude=-33.8, longitude=-70.6)))
        self.assertEqual((f[4], f[6]), ("S", "W"))

    def test_gsa_has_18_fields(self):
        f = verify(self, g.gsa(sample()))
        self.assertEqual(len(f), 18)
        self.assertEqual(f[2], "3")

    def test_gsv_multi_message(self):
        msgs = g.gsv(sample(satellites=9))
        self.assertEqual(len(msgs), 3)
        for i, m in enumerate(msgs, 1):
            f = verify(self, m)
            self.assertEqual(f[1], "3")
            self.assertEqual(f[2], str(i))
            self.assertEqual(f[3], "09")

    def test_sentences_within_nmea_length_limit(self):
        for s in (g.rmc(sample()), g.gga(sample()), g.gsa(sample()), *g.gsv(sample())):
            self.assertLessEqual(len(s), 82)

    def test_epoch_sentences_order_and_unknown_kind(self):
        out = g.epoch_sentences(sample(), kinds=("rmc", "gga", "gsa", "gsv"))
        self.assertEqual([s[3:6] for s in out], ["RMC", "GGA", "GSA", "GSV", "GSV"])
        with self.assertRaises(ValueError):
            g.epoch_sentences(sample(), kinds=("xyz",))


if __name__ == "__main__":
    unittest.main()
