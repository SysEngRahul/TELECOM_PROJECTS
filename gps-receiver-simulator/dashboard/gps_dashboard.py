#!/usr/bin/env python3
"""Terminal dashboard fed by `gps_receiver --stream`.

    python3 simulator/gps_simulator.py --count 20 --interval 1 \
      | ./build/gps_receiver --stream \
      | python3 dashboard/gps_dashboard.py --csv results/run.csv

On a terminal the screen is redrawn in place; when piped or with --plain every
update is printed as a block. Reads only the receiver's text protocol; it never
parses NMEA itself.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from csv_logger import CsvLogger, FixParseError, parse_fix_line, parse_stats_line  # noqa: E402


def render(fix, stats, timeout):
    status = "VALID" if fix and fix["valid"] else "NO FIX"
    lines = ["========== GPS DASHBOARD ==========", ""]
    if fix is None:
        lines.append("waiting for data...")
    else:
        lines += [
            f"GPS FIX STATUS : {status}",
            f"TIMEOUT        : {'YES' if timeout else 'no'}",
            f"LATITUDE       : {fix['latitude']:.6f}",
            f"LONGITUDE      : {fix['longitude']:.6f}",
            f"ALTITUDE       : {fix['altitude_m']:.1f} m",
            f"SPEED          : {fix['speed_knots']:.1f} kn ({fix['speed_kmh']:.1f} km/h)",
            f"HEADING        : {fix['course_deg']:05.1f} deg",
            f"UTC            : {fix['date']} {fix['utc']}",
            f"SATELLITES     : {fix['satellites']}   HDOP {fix['hdop']:.1f}",
        ]
    if stats:
        lines += ["", f"VALID MESSAGES   : {stats['valid']}",
                  f"INVALID MESSAGES : {stats['invalid']}"]
    lines.append("===================================")
    return "\n".join(lines)


def run(lines, out, logger=None, live=False):
    fix, stats, timeout, shown = None, None, False, 0
    for line in lines:
        line = line.strip()
        if line.startswith("FIX "):
            try:
                fix = parse_fix_line(line)
            except FixParseError as exc:
                print(f"dashboard: {exc}", file=sys.stderr)
                continue
            timeout = False
            if logger:
                logger.log(fix)
        elif line.startswith("STATS "):
            stats = parse_stats_line(line)
            timeout = bool(stats.get("timeout"))
        elif line == "EVENT timeout":
            timeout = True
        else:
            continue
        if live:
            out.write("\x1b[H\x1b[2J")
        out.write(render(fix, stats, timeout) + "\n")
        out.flush()
        shown += 1
    return shown


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", help="also log every accepted fix to this CSV file")
    ap.add_argument("--plain", action="store_true", help="never use ANSI redraw")
    args = ap.parse_args(argv)
    logger = CsvLogger(args.csv) if args.csv else None
    live = sys.stdout.isatty() and not args.plain
    try:
        run(sys.stdin, sys.stdout, logger, live)
    except KeyboardInterrupt:
        pass
    finally:
        if logger:
            logger.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
