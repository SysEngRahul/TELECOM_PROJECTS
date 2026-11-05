"""Deterministic route generation.

Movement model (deliberately simple, see docs/architecture.md):
  * constant speed and heading (great-circle effects ignored)
  * local flat-earth approximation: 1 degree of latitude = R*pi/180 metres,
    longitude scaled by cos(latitude)
  * good to a few metres over short legs; error grows with distance and at
    high latitudes. Not suitable for navigation, fine for generating test data.
"""
import math
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone

EARTH_RADIUS_M = 6_371_000.0
KNOT_TO_MPS = 1852.0 / 3600.0
MAX_ROUTE_LATITUDE = 89.9  # the flat-earth model breaks down at the poles


class RouteError(ValueError):
    """Invalid route parameters or a route that leaves the supported area."""


@dataclass(frozen=True)
class NavSample:
    """One epoch of navigation truth used to build NMEA sentences."""

    latitude: float
    longitude: float
    altitude_m: float
    speed_knots: float
    course_deg: float
    utc: datetime
    satellites: int = 8
    hdop: float = 0.9


def validate_coordinates(latitude: float, longitude: float) -> None:
    if not -90.0 <= latitude <= 90.0:
        raise RouteError(f"latitude {latitude} outside -90..90")
    if not -180.0 <= longitude <= 180.0:
        raise RouteError(f"longitude {longitude} outside -180..180")


def wrap_longitude(longitude: float) -> float:
    """Wrap into [-180, 180)."""
    return (longitude + 180.0) % 360.0 - 180.0


class RouteGenerator:
    def __init__(self, latitude, longitude, speed_knots=20.0, heading_deg=90.0,
                 altitude_m=215.0, start_time=None, satellites=8, hdop=0.9):
        validate_coordinates(latitude, longitude)
        if abs(latitude) > MAX_ROUTE_LATITUDE:
            raise RouteError(f"latitude {latitude} too close to a pole for this model")
        if speed_knots < 0:
            raise RouteError("speed must be >= 0")
        if not 0.0 <= heading_deg <= 360.0:
            raise RouteError("heading must be within 0..360")
        if not 0 <= satellites <= 99:
            raise RouteError("satellites must be 0..99")
        if hdop < 0:
            raise RouteError("hdop must be >= 0")
        if start_time is None:
            start_time = datetime(2024, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
        self._sample = NavSample(latitude, longitude, altitude_m, speed_knots,
                                 heading_deg % 360.0, start_time, satellites, hdop)

    @property
    def current(self) -> NavSample:
        return self._sample

    def step(self, dt_seconds: float) -> NavSample:
        """Advance the route by dt_seconds and return the new sample."""
        if dt_seconds < 0:
            raise RouteError("dt must be >= 0")
        s = self._sample
        distance = s.speed_knots * KNOT_TO_MPS * dt_seconds
        heading = math.radians(s.course_deg)
        dlat = math.degrees(distance * math.cos(heading) / EARTH_RADIUS_M)
        dlon = math.degrees(distance * math.sin(heading) /
                            (EARTH_RADIUS_M * math.cos(math.radians(s.latitude))))
        lat = s.latitude + dlat
        if abs(lat) > MAX_ROUTE_LATITUDE:
            raise RouteError("route reached the polar limit of the movement model")
        lon = wrap_longitude(s.longitude + dlon)
        validate_coordinates(lat, lon)
        self._sample = replace(s, latitude=lat, longitude=lon,
                               utc=s.utc + timedelta(seconds=dt_seconds))
        return self._sample
