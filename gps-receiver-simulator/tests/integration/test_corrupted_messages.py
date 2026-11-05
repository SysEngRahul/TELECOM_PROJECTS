"""Corrupted-input behaviour of the real C receiver."""
import os
import sys
import unittest
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(__file__))
from _support import run_pipeline, run_receiver  # noqa: E402
import nmea_generator as g  # noqa: E402
from fault_injector import FaultInjector  # noqa: E402
from route_generator import NavSample  # noqa: E402


def sample(lat, lon, second=0):
    return NavSample(lat, lon, 100.0, 10.0, 90.0,
                     datetime(2024, 1, 1, 12, 0, second, tzinfo=timezone.utc))


GOOD = sample(28.0, 77.0, 0)
LATER = sample(29.5, 78.5, 10)


class SingleFaultTests(unittest.TestCase):
    """Each fault is injected into an otherwise good stream between two epochs."""

    def stream_with(self, fault, kind="rmc"):
        inj = FaultInjector({"seed": 5})
        pick = g.rmc if kind == "rmc" else g.gga
        bad = "".join(e.text for e in inj.apply(pick(sample(60.0, 100.0, 5)), fault))
        return (g.rmc(GOOD) + g.gga(GOOD) + bad + g.rmc(LATER) + g.gga(LATER)).encode()

    def check_rejected_and_recovered(self, fault, kind="rmc", counter=None):
        r = run_receiver(self.stream_with(fault, kind))
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stats["valid"], 4, r.raw)
        self.assertEqual(r.stats["invalid"], 1, r.raw)
        if counter:
            self.assertEqual(r.stats[counter], 1)
        # the corrupted position (60N, 100E) must never reach the state
        for fix in r.fixes:
            self.assertNotAlmostEqual(fix["latitude"], 60.0, places=1)
        # and the stream recovers: final state is from the later good epoch
        self.assertAlmostEqual(r.fixes[-1]["latitude"], 29.5, places=5)
        self.assertAlmostEqual(r.fixes[-1]["longitude"], 78.5, places=5)
        return r

    def test_invalid_checksum(self):
        self.check_rejected_and_recovered("invalid_checksum", counter="checksum")

    def test_missing_checksum(self):
        self.check_rejected_and_recovered("missing_checksum", counter="checksum")

    def test_truncated_sentence(self):
        self.check_rejected_and_recovered("truncated", counter="incomplete")

    def test_invalid_coordinates_with_valid_checksum(self):
        self.check_rejected_and_recovered("invalid_coords", counter="format")

    def test_missing_fields_with_valid_checksum(self):
        self.check_rejected_and_recovered("missing_fields", counter="format")

    def test_random_corruption(self):
        for seed in range(25):
            inj = FaultInjector({"seed": seed})
            bad = inj.apply(g.rmc(sample(60.0, 100.0, 5)), "random_corruption")[0].text
            r = run_receiver((g.rmc(GOOD) + bad + g.rmc(LATER)).encode())
            self.assertEqual(r.stats["valid"], 2, (seed, bad))
            self.assertEqual(r.stats["invalid"], 1, (seed, bad))

    def test_invalid_gga_coordinates(self):
        self.check_rejected_and_recovered("invalid_coords", kind="gga")

    def test_repeated_messages_are_valid_duplicates(self):
        inj = FaultInjector({"repeat_count": 3})
        rep = "".join(e.text for e in inj.apply(g.rmc(GOOD), "repeated"))
        r = run_receiver(rep.encode())
        self.assertEqual((r.stats["valid"], r.stats["invalid"]), (3, 0))


class StateIntegrityTests(unittest.TestCase):
    def test_corrupt_stream_from_the_start_never_produces_a_fix(self):
        bad = "$GPRMC,120000.00,A,2800.0000,N,07700.0000,E,10.0,90.0,010124,,,A*00\r\n"
        r = run_receiver((bad * 5).encode())
        self.assertEqual(r.stats["valid"], 0)
        self.assertEqual(r.stats["invalid"], 5)
        self.assertEqual(r.fixes, [])

    def test_garbage_and_binary_noise(self):
        noise = bytes(range(256)) * 3
        r = run_receiver(noise + g.rmc(GOOD).encode())
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stats["valid"], 1)

    def test_oversized_line(self):
        r = run_receiver(b"$" + b"A" * 5000 + b"\r\n" + g.rmc(GOOD).encode())
        self.assertEqual(r.stats["overflow"], 1)
        self.assertEqual(r.stats["valid"], 1)

    def test_partial_sentence_at_eof_counted_incomplete(self):
        r = run_receiver(g.rmc(GOOD).encode() + b"$GPRMC,120000.00,A,28")
        self.assertEqual((r.stats["valid"], r.stats["incomplete"]), (1, 1))

    def test_reject_messages_go_to_stderr(self):
        r = run_receiver(b"$GPRMC,1,A*00\r\n")
        self.assertIn("REJECT", r.stderr)
        self.assertNotIn("REJECT", r.raw)


class RandomizedPipelineTests(unittest.TestCase):
    def test_probabilistic_faults_through_whole_pipeline(self):
        faults = ["--fault", "invalid_checksum=0.1", "--fault", "random_corruption=0.1",
                  "--fault", "missing_checksum=0.05", "--fault", "invalid_coords=0.05",
                  "--seed", "2024", "--count", "200", "--interval", "0"]
        a = run_pipeline(faults)
        b = run_pipeline(faults)
        self.assertEqual(a.stats, b.stats)  # reproducible with the same seed
        self.assertGreater(a.stats["invalid"], 40)
        self.assertGreater(a.stats["valid"], 200)
        self.assertEqual(a.stats["valid"] + a.stats["invalid"], 400)
        for fix in a.fixes:  # no corrupted value ever leaks into the state
            self.assertAlmostEqual(fix["latitude"], 28.6, delta=0.1)
            self.assertAlmostEqual(fix["longitude"], 77.2, delta=0.1)

    def test_truncation_through_pipeline(self):
        r = run_pipeline(["--fault", "truncated=0.2", "--seed", "3", "--count", "100",
                          "--interval", "0"])
        self.assertGreater(r.stats["incomplete"], 0)
        self.assertEqual(r.stats["valid"] + r.stats["invalid"], 200)

    def test_repeated_and_delay_faults(self):
        r = run_pipeline(["--fault", "repeated=0.5", "--seed", "1", "--count", "20",
                          "--interval", "0"])
        self.assertGreater(r.stats["valid"], 40)
        self.assertEqual(r.stats["invalid"], 0)


if __name__ == "__main__":
    unittest.main()
