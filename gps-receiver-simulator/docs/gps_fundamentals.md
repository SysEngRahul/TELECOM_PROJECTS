# GPS / GNSS fundamentals (as relevant to this project)

- **GNSS**: umbrella term for satellite navigation systems - GPS (US), GLONASS (Russia), Galileo (EU), BeiDou (China).
- **How a fix works (not simulated here):** a receiver measures signal travel time (pseudorange) to at least four satellites and solves for x, y, z and its own clock error. Modules perform this internally and report the *result* in NMEA.
- **What the project does simulate:** only the module's serial output.
- **HDOP**: geometric quality factor; lower is better (about 1 is excellent, above 5 is poor). Not a distance error by itself.
- **Fix types**: no fix, 2D (needs 3 satellites, no altitude), 3D (4+ satellites).
- **Satellites used vs in view**: GGA reports used in the solution, GSV reports visible.
- **UTC and date**: time comes in every sentence, the date only in RMC.
- **Speed over ground / course over ground**: derived from Doppler/position changes, reported in knots and degrees true.
- **Knots**: 1 kn = 1.852 km/h.
- **UART link**: modules typically use 9600 baud, 8N1 (one start bit, 8 data bits, no parity, one stop bit), about 960 bytes/s, so ~10 sentences of 80 bytes per second saturate it. Receivers read bytes from an ISR or DMA into a ring buffer, hence the need for stream parsing.
