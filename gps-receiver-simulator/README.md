# GPS Receiver Simulator & NMEA 0183 Processing System

[![CI](https://github.com/SysEngRahul/gps-receiver-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/SysEngRahul/gps-receiver-simulator/actions/workflows/ci.yml)

A software-only GPS **receiver-output** simulator (Python) feeding an **embedded-style NMEA 0183 receiver** (C11) through a simulated serial link, with a terminal dashboard, CSV logging, fault injection, unit/integration tests, sanitizer runs and GitHub Actions CI/CD.

> **Scope / honesty note.** This is *not* a satellite RF simulator. It does no signal acquisition, tracking loops, pseudorange measurement or position solving. Python generates *known* positions and renders them as the NMEA sentences a GPS module would emit; the C code processes those sentences the way MCU firmware reads a module on a UART. A Unix pipe is not a UART, and a PTY only approximates a serial device (no baud-rate timing, no electrical layer).

## 1. Problem statement
A GPS module delivers navigation data as a byte stream over UART. Firmware cannot assume one `read()` returns one sentence, that sentences are intact, or that the module always has a fix. This project builds and tests that receive path: framing, checksum validation, field validation, state handling and timeout detection.

## 2. Objectives
Generate NMEA (RMC, GGA, GSA, GSV) · transport it as a byte stream · frame/validate/parse in C · convert `ddmm.mmmm` to decimal degrees · reject corrupted, incomplete, oversized and stale data without polluting state · display and log results · prove it with automated tests and CI.

## 3. Architecture
```
 Python simulator            NMEA generator          Transport
 route_generator.py  --->  nmea_generator.py  --->  serial_transport.py  (stdout pipe | PTY)
                              ^ fault_injector.py                         |
                                                                           v bytes
 C receiver:  gps_receiver (framing, stats, timeout)
                 -> nmea_parser (fields) -> nmea_checksum -> coordinate_converter
                 -> gps_fix_t (latest valid state)
                                |
                  gps_receiver --stream  (text protocol)
                                v
                 dashboard/gps_dashboard.py  +  csv_logger.py
```
Details: [docs/architecture.md](docs/architecture.md).

## 4. Directory structure
```
include/ src/          C receiver (checksum, coordinates, parser, stream receiver, main)
simulator/             route, NMEA generator, fault injector, transports, CLI
dashboard/             terminal dashboard + CSV logger (reads receiver output only)
tests/c|python|integration
config/ data/ docs/ results/ .github/workflows/{ci,release}.yml
Makefile CMakeLists.txt
```

## 5. Technology stack
C11 (GCC), POSIX (`poll`, `clock_gettime`), Python 3 standard library only, GNU Make and CMake, cppcheck, ASan/UBSan, GitHub Actions.

## 6. Installation (Ubuntu 24.04)
```bash
sudo apt update
sudo apt install -y build-essential cmake python3 git cppcheck
```

## 7. Build
```bash
make                 # release  -> build/gps_receiver
make debug           # -O0 -g   -> build/debug/gps_receiver
make clean
# CMake alternative
cmake -S . -B cmake-build && cmake --build cmake-build && (cd cmake-build && ctest)
cmake -S . -B cmake-san -DGPS_SANITIZE=ON   # sanitizer build
```

## 8. Run
```bash
python3 simulator/gps_simulator.py --help
python3 simulator/gps_simulator.py --count 10 --interval 1
python3 simulator/gps_simulator.py --count 10 --interval 1 | ./build/gps_receiver

# live dashboard + CSV log
python3 simulator/gps_simulator.py --count 30 --interval 1 \
  | ./build/gps_receiver --stream | python3 dashboard/gps_dashboard.py --csv results/run.csv

# faults (seeded, reproducible)
python3 simulator/gps_simulator.py --count 50 --interval 0 --seed 1 \
  --fault invalid_checksum=0.1 --fault truncated=0.05 --fix-loss 20,5 | ./build/gps_receiver

# fragmented delivery (3 bytes at a time)
python3 simulator/gps_simulator.py --count 5 --interval 0 --chunk-size 3 | ./build/gps_receiver

# pseudo-terminal "serial port" (two terminals)
python3 simulator/gps_simulator.py --transport pty --count 10 --interval 1   # prints /dev/pts/N
./build/gps_receiver < /dev/pts/N
```
Pipeline mode writes **only NMEA to stdout**; diagnostics go to stderr. The receiver reads stdin until EOF.

### Simulator options
`--config FILE` `--count N` `--continuous` `--interval S` `--lat` `--lon` `--altitude` `--speed` (knots) `--heading` `--talker GP|GN|...` `--sentences rmc,gga,gsa,gsv` `--utc-now` `--seed N` `--fault NAME=PROB` `--fix-loss START,DURATION` `--transport stdout|pty` `--chunk-size N` `--chunk-delay S` `--start-delay S` `-q`.
Faults: `invalid_checksum missing_checksum truncated invalid_coords missing_fields random_corruption delay repeated`.
Defaults start at 28.6139 N, 77.2090 E, 22.4 kn, heading 84.4, fixed start time 2024-01-01T12:00:00Z (reproducible).

### Receiver options
`--stream` (machine-readable `FIX`/`STATS` lines) · `--timeout-ms N` (default 5000) · `--help`.

## 9. Sample NMEA
```
$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
```
More in `data/sample_nmea.txt`.

## 10. Example output
Output of `python3 simulator/gps_simulator.py --count 10 --interval 0 -q | ./build/gps_receiver` (actual run):
```
========== GPS RECEIVER ==========

GPS FIX STATUS   : VALID
DATA STATUS      : FRESH
LATITUDE         : 28.613992
LONGITUDE        : 77.210057
ALTITUDE         : 215.0 m
SPEED            : 22.4 knots (41.5 km/h)
COURSE           : 084.4 degrees
UTC              : 2024-01-01 12:00:09
SATELLITES USED  : 8 (in view: 0)
HDOP             : 0.9

VALID MESSAGES   : 20
INVALID MESSAGES : 0
  checksum       : 0
  format/fields  : 0
  overflow       : 0
  incomplete     : 0
IGNORED (type)   : 0

==================================
```
(`--interval 0` sends as fast as possible but still advances simulated time 1 s per epoch.)

## 11. Testing
```bash
make test-c            # C unit tests (checksum, coordinates, parser, stream receiver)
make test-python       # unittest: generator, routes, faults, config, transport, dashboard
make test-integration  # real simulator | real C binary, incl. corrupted input and PTY
make sanitize          # whole C suite + integration tests under ASan + UBSan
make lint              # cppcheck
make test              # c + python + integration
```
Strategy: [docs/testing_strategy.md](docs/testing_strategy.md).

## 12. CI/CD
`ci.yml` runs on push to `main` and PRs to `main`: **build** (make release/debug + CMake) · **c-tests** · **python-tests** · **integration** · **sanitizers** (ASan/UBSan) · **quality** (strict warnings, cppcheck). Permissions are `contents: read`.
`release.yml` runs on tags like `v1.0.0`: build, run the full test suite, package binary + docs, then a separate job with `contents: write` creates the GitHub Release via `gh` using the built-in `GITHUB_TOKEN` (no third-party actions, no extra secrets).
```bash
git tag -a v1.0.0 -m "GPS Receiver Simulator v1.0.0"
git push origin v1.0.0     # then watch GitHub -> Actions -> Release
```

## 13. Known limitations
- Not an RF/GNSS simulator (see scope note). Straight-line, constant-speed motion on a flat-earth approximation.
- Supported sentences: RMC, GGA, GSA, GSV (GSV: only the satellites-in-view count is used). Talkers GP, GN, GL, GA, GB, BD. Other types are counted as "ignored".
- Max sentence length 80 characters before CR/LF (NMEA 82 including CRLF); longer proprietary sentences are rejected.
- Timeout is "no valid sentence for N ms". A well-formed "no fix" sentence still counts as data.
- Two-digit year pivot: 80–99 → 19xx, 00–79 → 20xx.
- Not verified here: execution of the GitHub Actions workflows themselves (they are syntax-checked only until you push).

## 14. Future improvements
Multi-constellation GSV detail (PRN/elevation/azimuth/SNR), GLL/VTG, UBX/binary protocols, interrupt-driven UART port to BeagleBone Black or an MCU (replace `read()` with an ISR + ring buffer), waypoint routes, map output (GPX/KML), fuzzing (libFuzzer/AFL) of the parser.
