# NMEA 0183 protocol notes

## Framing
```
$<talker><type>,<field>,<field>,...*<HH><CR><LF>
```
ASCII, printable characters only. `$` starts a sentence, `*` precedes a two-digit hexadecimal checksum, CR/LF ends it. Maximum 82 characters including `$` and CR/LF. Talker `GP`=GPS, `GN`=combined GNSS, `GL`=GLONASS, `GA`=Galileo, `GB`/`BD`=BeiDou.

## XOR checksum
XOR of every character strictly between `$` and `*`.
Example payload `GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,` -> `0x47`.
```python
c = 0
for ch in payload: c ^= ord(ch)
```
The C implementation distinguishes: no leading `$` / stray `$` (framing), no `*` (missing), not exactly two hex digits (malformed), valid but different (mismatch).

## RMC - Recommended Minimum
| # | Field | Example | Notes |
|---|---|---|---|
| 0 | ID | GPRMC | |
| 1 | UTC time | 123519 | hhmmss[.sss] |
| 2 | Status | A | A=valid, V=void |
| 3-4 | Latitude, N/S | 4807.038,N | ddmm.mmmm |
| 5-6 | Longitude, E/W | 01131.000,E | dddmm.mmmm |
| 7 | Speed | 022.4 | knots |
| 8 | Course | 084.4 | degrees true (may be empty) |
| 9 | Date | 230394 | ddmmyy |
| 10-11 | Magnetic variation | 003.1,W | optional |
| 12 | Mode | A | optional (NMEA 2.3+) |

## GGA - Fix data
| # | Field | Example | Notes |
|---|---|---|---|
| 1 | UTC time | 123519 | |
| 2-5 | Position | 4807.038,N,01131.000,E | |
| 6 | Quality | 1 | 0 none, 1 GPS, 2 DGPS, 3 PPS, 4 RTK, 5 float RTK, 6 estimated, 7 manual, 8 simulation |
| 7 | Satellites used | 08 | |
| 8 | HDOP | 0.9 | |
| 9-10 | Altitude, unit | 545.4,M | metres above mean sea level |
| 11-12 | Geoid separation | 46.9,M | |
| 13-14 | DGPS age, station | , | optional |

## GSA / GSV
GSA: mode (A/M), fix type (1 none, 2 2D, 3 3D), up to 12 PRNs, PDOP, HDOP, VDOP. GSV: total messages, message number, satellites in view, then 4 fields (PRN, elevation, azimuth, SNR) per satellite, up to 4 satellites per sentence.

## Coordinate conversion
`ddmm.mmmm` -> `dd + mm.mmmm/60`. `4807.038,N` = 48 + 7.038/60 = 48.1173. South and West are negative. The converter requires exactly 2 (lat) or 3 (lon) degree digits and checks minutes < 60 and degrees <= 90/180.

## Empty fields
Commas are kept when data is absent: `$GPRMC,123519,V,,,,,,,230394,,*..`. Empty fields are legal for a void fix; a sentence that claims a valid fix (`A`, quality>0) but lacks time/position/date is rejected.
