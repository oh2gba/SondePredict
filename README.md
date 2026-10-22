# Sonde Predict

Predicts radiosonde flight paths and landing points from GFS upper-air winds, so you can
tell whether a launch is worth hunting. A rewrite of Sonde Predict v3 (2016–2022) in
C++20 / Qt 6 for Windows and Linux. See [PLAN.md](PLAN.md) for the roadmap; the original
recovered source is in [recovered/](recovered/).

Status: the desktop app (`sondepredict`) and a command-line predictor (`sp-cli`) work:
predictions on an OpenStreetMap view, batch runs and a scheduler, export to Google Earth (KML)
and GPS apps (GPX), saved locations and predictions, and import of v3 settings.

## User manual

[docs/manual/user-manual.md](docs/manual/user-manual.md) (also built into the app: Help → User
manual, F1) and as a PDF: [docs/manual/user-manual.pdf](docs/manual/user-manual.pdf). After
editing the manual, regenerate the PDF with `sondepredict --manual-pdf docs/manual/user-manual.pdf`
(add `QT_QPA_PLATFORM=offscreen` on a machine without a display).

## Packages

* **Linux**: `packaging/linux/build-appimage.sh <qt-prefix>` builds a self-contained AppImage.
* **Windows**: pushing a `v*` tag runs `.github/workflows/release.yml`, which builds an installer
  and a zip (and the AppImage) and attaches them to a GitHub release.

## Build

Needs CMake ≥ 3.21, Ninja, a C++20 compiler, Qt ≥ 6.5 (Core, Network, Sql, Widgets, Quick,
QuickWidgets, Location, Positioning, Test) and the OpenGL development headers.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=".toolchain/Qt/6.11.3/gcc_64;.toolchain/sysroot/usr"
cmake --build build
ctest --test-dir build --output-on-failure
```

Then start the app:

```bash
./build/src/app/sondepredict
```

## Command line

```bash
./build/src/cli/sp-cli --launch 60.814,23.499,104 --time 2026-10-23T11:15 --home 60.17,24.94
```

Times are UTC. `--help` lists the balloon settings (ascent rate, burst altitude, descent
model), `--csv` writes the track, and `--compare` checks a prediction against a received
flight saved by RS41Tracker.

Coming from v3? Import your old settings once, then use location names:

```bash
./build/src/cli/sp-cli --import-v3 /path/to/sonde_predict/config.ini
```

```bash
./build/src/cli/sp-cli --launch Jokioinen --home "Home, Espoo" --time 2026-10-23T11:15 --save
```

`--locations` lists saved locations and `--list` the saved predictions. `--batch` runs the batch
set up in the app, `--kml` / `--gpx` write the result for Google Earth or GPS apps, and
`--export 3,5 --kml out.kml` exports stored predictions. Settings live in
`~/.config/SondePredict/sondepredict.ini` (Windows: `%LOCALAPPDATA%\SondePredict`), predictions
in `~/.local/share/SondePredict/predictions.sqlite`.

Wind data comes from the free [Open-Meteo](https://open-meteo.com) GFS API (non-commercial
use; see the quota note in PLAN.md).
