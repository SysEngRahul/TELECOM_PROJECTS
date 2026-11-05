# Testing strategy

| Layer | Tool | What it covers |
|---|---|---|
| C unit | custom harness (`tests/c/test_harness.h`) | checksum (valid/invalid/missing/malformed/framing/length), coordinates (N/S/E/W, boundaries, range, format), parser (RMC/GGA/GSA/GSV, empty/invalid fields, leap day, field limits, length boundary 80/81), receiver (single, multiple, **every possible 2-way split**, byte-at-a-time, bad checksum leaves state untouched, incomplete, oversized, non-printable, LF-only, no-fix keeps last position, timeout/recovery, NULL safety) |
| Python unit | `unittest` | NMEA formatting incl. the 59.99999-minute rounding trap, checksum, routes (distance vs haversine, wrap, bounds), fault injection (each fault, reproducibility by seed, probability), config validation, transports (chunking, PTY raw round-trip), dashboard parsing, CSV |
| Integration | `unittest` + subprocess | real `simulator | gps_receiver` pipeline: known positions round-trip, hemispheres, movement, GNSS talker, all sentence types, 1-byte and 3-byte fragmentation, fix loss and recovery, timeout event on idle pipe, PTY transport, each fault type through the real binary, seeded random faults, garbage/binary noise, oversized lines, partial sentence at EOF |
| Sanitizers | ASan + UBSan | C unit tests and integration tests run against the instrumented binary; any error aborts |
| Static analysis | cppcheck + `-Wall -Wextra -Wpedantic -Wconversion -Werror` | |

## Principles
- Tests execute the real C binary; the Python generator is an independent oracle for the C parser.
- Corrupted input must be *counted and rejected* and must never change navigation state.
- Fault injection is seeded; failures are reproducible.
- Limits of the suite: no fuzzing, no hardware-in-the-loop, no timing/baud simulation, no real receiver captures.
