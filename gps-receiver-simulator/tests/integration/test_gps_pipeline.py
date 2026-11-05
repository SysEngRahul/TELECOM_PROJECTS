"""End-to-end: Python simulator -> pipe -> real C receiver -> parsed output."""
import os
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from _support import RECEIVER, ROOT, SIMULATOR, run_pipeline, run_receiver  # noqa: E402

RMC = b"$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
GGA = b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"


class KnownSentenceTests(unittest.TestCase):
    def test_reference_sentences(self):
        r = run_receiver(RMC + GGA)
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stats["valid"], 2)
        self.assertEqual(r.stats["invalid"], 0)
        last = r.fixes[-1]
        self.assertAlmostEqual(last["latitude"], 48.1173, places=6)
        self.assertAlmostEqual(last["longitude"], 11.516667, places=6)
        self.assertAlmostEqual(last["altitude_m"], 545.4, places=1)
        self.assertAlmostEqual(last["speed_knots"], 22.4, places=1)
        self.assertAlmostEqual(last["course_deg"], 84.4, places=1)
        self.assertEqual((last["date"], last["utc"]), ("1994-03-23", "12:35:19"))
        self.assertEqual(last["satellites"], 8)

    def test_empty_input_exits_cleanly(self):
        r = run_receiver(b"")
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stats["valid"], 0)

    def test_summary_block_mode(self):
        proc = subprocess.run([RECEIVER], input=RMC + GGA, capture_output=True, timeout=10)
        out = proc.stdout.decode()
        self.assertIn("GPS FIX STATUS   : VALID", out)
        self.assertIn("LATITUDE         : 48.117300", out)
        self.assertIn("VALID MESSAGES   : 2", out)
        self.assertIn("INVALID MESSAGES : 0", out)

    def test_help_and_bad_args(self):
        self.assertEqual(subprocess.run([RECEIVER, "--help"], capture_output=True).returncode, 0)
        self.assertEqual(subprocess.run([RECEIVER, "--bogus"], capture_output=True).returncode, 2)
        self.assertEqual(subprocess.run([RECEIVER, "--timeout-ms", "x"],
                                        capture_output=True).returncode, 2)


class SimulatorPipelineTests(unittest.TestCase):
    def test_known_start_position_round_trips(self):
        r = run_pipeline(["--count", "5", "--interval", "0", "--lat", "28.6139",
                          "--lon", "77.2090", "--altitude", "215", "--speed", "0"])
        self.assertEqual((r.returncode, r.sim_returncode), (0, 0))
        self.assertEqual(r.stats["valid"], 10)  # 5 epochs x (RMC + GGA)
        self.assertEqual(r.stats["invalid"], 0)
        for fix in r.fixes:
            # NMEA carries 4 decimal minutes: ~0.2 m resolution
            self.assertAlmostEqual(fix["latitude"], 28.6139, places=5)
            self.assertAlmostEqual(fix["longitude"], 77.2090, places=5)
        self.assertAlmostEqual(r.fixes[-1]["altitude_m"], 215.0, places=1)

    def test_southern_western_hemisphere(self):
        r = run_pipeline(["--count", "2", "--interval", "0", "--lat", "-33.8688",
                          "--lon", "-70.6693", "--speed", "0"])
        self.assertAlmostEqual(r.fixes[0]["latitude"], -33.8688, places=5)
        self.assertAlmostEqual(r.fixes[0]["longitude"], -70.6693, places=5)

    def test_movement_is_tracked(self):
        r = run_pipeline(["--count", "10", "--interval", "0", "--speed", "30",
                          "--heading", "0", "--lat", "10", "--lon", "20"])
        lats = [f["latitude"] for f in r.fixes if f["source"] == "RMC"]
        self.assertEqual(lats, sorted(lats))
        self.assertGreater(lats[-1], lats[0])
        self.assertAlmostEqual(r.fixes[-1]["speed_knots"], 30.0, places=1)

    def test_gnss_talker_and_all_sentence_types(self):
        r = run_pipeline(["--count", "3", "--interval", "0", "--talker", "GN",
                          "--sentences", "rmc,gga,gsa,gsv"])
        self.assertEqual(r.stats["invalid"], 0)
        self.assertEqual(r.stats["valid"], 3 * 5)  # RMC GGA GSA + 2 GSV (8 sats)

    def test_fragmented_stream(self):
        r = run_pipeline(["--count", "4", "--interval", "0", "--chunk-size", "3"])
        self.assertEqual(r.stats["valid"], 8)
        self.assertEqual(r.stats["invalid"], 0)

    def test_single_byte_writes(self):
        r = run_pipeline(["--count", "2", "--interval", "0", "--chunk-size", "1"])
        self.assertEqual(r.stats["valid"], 4)

    def test_fix_loss_reported_and_recovers(self):
        r = run_pipeline(["--count", "6", "--interval", "0", "--fix-loss", "2,2"])
        self.assertEqual(r.stats["invalid"], 0)  # "no fix" is valid data, not corruption
        flags = [f["valid"] for f in r.fixes if f["source"] == "RMC"]
        self.assertEqual(flags, [True, True, False, False, True, True])
        # last known position is retained while the fix is lost
        rmc = [f for f in r.fixes if f["source"] == "RMC"]
        self.assertAlmostEqual(rmc[3]["latitude"], rmc[1]["latitude"], places=6)

    def test_stdout_contains_only_nmea(self):
        proc = subprocess.run([sys.executable, SIMULATOR, "--count", "3", "--interval", "0"],
                              capture_output=True, timeout=30)
        for line in proc.stdout.decode().split("\r\n")[:-1]:
            self.assertTrue(line.startswith("$") and "*" in line, line)
        self.assertIn("[sim]", proc.stderr.decode())  # diagnostics went to stderr

    def test_simulator_help(self):
        out = subprocess.run([sys.executable, SIMULATOR, "--help"], capture_output=True)
        self.assertEqual(out.returncode, 0)
        self.assertIn(b"--count", out.stdout)

    def test_simulator_rejects_bad_args(self):
        out = subprocess.run([sys.executable, SIMULATOR, "--lat", "123"], capture_output=True)
        self.assertEqual(out.returncode, 2)
        self.assertEqual(out.stdout, b"")


class TimeoutTests(unittest.TestCase):
    def test_timeout_event_when_stream_goes_quiet(self):
        proc = subprocess.Popen([RECEIVER, "--stream", "--timeout-ms", "300"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        proc.stdin.write(RMC)
        proc.stdin.flush()
        time.sleep(1.0)  # silence longer than the timeout, pipe stays open
        proc.stdin.write(GGA)
        proc.stdin.close()
        out = proc.stdout.read().decode().splitlines()
        proc.stdout.close()
        proc.wait(timeout=10)
        self.assertEqual(proc.returncode, 0)
        self.assertIn("EVENT timeout", out)
        self.assertEqual(len([l for l in out if l.startswith("FIX")]), 2)

    def test_no_timeout_while_data_flows(self):
        r = run_pipeline(["--count", "3", "--interval", "0"])
        self.assertNotIn("EVENT timeout", r.raw)
        self.assertEqual(r.stats["timeout"], 0)


class PtyTests(unittest.TestCase):
    def test_simulator_over_pty_into_receiver(self):
        sim = subprocess.Popen([sys.executable, SIMULATOR, "--transport", "pty", "--count", "3",
                                "--interval", "0", "--start-delay", "1.5"],
                               stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        line = sim.stderr.readline().decode()
        self.assertIn("/dev/pts/", line)
        path = line.split("PTY slave:")[1].split()[0]
        with open(path, "rb", buffering=0) as slave:
            rec = subprocess.run([RECEIVER, "--stream"], stdin=slave, capture_output=True,
                                 timeout=20)
        sim.wait(timeout=10)
        self.assertEqual(sim.stdout.read(), b"")  # nothing leaks to stdout in PTY mode
        sim.stdout.close()
        sim.stderr.close()
        self.assertIn(b"STATS valid=6 invalid=0", rec.stdout)


if __name__ == "__main__":
    unittest.main()
