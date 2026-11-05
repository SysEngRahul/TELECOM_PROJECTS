"""Shared helpers: run the real C receiver and parse its --stream output."""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "simulator"))
sys.path.insert(0, os.path.join(ROOT, "dashboard"))

RECEIVER = os.environ.get("GPS_RECEIVER_BIN", os.path.join(ROOT, "build", "gps_receiver"))
SIMULATOR = os.path.join(ROOT, "simulator", "gps_simulator.py")

from csv_logger import parse_fix_line, parse_stats_line  # noqa: E402


class ReceiverResult:
    def __init__(self, proc):
        self.returncode = proc.returncode
        self.stderr = proc.stderr.decode(errors="replace")
        out = proc.stdout.decode(errors="replace").splitlines()
        self.fixes = [parse_fix_line(l) for l in out if l.startswith("FIX ")]
        stats = [l for l in out if l.startswith("STATS ")]
        self.stats = parse_stats_line(stats[-1]) if stats else {}
        self.raw = "\n".join(out)


def run_receiver(data: bytes, extra_args=()):
    """Feed raw bytes to the C binary through a real pipe."""
    if not os.path.exists(RECEIVER):
        raise FileNotFoundError(f"{RECEIVER} not built - run `make` first")
    proc = subprocess.run([RECEIVER, "--stream", *extra_args], input=data,
                          capture_output=True, timeout=30)
    return ReceiverResult(proc)


def run_pipeline(sim_args):
    """simulator | receiver, connected by a real OS pipe (as on the CLI)."""
    sim = subprocess.Popen([sys.executable, SIMULATOR, "-q", *sim_args],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    rec = subprocess.Popen([RECEIVER, "--stream"], stdin=sim.stdout,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    sim.stdout.close()
    out, err = rec.communicate(timeout=60)
    sim_err = sim.stderr.read().decode()
    sim.stderr.close()
    sim.wait(timeout=10)
    proc = subprocess.CompletedProcess(rec.args, rec.returncode, out, err)
    result = ReceiverResult(proc)
    result.sim_returncode = sim.returncode
    result.sim_stderr = sim_err
    return result
