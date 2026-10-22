# Sonde Predict

Predicts where a radiosonde will land after its weather balloon bursts, from GFS upper-air
winds, so you can tell at a glance whether a launch is worth hunting. Shows the track, burst,
landing point and a 95% landing area on a map, with the distance and direction from your own
location; batch predictions for several days, Google Earth (KML) and GPS/phone (GPX) export.

![Sonde Predict](docs/manual/images/main-window.jpg)

## Download

| System | Download |
|---|---|
| **Windows** 10 / 11 (64-bit) | [Installer](https://github.com/oh2gba/SondePredict/releases/latest/download/SondePredict-windows-x64-setup.exe) · [Zip (no install)](https://github.com/oh2gba/SondePredict/releases/latest/download/SondePredict-windows-x64.zip) |
| **Linux** (x86-64) | [AppImage](https://github.com/oh2gba/SondePredict/releases/latest/download/SondePredict-x86_64.AppImage) — make it executable (`chmod +x`) and run it |

These links always give the newest version; older ones are on the
[releases page](https://github.com/oh2gba/SondePredict/releases). How to install, use and
uninstall it is in the [user manual](docs/manual/user-manual.md)
([PDF](docs/manual/user-manual.pdf)), which is also built into the app (Help → User manual, F1).

Sonde Predict 5 is a C++20 / Qt 6 rewrite of Sonde Predict v3 (2016–2022), which was
Windows-only. Coming from v3? File → Import Sonde Predict v3 settings brings over your
locations, balloon settings and batch schedule.

## Building from source

The rest of this page is for developers. [PLAN.md](PLAN.md) describes the design and the
roadmap; the recovered source of v3 is in [recovered/](recovered/).

Needs CMake ≥ 3.21, Ninja, a C++20 compiler, Qt ≥ 6.5 (Core, Network, Sql, Widgets, Quick,
QuickWidgets, Location, Positioning, Test) and the OpenGL development headers.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=".toolchain/Qt/6.11.3/gcc_64;.toolchain/sysroot/usr"
cmake --build build
ctest --test-dir build --output-on-failure
```

Then start the app (`sp-cli`, the command-line predictor, is in `build/src/cli`):

```bash
./build/src/app/sondepredict
```

## Packages and releases

* **Linux**: `packaging/linux/build-appimage.sh <qt-prefix>` builds a self-contained AppImage.
* **Releases**: pushing a `v*` tag runs `.github/workflows/release.yml`, which builds the Windows
  installer and zip and the AppImage and publishes them as a GitHub release.
* **Manual**: after editing `docs/manual/user-manual.md`, regenerate the PDF with
  `sondepredict --manual-pdf docs/manual/user-manual.pdf` (add `QT_QPA_PLATFORM=offscreen` on a
  machine without a display).

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

## Licence

Sonde Predict is free software under the [GNU General Public License v3.0](LICENSE).
Weather data by [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0); map data ©
[OpenStreetMap](https://www.openstreetmap.org/copyright) contributors.
