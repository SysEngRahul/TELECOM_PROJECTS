# Architecture

## Data flow
`route_generator` (truth) -> `nmea_generator` (sentences) -> `fault_injector` (optional corruption) -> `serial_transport` (bytes) -> **C receiver** -> `gps_fix_t` -> `--stream` text -> dashboard / CSV.

## Module responsibilities
| Module | Responsibility | Depends on |
|---|---|---|
| `nmea_checksum` | XOR compute, hex parse, framing/missing/malformed/mismatch classification | - |
| `coordinate_converter` | `(d)ddmm.mmmm` + hemisphere -> signed degrees, range checks | - |
| `nmea_parser` | Validate one sentence (checksum, fields) -> typed message; never leaves partial data | checksum, converter |
| `gps_receiver` | Byte-stream framing, statistics, state update, timeout | parser |
| `main.c` | stdin loop (`poll`), signals, output formatting | receiver |
| `simulator/*` | Test-data generation only; shares no code with the receiver | - |
| `dashboard/*` | Reads the receiver's text protocol only; never parses NMEA | - |

The receiver and simulator are deliberately independent implementations of the same protocol: the Python checksum cannot mask a C checksum bug and vice versa.

## Receiver state machine
```
IDLE --'$'--> COLLECTING --CR/LF--> [parse] --> IDLE
                |  '$'  : report "incomplete", restart
                |  >80 chars: report "overflow" once -> OVERFLOW (drop until CR/LF or '$')
```
Bytes outside a sentence are counted and ignored. A non-printable byte flags the sentence as corrupt; it is rejected at end of line. `gps_receiver_finish()` reports a sentence still open at EOF as incomplete.

## State-update rules
1. Only sentences that pass framing, checksum and every field check update `gps_fix_t`.
2. RMC `A` / GGA quality>0 update position; RMC `V` / GGA quality 0 set `nav_valid=false` but keep the *last known* position (flagged in the output).
3. `nav_valid` reflects the most recent fix-status sentence (RMC or GGA).
4. Freshness uses a monotonic clock supplied by the caller (`now_ms`), so the module is testable without sleeping.

## Error taxonomy
checksum (missing/malformed/mismatch) · format (framing, too few/many fields, bad/missing field) · overflow · incomplete. "Invalid messages" = sum of these. Valid sentences of unsupported type are "ignored", not invalid.

## Movement model limits
Constant speed/heading, flat-earth approximation (1 deg latitude = R*pi/180, longitude scaled by cos(lat)); error grows with distance and near poles (rejected above 89.9 deg). Longitude wraps at the antimeridian. Altitude, satellites and HDOP are constant.

## Transports
- stdout pipe: kernel byte stream, no baud/framing/parity.
- PTY: `openpty()`, slave set raw (`tty.setraw`) so no echo or CR/LF translation; reader sees `EIO` when the master closes (the receiver treats that as end of stream). Closest to a serial device available without hardware; still no electrical or timing layer.
