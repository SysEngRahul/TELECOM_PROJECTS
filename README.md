# TELECOM_PROJECTS

A collection of practical projects covering embedded systems, telecommunications,
GNSS/GPS, communication protocols, Linux systems, and firmware development.

This repository uses a monorepo structure. Each project has its own directory,
with shared GitHub Actions workflows maintained at the repository root.

## Projects

| Project | Description | Technologies |
|---|---|---|
| [GPS Receiver Simulator](./gps-receiver-simulator/) | GPS receiver simulation with NMEA generation, parsing, validation, fault injection, and a monitoring dashboard. | Embedded C, Python, CMake, Make, NMEA 0183 |

## Repository Structure

```text
TELECOM_PROJECTS/
├── .github/
│   └── workflows/
│       ├── ci.yml
│       └── release.yml
├── gps-receiver-simulator/
│   ├── config/
│   ├── dashboard/
│   ├── data/
│   ├── docs/
│   ├── include/
│   ├── simulator/
│   ├── src/
│   └── tests/
├── .gitignore
└── README.md
```

## Build and Test

```bash
cd gps-receiver-simulator
make
make test
```

## Continuous Integration

GitHub Actions workflows are maintained centrally under `.github/workflows/`.
The CI workflow validates compilation, C unit tests, Python tests, integration
tests, sanitizer checks, and code quality.

## Adding a New Project

1. Create a new directory at the repository root.
2. Keep source code, documentation, tests, and build files inside that directory.
3. Update the root CI workflow if automated testing is required.
4. Include a project-specific README with setup and execution instructions.

## License

Each project may have its own license. Refer to its individual directory
for licensing terms.
