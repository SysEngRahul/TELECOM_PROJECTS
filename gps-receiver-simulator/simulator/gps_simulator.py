#!/usr/bin/env python3
"""GPS navigation simulator: route -> NMEA sentences -> transport.

In stdout mode ONLY NMEA sentences are written to stdout; every diagnostic goes
to stderr, so the output can be piped straight into ./build/gps_receiver.
"""
import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fault_injector import FaultConfigError, FaultInjector, SENTENCE_FAULTS, validate_fault_config  # noqa: E402
from nmea_generator import VALID_TALKERS, epoch_sentences  # noqa: E402
from route_generator import RouteError, RouteGenerator  # noqa: E402
from serial_transport import PtyTransport, StdoutTransport, TransportError  # noqa: E402

DEFAULTS = {
    "start": {"latitude": 28.6139, "longitude": 77.2090, "altitude_m": 215.0},
    "motion": {"speed_knots": 22.4, "heading_deg": 84.4},
    "output": {"count": 10, "interval_s": 1.0, "talker": "GP",
               "sentences": ["rmc", "gga"], "satellites": 8, "hdop": 0.9},
    "faults": {},
}


class ConfigError(ValueError):
    pass


def _merge(base, override):
    out = {k: (dict(v) if isinstance(v, dict) else v) for k, v in base.items()}
    for key, value in (override or {}).items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _merge(out[key], value)
        else:
            out[key] = value
    return out


def validate_config(cfg: dict) -> dict:
    """Validate the merged configuration; raise ConfigError on any problem."""
    try:
        s, m, o = cfg["start"], cfg["motion"], cfg["output"]
        if not -90 <= s["latitude"] <= 90:
            raise ConfigError("start.latitude must be within -90..90")
        if not -180 <= s["longitude"] <= 180:
            raise ConfigError("start.longitude must be within -180..180")
        if m["speed_knots"] < 0:
            raise ConfigError("motion.speed_knots must be >= 0")
        if not 0 <= m["heading_deg"] <= 360:
            raise ConfigError("motion.heading_deg must be within 0..360")
        if o["count"] < 0:
            raise ConfigError("output.count must be >= 0 (0 = continuous)")
        if o["interval_s"] < 0:
            raise ConfigError("output.interval_s must be >= 0")
        if o["talker"] not in VALID_TALKERS:
            raise ConfigError(f"output.talker must be one of {VALID_TALKERS}")
        bad = [k for k in o["sentences"] if k not in ("rmc", "gga", "gsa", "gsv")]
        if bad or not o["sentences"]:
            raise ConfigError("output.sentences must be a non-empty subset of rmc,gga,gsa,gsv")
        validate_fault_config(cfg.get("faults", {}))
    except KeyError as exc:
        raise ConfigError(f"missing configuration key {exc}") from exc
    except (TypeError, FaultConfigError) as exc:
        raise ConfigError(str(exc)) from exc
    return cfg


def load_config(path=None) -> dict:
    cfg = DEFAULTS
    if path:
        try:
            with open(path, "r", encoding="utf-8") as fh:
                cfg = _merge(DEFAULTS, json.load(fh))
        except (OSError, json.JSONDecodeError) as exc:
            raise ConfigError(f"cannot read config {path}: {exc}") from exc
    return cfg


def build_parser():
    p = argparse.ArgumentParser(
        description="Simulate a GPS receiver's NMEA 0183 output (not an RF simulator).")
    p.add_argument("--config", help="JSON config file (see config/simulator_config.json)")
    p.add_argument("--count", type=int, help="number of epochs to emit (default 10)")
    p.add_argument("--continuous", action="store_true", help="run until interrupted")
    p.add_argument("--interval", type=float, help="seconds between epochs (default 1.0)")
    p.add_argument("--lat", type=float, help="start latitude, decimal degrees")
    p.add_argument("--lon", type=float, help="start longitude, decimal degrees")
    p.add_argument("--altitude", type=float, help="altitude in metres")
    p.add_argument("--speed", type=float, help="speed in knots")
    p.add_argument("--heading", type=float, help="heading in degrees true (0..360)")
    p.add_argument("--talker", help="talker id: GP (GPS) or GN (combined GNSS), ...")
    p.add_argument("--sentences", help="comma list from rmc,gga,gsa,gsv (default rmc,gga)")
    p.add_argument("--utc-now", action="store_true",
                   help="start at the current UTC time (default: fixed, reproducible)")
    p.add_argument("--seed", type=int, help="random seed for fault injection")
    p.add_argument("--fault", action="append", default=[], metavar="NAME=PROB",
                   help="enable a fault with probability, repeatable. NAME is one of: "
                        + ", ".join(SENTENCE_FAULTS))
    p.add_argument("--fix-loss", metavar="START,DURATION",
                   help="simulate GPS fix loss from epoch START for DURATION epochs")
    p.add_argument("--transport", choices=("stdout", "pty"), default="stdout")
    p.add_argument("--chunk-size", type=int, default=0,
                   help="write N bytes at a time to exercise fragmented reception")
    p.add_argument("--chunk-delay", type=float, default=0.0,
                   help="seconds between chunks (with --chunk-size)")
    p.add_argument("--start-delay", type=float, default=3.0,
                   help="PTY mode: seconds to wait for a reader to open the device")
    p.add_argument("-q", "--quiet", action="store_true", help="no diagnostics on stderr")
    return p


def apply_cli(cfg: dict, args) -> dict:
    cfg = _merge(cfg, {})
    pairs = [
        (args.lat, "start", "latitude"), (args.lon, "start", "longitude"),
        (args.altitude, "start", "altitude_m"), (args.speed, "motion", "speed_knots"),
        (args.heading, "motion", "heading_deg"), (args.count, "output", "count"),
        (args.interval, "output", "interval_s"), (args.talker, "output", "talker"),
    ]
    for value, section, key in pairs:
        if value is not None:
            cfg[section][key] = value
    if args.continuous:
        cfg["output"]["count"] = 0
    if args.sentences:
        cfg["output"]["sentences"] = [s.strip().lower() for s in args.sentences.split(",")]
    faults = dict(cfg.get("faults", {}))
    probs = dict(faults.get("probabilities", {}))
    for item in args.fault:
        name, _, value = item.partition("=")
        try:
            probs[name] = float(value)
        except ValueError as exc:
            raise ConfigError(f"--fault expects NAME=PROB, got {item!r}") from exc
    if probs:
        faults["probabilities"] = probs
    if args.seed is not None:
        faults["seed"] = args.seed
    if args.fix_loss:
        try:
            start, duration = (int(x) for x in args.fix_loss.split(","))
        except ValueError as exc:
            raise ConfigError("--fix-loss expects START,DURATION") from exc
        faults["fix_loss"] = {"start_epoch": start, "duration": duration}
    cfg["faults"] = faults
    return cfg


def run(cfg: dict, transport, utc_now=False, quiet=False, sleep=time.sleep) -> int:
    """Emit epochs to transport. Returns number of epochs sent."""
    o = cfg["output"]
    start_time = datetime.now(timezone.utc) if utc_now else None
    route = RouteGenerator(cfg["start"]["latitude"], cfg["start"]["longitude"],
                           cfg["motion"]["speed_knots"], cfg["motion"]["heading_deg"],
                           cfg["start"].get("altitude_m", 0.0), start_time,
                           o.get("satellites", 8), o.get("hdop", 0.9))
    injector = FaultInjector(cfg.get("faults", {}))
    step_s = o["interval_s"] if o["interval_s"] > 0 else 1.0
    epoch = 0
    next_deadline = time.monotonic()
    while o["count"] == 0 or epoch < o["count"]:
        sample = route.current
        fix = not injector.fix_lost(epoch)
        for sentence in epoch_sentences(sample, o["talker"], o["sentences"], fix):
            for emission in injector.process(sentence):
                if emission.delay_s:
                    sleep(emission.delay_s)
                transport.write(emission.text)
                if emission.fault and not quiet:
                    print(f"[sim] epoch {epoch}: fault {emission.fault}", file=sys.stderr)
        epoch += 1
        route.step(step_s)
        if o["interval_s"] > 0:
            next_deadline += o["interval_s"]
            delay = next_deadline - time.monotonic()
            if delay > 0:
                sleep(delay)
    return epoch


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        cfg = validate_config(apply_cli(load_config(args.config), args))
        if args.chunk_size < 0:
            raise ConfigError("--chunk-size must be >= 0")
    except ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    kwargs = {"chunk_size": args.chunk_size, "chunk_delay_s": args.chunk_delay}
    try:
        if args.transport == "pty":
            transport = PtyTransport(**kwargs)
            print(f"[sim] PTY slave: {transport.slave_path}  "
                  f"(start the receiver with: ./build/gps_receiver < {transport.slave_path})",
                  file=sys.stderr, flush=True)
            time.sleep(args.start_delay)
        else:
            transport = StdoutTransport(**kwargs)
        try:
            sent = run(cfg, transport, args.utc_now, args.quiet)
        finally:
            if isinstance(transport, PtyTransport):
                transport.close(drain_s=0.5)
            else:
                transport.close()
        if not args.quiet:
            print(f"[sim] sent {sent} epochs", file=sys.stderr)
    except (RouteError, TransportError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
