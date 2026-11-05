import io
import json
import os
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "simulator"))
sys.path.insert(0, os.path.join(ROOT, "dashboard"))
import gps_simulator as sim  # noqa: E402
from serial_transport import PtyTransport, StdoutTransport  # noqa: E402
from csv_logger import CsvLogger, FixParseError, parse_fix_line, parse_stats_line  # noqa: E402
import gps_dashboard  # noqa: E402


def cfg_with(**overrides):
    cfg = sim._merge(sim.DEFAULTS, {})
    for path, value in overrides.items():
        section, key = path.split("__")
        cfg[section][key] = value
    return cfg


class ConfigTests(unittest.TestCase):
    def test_defaults_valid(self):
        sim.validate_config(sim._merge(sim.DEFAULTS, {}))

    def test_shipped_config_file_valid(self):
        cfg = sim.load_config(os.path.join(ROOT, "config", "simulator_config.json"))
        sim.validate_config(cfg)

    def test_rejections(self):
        for kw in ({"start__latitude": 95}, {"start__longitude": -181},
                   {"motion__speed_knots": -3}, {"motion__heading_deg": 400},
                   {"output__count": -1}, {"output__interval_s": -1},
                   {"output__talker": "ZZ"}, {"output__sentences": []},
                   {"output__sentences": ["foo"]}):
            with self.assertRaises(sim.ConfigError, msg=str(kw)):
                sim.validate_config(cfg_with(**kw))

    def test_bad_file(self):
        with self.assertRaises(sim.ConfigError):
            sim.load_config("/nonexistent/file.json")
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            fh.write("{not json")
        try:
            with self.assertRaises(sim.ConfigError):
                sim.load_config(fh.name)
        finally:
            os.unlink(fh.name)

    def test_partial_file_merges_with_defaults(self):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            json.dump({"start": {"latitude": 10.0}}, fh)
        try:
            cfg = sim.load_config(fh.name)
        finally:
            os.unlink(fh.name)
        self.assertEqual(cfg["start"]["latitude"], 10.0)
        self.assertEqual(cfg["start"]["longitude"], sim.DEFAULTS["start"]["longitude"])

    def test_cli_overrides(self):
        args = sim.build_parser().parse_args(
            ["--lat", "1.5", "--count", "3", "--fault", "truncated=0.1", "--seed", "9",
             "--fix-loss", "2,3", "--sentences", "rmc"])
        cfg = sim.validate_config(sim.apply_cli(sim.load_config(), args))
        self.assertEqual(cfg["start"]["latitude"], 1.5)
        self.assertEqual(cfg["output"]["count"], 3)
        self.assertEqual(cfg["faults"]["probabilities"], {"truncated": 0.1})
        self.assertEqual(cfg["faults"]["seed"], 9)
        self.assertEqual(cfg["faults"]["fix_loss"], {"start_epoch": 2, "duration": 3})

    def test_bad_fault_cli(self):
        args = sim.build_parser().parse_args(["--fault", "oops"])
        with self.assertRaises(sim.ConfigError):
            sim.apply_cli(sim.load_config(), args)


class RunTests(unittest.TestCase):
    def test_run_emits_only_nmea(self):
        buf = io.BytesIO()
        cfg = cfg_with(output__count=4, output__interval_s=0)
        sent = sim.run(cfg, StdoutTransport(stream=buf), quiet=True)
        self.assertEqual(sent, 4)
        lines = buf.getvalue().decode().split("\r\n")[:-1]
        self.assertEqual(len(lines), 8)
        self.assertTrue(all(l.startswith("$GP") and "*" in l for l in lines))

    def test_fix_loss_emits_void_sentences(self):
        buf = io.BytesIO()
        cfg = cfg_with(output__count=3, output__interval_s=0)
        cfg["faults"] = {"fix_loss": {"start_epoch": 1, "duration": 1}}
        sim.run(cfg, StdoutTransport(stream=buf), quiet=True)
        lines = buf.getvalue().decode().split("\r\n")
        self.assertIn(",V,", lines[2])
        self.assertIn(",A,", lines[4])


class TransportTests(unittest.TestCase):
    def test_chunking(self):
        class Rec:
            def __init__(self):
                self.parts = []
            def write(self, d):
                self.parts.append(d)
            def flush(self):
                pass
        rec = Rec()
        t = StdoutTransport(stream=rec, chunk_size=5)
        t.write("0123456789ABC")
        self.assertEqual([len(p) for p in rec.parts], [5, 5, 3])
        self.assertEqual(b"".join(rec.parts), b"0123456789ABC")

    def test_pty_round_trip(self):
        t = PtyTransport()
        try:
            t.write("$GPGGA,1*00\r\n")
            data = os.read(t.slave_fd, 100)
            self.assertEqual(data, b"$GPGGA,1*00\r\n")  # raw mode: no CR/LF mangling
            self.assertTrue(t.slave_path.startswith("/dev/"))
        finally:
            t.close()


class DashboardTests(unittest.TestCase):
    LINE = ("FIX source=RMC valid=1 lat=28.613900 lon=77.209000 alt=215.0 speed_kn=22.4 "
            "speed_kmh=41.5 course=84.4 utc=12:00:00 date=2024-01-01 quality=1 sats=8 hdop=0.9")

    def test_parse_fix_line(self):
        f = parse_fix_line(self.LINE)
        self.assertEqual((f["latitude"], f["satellites"], f["valid"]), (28.6139, 8, True))

    def test_parse_errors(self):
        for bad in ("STATS valid=1", "FIX lat=1", "FIX source", ""):
            with self.assertRaises(FixParseError):
                parse_fix_line(bad)

    def test_parse_stats(self):
        self.assertEqual(parse_stats_line("STATS valid=3 invalid=1")["invalid"], 1)

    def test_csv_logging(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "x.csv")
            with CsvLogger(path, clock=lambda: __import__("datetime").datetime(2024, 1, 1)) as lg:
                lg.log(parse_fix_line(self.LINE))
            rows = open(path).read().splitlines()
        self.assertEqual(rows[0].split(",")[:3], ["timestamp", "source", "latitude"])
        self.assertIn("28.613900,77.209000,215.0,22.4,84.4,1,8,1", rows[1])

    def test_dashboard_run_renders_and_ignores_noise(self):
        out = io.StringIO()
        n = gps_dashboard.run(["garbage", self.LINE, "STATS valid=2 invalid=0 timeout=0"], out)
        self.assertEqual(n, 2)
        self.assertIn("GPS FIX STATUS : VALID", out.getvalue())
        self.assertIn("VALID MESSAGES   : 2", out.getvalue())


if __name__ == "__main__":
    unittest.main()
