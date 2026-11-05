# Build and execution

## Requirements (Ubuntu 24.04)
`sudo apt install -y build-essential cmake python3 git cppcheck`

## Make targets
| Command | Action |
|---|---|
| `make` | release build -> `build/gps_receiver` |
| `make debug` | `-O0 -g` -> `build/debug/gps_receiver` |
| `make test-c` | build and run C unit tests |
| `make test-python` | Python unittest discovery |
| `make test-integration` | build binary, run simulator-to-receiver tests |
| `make sanitize` | rebuild in `build/asan` with ASan+UBSan, run C and integration tests |
| `make lint` | cppcheck |
| `make test` | c + python + integration |
| `make clean` | remove `build/` |

Compiler flags: `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wformat=2 -Wconversion -Werror`. Use `make WERROR=` to downgrade warnings if a newer compiler adds new ones.

## CMake
`cmake -S . -B cmake-build && cmake --build cmake-build && (cd cmake-build && ctest)`; add `-DGPS_SANITIZE=ON` for ASan/UBSan.

## Running
See README section 8. Key points: the simulator's stdout is NMEA only; the receiver reads stdin to EOF; `--stream` switches the receiver to machine-readable output for the dashboard.

## Environment variable
`GPS_RECEIVER_BIN` selects the receiver binary used by the integration tests (the sanitizer target sets it to the ASan build).

## Releases
```bash
git tag -a v1.0.0 -m "GPS Receiver Simulator v1.0.0"
git push origin v1.0.0
```
The Release workflow builds, runs all tests, packages `build/gps_receiver` + docs + simulator + dashboard + config, and creates the GitHub Release. A release exists only if that workflow run is green - check the Actions tab.
