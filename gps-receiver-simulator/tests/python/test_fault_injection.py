import os
import sys
import unittest
from datetime import datetime, timezone

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "simulator"))
import nmea_generator as g  # noqa: E402
from fault_injector import FaultConfigError, FaultInjector, validate_fault_config  # noqa: E402
from route_generator import NavSample  # noqa: E402

S = NavSample(28.6139, 77.2090, 215.0, 22.4, 84.4,
              datetime(2024, 1, 1, 12, 0, 0, tzinfo=timezone.utc))
RMC = g.rmc(S)
GGA = g.gga(S)


def is_valid(sentence):
    """Independent re-implementation of the checksum rule."""
    body = sentence.rstrip("\r\n")
    if not body.startswith("$") or "*" not in body:
        return False
    payload, _, cs = body[1:].rpartition("*")
    try:
        return len(cs) == 2 and g.checksum(payload) == int(cs, 16)
    except ValueError:
        return False


class FaultInjectorTests(unittest.TestCase):
    def setUp(self):
        self.inj = FaultInjector({"seed": 7})

    def test_clean_when_no_faults(self):
        out = self.inj.process(RMC)
        self.assertEqual([e.text for e in out], [RMC])

    def test_invalid_checksum(self):
        e = self.inj.apply(RMC, "invalid_checksum")[0]
        self.assertFalse(is_valid(e.text))
        self.assertTrue(e.text.endswith("\r\n"))
        self.assertEqual(e.text[:-5], RMC[:-5])  # only the checksum differs

    def test_missing_checksum(self):
        e = self.inj.apply(RMC, "missing_checksum")[0]
        self.assertNotIn("*", e.text)
        self.assertTrue(e.text.endswith("\r\n"))

    def test_truncated(self):
        e = self.inj.apply(RMC, "truncated")[0]
        self.assertFalse(e.text.endswith("\r\n"))
        self.assertLess(len(e.text), len(RMC) - 2)
        self.assertTrue(RMC.startswith(e.text))

    def test_invalid_coords_keeps_valid_checksum(self):
        for s, idx in ((RMC, 3), (GGA, 2)):
            e = self.inj.apply(s, "invalid_coords")[0]
            self.assertTrue(is_valid(e.text))
            self.assertEqual(e.text.split(",")[idx], "9961.5000")

    def test_missing_fields(self):
        e = self.inj.apply(RMC, "missing_fields")[0]
        self.assertTrue(is_valid(e.text))
        self.assertLess(len(e.text.split(",")), len(RMC.split(",")))

    def test_random_corruption_always_detectable(self):
        for _ in range(500):
            e = self.inj.apply(RMC, "random_corruption")[0]
            self.assertFalse(is_valid(e.text))
            self.assertNotEqual(e.text, RMC)

    def test_delay(self):
        inj = FaultInjector({"delay_s": 1.5})
        e = inj.apply(RMC, "delay")[0]
        self.assertEqual((e.text, e.delay_s), (RMC, 1.5))

    def test_repeated(self):
        inj = FaultInjector({"repeat_count": 3})
        out = inj.apply(RMC, "repeated")
        self.assertEqual([e.text for e in out], [RMC] * 3)

    def test_unknown_fault(self):
        with self.assertRaises(FaultConfigError):
            self.inj.apply(RMC, "nope")

    def test_reproducible_with_seed(self):
        cfg = {"seed": 123, "probabilities": {"random_corruption": 0.3, "truncated": 0.2,
                                              "invalid_checksum": 0.2}}
        def run():
            inj = FaultInjector(cfg)
            return [[e.text for e in inj.process(RMC)] for _ in range(200)]
        self.assertEqual(run(), run())

    def test_different_seeds_differ(self):
        def run(seed):
            inj = FaultInjector({"seed": seed, "probabilities": {"random_corruption": 0.5}})
            return [[e.text for e in inj.process(RMC)] for _ in range(50)]
        self.assertNotEqual(run(1), run(2))

    def test_probability_roughly_respected(self):
        inj = FaultInjector({"seed": 1, "probabilities": {"invalid_checksum": 0.25}})
        bad = sum(not is_valid(inj.process(RMC)[0].text) for _ in range(4000))
        self.assertTrue(800 < bad < 1200, bad)

    def test_fix_loss_window(self):
        inj = FaultInjector({"fix_loss": {"start_epoch": 3, "duration": 2}})
        self.assertEqual([inj.fix_lost(i) for i in range(7)],
                         [False, False, False, True, True, False, False])
        self.assertFalse(FaultInjector({}).fix_lost(0))


class ConfigValidationTests(unittest.TestCase):
    def test_unknown_fault_name(self):
        with self.assertRaises(FaultConfigError):
            validate_fault_config({"probabilities": {"bogus": 0.1}})

    def test_probability_range(self):
        with self.assertRaises(FaultConfigError):
            validate_fault_config({"probabilities": {"truncated": 1.5}})

    def test_probability_sum(self):
        with self.assertRaises(FaultConfigError):
            validate_fault_config({"probabilities": {"truncated": 0.7, "delay": 0.7}})

    def test_repeat_count(self):
        with self.assertRaises(FaultConfigError):
            validate_fault_config({"repeat_count": 1})


if __name__ == "__main__":
    unittest.main()
