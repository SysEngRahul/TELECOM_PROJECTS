"""Controlled, reproducible fault injection for NMEA sentences.

Randomness comes from a private random.Random(seed), so a given seed and
configuration always produces the same fault pattern.

Faults (sentence level):
  invalid_checksum, missing_checksum, truncated, invalid_coords,
  missing_fields, random_corruption, delay, repeated
Faults (epoch level):
  fix_loss  - window of epochs during which the receiver reports no fix
"""
import random
from dataclasses import dataclass

CRLF = "\r\n"

try:  # allow use both as a script (same dir on sys.path) and as a module
    from nmea_generator import build_sentence, checksum
except ImportError:  # pragma: no cover
    from simulator.nmea_generator import build_sentence, checksum

SENTENCE_FAULTS = ("invalid_checksum", "missing_checksum", "truncated",
                   "invalid_coords", "missing_fields", "random_corruption",
                   "delay", "repeated")


@dataclass
class Emission:
    """One chunk of text to transmit, optionally after a delay."""
    text: str
    delay_s: float = 0.0
    fault: str = ""  # name of the fault that produced it ('' = clean)


class FaultConfigError(ValueError):
    pass


def validate_fault_config(cfg: dict) -> dict:
    """Return a normalised copy of the 'faults' config section or raise."""
    cfg = dict(cfg or {})
    out = {"seed": int(cfg.get("seed", 0)), "probabilities": {}, "delay_s": 0.5,
           "repeat_count": 2, "fix_loss": None}
    probs = cfg.get("probabilities", {})
    total = 0.0
    for name, p in probs.items():
        if name not in SENTENCE_FAULTS:
            raise FaultConfigError(f"unknown fault {name!r}")
        if not isinstance(p, (int, float)) or not 0.0 <= p <= 1.0:
            raise FaultConfigError(f"probability for {name} must be within 0..1")
        total += p
        out["probabilities"][name] = float(p)
    if total > 1.0 + 1e-9:
        raise FaultConfigError("fault probabilities sum to more than 1")
    if cfg.get("delay_s", 0.5) < 0:
        raise FaultConfigError("delay_s must be >= 0")
    out["delay_s"] = float(cfg.get("delay_s", 0.5))
    rc = int(cfg.get("repeat_count", 2))
    if rc < 2:
        raise FaultConfigError("repeat_count must be >= 2")
    out["repeat_count"] = rc
    fl = cfg.get("fix_loss")
    if fl:
        start, dur = int(fl.get("start_epoch", 0)), int(fl.get("duration", 0))
        if start < 0 or dur < 0:
            raise FaultConfigError("fix_loss values must be >= 0")
        out["fix_loss"] = (start, dur)
    return out


def _split(sentence: str):
    """'$payload*HH\\r\\n' -> payload (without $, *HH and CRLF)."""
    body = sentence.rstrip("\r\n")
    star = body.rfind("*")
    return body[1:star] if star != -1 else body[1:]


class FaultInjector:
    def __init__(self, config=None):
        self.config = validate_fault_config(config or {})
        self.rng = random.Random(self.config["seed"])

    # --------------------------------------------------------- epoch level
    def fix_lost(self, epoch: int) -> bool:
        window = self.config["fix_loss"]
        if not window:
            return False
        start, duration = window
        return start <= epoch < start + duration

    # ------------------------------------------------------ sentence level
    def apply(self, sentence: str, fault: str) -> list:
        """Apply a named fault to one sentence; returns a list of Emission."""
        if fault == "invalid_checksum":
            return [Emission(self._bad_checksum(sentence), fault=fault)]
        if fault == "missing_checksum":
            return [Emission("$" + _split(sentence) + CRLF, fault=fault)]
        if fault == "truncated":
            return [Emission(self._truncate(sentence), fault=fault)]
        if fault == "invalid_coords":
            return [Emission(self._invalid_coords(sentence), fault=fault)]
        if fault == "missing_fields":
            return [Emission(self._missing_fields(sentence), fault=fault)]
        if fault == "random_corruption":
            return [Emission(self._corrupt(sentence), fault=fault)]
        if fault == "delay":
            return [Emission(sentence, delay_s=self.config["delay_s"], fault=fault)]
        if fault == "repeated":
            return [Emission(sentence, fault=fault)
                    for _ in range(self.config["repeat_count"])]
        raise FaultConfigError(f"unknown fault {fault!r}")

    def process(self, sentence: str) -> list:
        """Roll the dice once for this sentence; return its emissions."""
        roll = self.rng.random()  # always consume one draw => stable sequences
        acc = 0.0
        for name in SENTENCE_FAULTS:
            acc += self.config["probabilities"].get(name, 0.0)
            if roll < acc:
                return self.apply(sentence, name)
        return [Emission(sentence)]

    # ------------------------------------------------------- fault bodies
    def _bad_checksum(self, sentence):
        payload = _split(sentence)
        good = checksum(payload)
        bad = good ^ self.rng.randint(1, 255)  # guaranteed different
        return f"${payload}*{bad:02X}{CRLF}"

    def _truncate(self, sentence):
        """Cut the sentence mid-way and drop CRLF, like a lost UART tail."""
        body = sentence.rstrip("\r\n")
        cut = self.rng.randint(4, max(5, len(body) - 4))
        return body[:cut]

    def _rebuild(self, fields):
        return build_sentence(",".join(fields))

    def _invalid_coords(self, sentence):
        """Valid checksum, impossible coordinates (latitude 99 degrees)."""
        fields = _split(sentence).split(",")
        kind = fields[0][2:]
        idx = {"RMC": 3, "GGA": 2}.get(kind)
        if idx is None or idx >= len(fields) or not fields[idx]:
            return self._bad_checksum(sentence)
        fields[idx] = "9961.5000"
        return self._rebuild(fields)

    def _missing_fields(self, sentence):
        """Valid checksum but the tail of the sentence is gone."""
        fields = _split(sentence).split(",")
        keep = max(2, len(fields) // 2 - self.rng.randint(0, 1))
        return self._rebuild(fields[:keep])

    def _corrupt(self, sentence):
        """Overwrite 1-3 characters; the transmitted checksum is left untouched.

        Retries until the checksum really mismatches (several edits could in
        theory cancel out in the XOR), so the result is always detectable.
        """
        alphabet = "0123456789ABCDEFXYZ,.;#"
        original = sentence.rstrip("\r\n")
        while True:
            body = list(original)
            star = len(body) - 3
            for _ in range(self.rng.randint(1, 3)):
                pos = self.rng.randint(1, max(1, star - 1))
                body[pos] = self.rng.choice([c for c in alphabet if c != body[pos]])
            text = "".join(body)
            if checksum(text[1:star]) != int(text[star + 1:], 16):
                return text + CRLF
