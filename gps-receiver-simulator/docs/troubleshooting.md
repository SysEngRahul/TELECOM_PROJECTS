# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `build/gps_receiver: No such file` | run `make` first |
| Integration tests: "not built" | `make test-integration` builds it; with plain `unittest` set `GPS_RECEIVER_BIN` |
| Receiver prints nothing until the end | default mode prints a summary at EOF; use `--stream` for live lines |
| Dashboard shows nothing | the dashboard needs `gps_receiver --stream`, not the default mode |
| Receiver hangs | stdin is still open - the pipe producer is alive; Ctrl-C prints the summary |
| PTY: receiver gets no data | start the receiver within `--start-delay` seconds (default 3), use `< /dev/pts/N` |
| PTY: `Permission denied` | the slave device belongs to the user who ran the simulator |
| `WARNING: no valid GPS data` | no accepted sentence for `--timeout-ms`; check the producer and the checksum |
| Many `REJECT reason="checksum mismatch"` | stream corruption or a producer using the wrong checksum range (must exclude `$` and `*`) |
| New compiler breaks the build with `-Werror` | `make WERROR=` and report the warning |
| `make sanitize` dies at start-up with an ASan shadow-memory error | known incompatibility between ASan and high ASLR entropy on some newer Ubuntu kernels; try `sudo sysctl vm.mmap_rnd_bits=28` |
