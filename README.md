# Sonde Predict

Predicts radiosonde flight paths and landing points from GFS upper-air winds, so you can
tell whether a launch is worth hunting. A rewrite of Sonde Predict v3 (2016–2022) in
C++20 / Qt 6 for Windows and Linux. See [PLAN.md](PLAN.md) for the roadmap; the original
recovered source is in [recovered/](recovered/).

Status: the calculation core and a command-line predictor work; the desktop UI is next.

## Build

Needs CMake ≥ 3.21, Ninja, a C++20 compiler and Qt ≥ 6.5 (Core, Network, Test).

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=.toolchain/Qt/6.11.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
```

## Command line

```bash
./build/src/cli/sp-cli --launch 60.814,23.499,104 --time 2026-10-23T11:15 --home 60.17,24.94
```

Times are UTC. `--help` lists the balloon settings (ascent rate, burst altitude, descent
model), `--csv` writes the track, and `--compare` checks a prediction against a received
flight saved by RS41Tracker.

Wind data comes from the free [Open-Meteo](https://open-meteo.com) GFS API (non-commercial
use; see the quota note in PLAN.md).
