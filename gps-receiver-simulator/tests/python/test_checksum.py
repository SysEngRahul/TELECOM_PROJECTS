import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "simulator"))
from nmea_generator import build_sentence, checksum  # noqa: E402


class ChecksumTests(unittest.TestCase):
    def test_known_rmc(self):
        payload = "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"
        self.assertEqual(checksum(payload), 0x6A)

    def test_known_gga(self):
        payload = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"
        self.assertEqual(checksum(payload), 0x47)

    def test_empty_payload(self):
        self.assertEqual(checksum(""), 0)

    def test_single_char_change_changes_checksum(self):
        a = "GPGGA,1,2,3"
        b = "GPGGA,1,2,4"
        self.assertNotEqual(checksum(a), checksum(b))

    def test_build_sentence_format(self):
        s = build_sentence("GPGGA,1")
        self.assertTrue(s.startswith("$GPGGA,1*"))
        self.assertTrue(s.endswith("\r\n"))
        self.assertEqual(s[-4:-2], f"{checksum('GPGGA,1'):02X}")


if __name__ == "__main__":
    unittest.main()
