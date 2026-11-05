"""CSV logging of receiver output.

Consumes the machine-readable 'FIX key=value ...' lines printed by
`gps_receiver --stream`. Kept separate from the C parser on purpose.
"""
import csv
import math
from datetime import datetime, timezone

FIELDS = ["timestamp", "source", "latitude", "longitude", "altitude_m", "speed_knots",
          "course_deg", "fix_quality", "satellites", "valid"]


class FixParseError(ValueError):
    pass


def parse_fix_line(line: str) -> dict:
    """Parse 'FIX a=b c=d ...' into a dict of typed values."""
    parts = line.strip().split()
    if not parts or parts[0] != "FIX":
        raise FixParseError(f"not a FIX line: {line!r}")
    raw = {}
    for item in parts[1:]:
        key, sep, value = item.partition("=")
        if not sep:
            raise FixParseError(f"malformed token {item!r}")
        raw[key] = value
    try:
        return {
            "source": raw["source"],
            "valid": raw["valid"] == "1",
            "latitude": float(raw["lat"]),
            "longitude": float(raw["lon"]),
            "altitude_m": float(raw["alt"]),
            "speed_knots": float(raw["speed_kn"]),
            "speed_kmh": float(raw["speed_kmh"]),
            "course_deg": float(raw["course"]),
            "utc": raw["utc"],
            "date": raw["date"],
            "fix_quality": int(raw["quality"]),
            "satellites": int(raw["sats"]),
            "hdop": float(raw["hdop"]),
        }
    except (KeyError, ValueError) as exc:
        raise FixParseError(f"bad FIX line {line!r}: {exc}") from exc


def parse_stats_line(line: str) -> dict:
    """Parse 'STATS valid=N invalid=M ...' into ints."""
    parts = line.strip().split()
    if not parts or parts[0] != "STATS":
        raise FixParseError(f"not a STATS line: {line!r}")
    return {k: int(v) for k, _, v in (p.partition("=") for p in parts[1:])}


class CsvLogger:
    def __init__(self, path, clock=None):
        self._clock = clock or (lambda: datetime.now(timezone.utc))
        self._fh = open(path, "w", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(self._fh, fieldnames=FIELDS)
        self._writer.writeheader()
        self.rows = 0

    def log(self, fix: dict) -> None:
        def num(x, digits):
            return "" if math.isnan(x) else f"{x:.{digits}f}"
        self._writer.writerow({
            "timestamp": self._clock().strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z",
            "source": fix["source"],
            "latitude": num(fix["latitude"], 6),
            "longitude": num(fix["longitude"], 6),
            "altitude_m": num(fix["altitude_m"], 1),
            "speed_knots": num(fix["speed_knots"], 1),
            "course_deg": num(fix["course_deg"], 1),
            "fix_quality": fix["fix_quality"],
            "satellites": fix["satellites"],
            "valid": int(fix["valid"]),
        })
        self._fh.flush()
        self.rows += 1

    def close(self):
        self._fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
