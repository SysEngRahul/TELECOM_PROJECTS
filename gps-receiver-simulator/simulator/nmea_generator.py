"""NMEA 0183 sentence generation (RMC, GGA, GSA, GSV).

Every function returns a complete sentence including '$', '*HH' and CRLF.
"""
import math

CRLF = "\r\n"
VALID_TALKERS = ("GP", "GN", "GL", "GA", "GB", "BD")
KNOT_TO_KMH = 1.852


def checksum(payload: str) -> int:
    """XOR of all characters between '$' and '*' (both excluded)."""
    value = 0
    for ch in payload:
        value ^= ord(ch)
    return value


def build_sentence(payload: str) -> str:
    """Wrap payload as '$payload*HH\\r\\n'."""
    return f"${payload}*{checksum(payload):02X}{CRLF}"


def _check_talker(talker: str) -> str:
    if talker not in VALID_TALKERS:
        raise ValueError(f"unsupported talker id {talker!r}")
    return talker


def _format_angle(value: float, degree_digits: int):
    """Return (ddmm.mmmm, sign) using integer arithmetic.

    Rounding the *total* in units of 1e-4 minutes avoids the classic bug where
    59.99999 minutes rounds up to '60.0000'.
    """
    total = round(abs(value) * 60.0 * 10_000)
    degrees, rest = divmod(total, 60 * 10_000)
    minutes = rest / 10_000
    return f"{degrees:0{degree_digits}d}{minutes:07.4f}", value < 0


def format_latitude(lat: float):
    """-> ('ddmm.mmmm', 'N'|'S')"""
    if not -90.0 <= lat <= 90.0:
        raise ValueError(f"latitude {lat} out of range")
    text, neg = _format_angle(lat, 2)
    return text, "S" if neg else "N"


def format_longitude(lon: float):
    """-> ('dddmm.mmmm', 'E'|'W')"""
    if not -180.0 <= lon <= 180.0:
        raise ValueError(f"longitude {lon} out of range")
    text, neg = _format_angle(lon, 3)
    return text, "W" if neg else "E"


def format_time(utc) -> str:
    return f"{utc:%H%M%S}.{utc.microsecond // 10_000:02d}"


def format_date(utc) -> str:
    return f"{utc:%d%m%y}"


def rmc(sample, talker="GP", fix=True) -> str:
    """Recommended Minimum data. fix=False emits a 'void' sentence (status V)."""
    talker = _check_talker(talker)
    t = format_time(sample.utc)
    d = format_date(sample.utc)
    if fix:
        lat, ns = format_latitude(sample.latitude)
        lon, ew = format_longitude(sample.longitude)
        payload = (f"{talker}RMC,{t},A,{lat},{ns},{lon},{ew},"
                   f"{sample.speed_knots:.1f},{sample.course_deg:.1f},{d},,,A")
    else:
        payload = f"{talker}RMC,{t},V,,,,,,,{d},,,N"
    return build_sentence(payload)


def gga(sample, talker="GP", fix=True) -> str:
    """Global positioning fix data. fix=False emits quality 0 with empty position."""
    talker = _check_talker(talker)
    t = format_time(sample.utc)
    if fix:
        lat, ns = format_latitude(sample.latitude)
        lon, ew = format_longitude(sample.longitude)
        payload = (f"{talker}GGA,{t},{lat},{ns},{lon},{ew},1,{sample.satellites:02d},"
                   f"{sample.hdop:.1f},{sample.altitude_m:.1f},M,0.0,M,,")
    else:
        payload = f"{talker}GGA,{t},,,,,0,00,,,M,,M,,"
    return build_sentence(payload)


def gsa(sample, talker="GP", fix=True) -> str:
    """DOP and active satellites."""
    talker = _check_talker(talker)
    if fix:
        prns = [f"{n:02d}" for n in range(1, min(sample.satellites, 12) + 1)]
        prns += [""] * (12 - len(prns))
        dop = f"{sample.hdop * 1.5:.1f},{sample.hdop:.1f},{sample.hdop * 1.2:.1f}"
        payload = f"{talker}GSA,A,3,{','.join(prns)},{dop}"
    else:
        payload = f"{talker}GSA,A,1,,,,,,,,,,,,,,,"
    return build_sentence(payload)


def gsv(sample, talker="GP", fix=True) -> list:
    """Satellites in view; returns a list of sentences (4 satellites each).

    Elevation/azimuth/SNR are synthetic but within the legal ranges.
    """
    talker = _check_talker(talker)
    count = sample.satellites if fix else 0
    if count == 0:
        return [build_sentence(f"{talker}GSV,1,1,00")]
    messages = math.ceil(count / 4)
    out = []
    for m in range(messages):
        fields = [f"{talker}GSV", str(messages), str(m + 1), f"{count:02d}"]
        for prn in range(m * 4 + 1, min(count, m * 4 + 4) + 1):
            elevation = 15 + (prn * 7) % 70
            azimuth = (prn * 47) % 360
            snr = 30 + (prn * 3) % 20
            fields += [f"{prn:02d}", f"{elevation:02d}", f"{azimuth:03d}", f"{snr:02d}"]
        out.append(build_sentence(",".join(fields)))
    return out


SENTENCE_BUILDERS = ("rmc", "gga", "gsa", "gsv")


def epoch_sentences(sample, talker="GP", kinds=("rmc", "gga"), fix=True) -> list:
    """All requested sentences for one epoch, in order."""
    out = []
    for kind in kinds:
        if kind == "rmc":
            out.append(rmc(sample, talker, fix))
        elif kind == "gga":
            out.append(gga(sample, talker, fix))
        elif kind == "gsa":
            out.append(gsa(sample, talker, fix))
        elif kind == "gsv":
            out.extend(gsv(sample, talker, fix))
        else:
            raise ValueError(f"unknown sentence kind {kind!r}")
    return out
