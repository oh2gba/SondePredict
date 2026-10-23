# Sonde Predict 5 — rewrite plan

Sonde Predict (v3, last built Dec 2022) predicts where a radiosonde lands, using GFS
upper-air winds, so you know whether a launch is worth hunting. The original was Python 2.7 +
wxPython, Windows-only, and fetched one GRIB file per grid point via NOMADS + `wgrib2`.
Its source was recovered by decompiling `sp.exe` and lives in [`recovered/`](recovered/) for
reference.

This rewrite keeps the original idea and workflow and modernises everything underneath.

## Decisions

| Topic | Choice | Why |
|---|---|---|
| Language / UI | C++20, Qt 6 (Widgets, plus a QML map view) | Native on Windows and Linux, small installers, Qt covers network/JSON/SQL/XML/settings/maps |
| Build | CMake + Ninja, GitHub Actions for Windows (MSVC) and Linux | One build definition for both platforms |
| Wind data | Open-Meteo GFS API (`/v1/gfs`, model `gfs_global`) | JSON, no GRIB decoder or `wgrib2`; many grid points per request; ground elevation included |
| Levels | 10 m wind + 28 pressure levels, 1000 → 10 hPa (≈ 30.5 km) | Verified live; 7 and 5 hPa return null. Above 10 hPa the top-level wind is held constant and flagged |
| Interpolation | Bilinear in lat/lon, linear in geometric altitude, linear in time | The original used nearest grid point, nearest level, nearest 3-h step |
| Storage | SQLite via Qt SQL, settings via INI (QSettings) | Same idea as v3; old `config.ini` locations can be imported |
| Map | Embedded OpenStreetMap (Qt Location) | Replaces "open in Google Earth" as the default view; KML export stays |

### Flight model

* **Ascent**: constant rate (as in v3).
* **Burst**: at a fixed altitude (as in v3).
* **Descent**, two models:
  * *Density-scaled* (default): `v(h) = v0 · (ρ0 / ρ(h))^k`, with ρ from the US Standard
    Atmosphere 1976. `k = 0.5` is classic terminal velocity; a least-squares fit to the 2019
    flight (RS41 P4740092, Jokioinen, 80 one-minute samples) gives `v0 = 11.9 m/s, k = 0.24`,
    RMS error 1.3 m/s from 27 km to the ground.
  * *Legacy (v3)*: `v(h) = rate + h · gain / 1000`, kept so old settings still mean the same thing.
    The same flight fits `rate = 9.9, gain = 0.63` (RMS 1.5 m/s), so the v3 idea was sound.
* Starts at the launch site's altitude, lands on the ground elevation under the track
  (v3 started at 0 m and landed at sea level).
* Geodesic distance and bearing on the WGS84 ellipsoid (v3 used a flat lat/lon bearing).

Defaults (from the 2019 flight): ascent 5.5 m/s, burst 28 000 m, descent v0 12 m/s, k 0.24.

### Fetching strategy

Calling Open-Meteo once per point (like v3 did with NOMADS) would burn the free quota, so
the predictor runs a *simulate → fetch → re-simulate* loop:

1. Fetch a small block of grid nodes around the launch site for the whole flight window.
2. Simulate. Where a node is missing, borrow the nearest one we have and record the gap.
3. Fetch every missing node (plus a one-cell ring) in one batched request, then simulate again.
4. Stop when a pass needs no new data (normally 2–4 passes).

Nodes are cached per (grid cell, hour), so batch runs for the same day reuse data.

**Quota.** The free, non-commercial Open-Meteo API allows 600 / 5 000 / 10 000 weighted
calls per minute / hour / day. One grid point with our 86 variables weighs ≈ 8.6 calls and a
typical prediction fetches 18–30 points, so ≈ 150–250 calls: roughly 40–60 uncached
predictions a day, and 3–4 per minute. The client waits and retries when the per-minute limit
is hit. A subscription (`customer-api.open-meteo.com` + API key) or a self-hosted Open-Meteo
server can be configured instead. Batch runs in phase 4 must share the cache and pace themselves.

## Phases

### Phase 0 — groundwork ✅
* CMake project, `.gitignore`, git repository.
* GitHub Actions: build and run the tests on `ubuntu-24.04` and `windows-2022`.
* Local toolchain (CMake, Ninja, Qt) under `.toolchain/` (not committed).

### Phase 1 — calculation core (no UI) ✅

Done: everything below, 8 Qt Test suites passing. Live check on 2026-10-22 against SondeHub's
Tawhiri predictor (same launch at Jokioinen, k = 0.5 to match its drag model): landings
0.5 / 1.4 / 1.9 km apart for launches 1 h, 13 h and 3 days ahead, on 55–90 km flights.
Open: the GitHub Actions workflow has not run yet (no remote); proxies that require
credentials on the very first request (rare) are not supported by Qt.

* `sp_core` static library (QtCore + QtNetwork):
  * `geo` — WGS84 small-step offsets, Vincenty inverse (distance, bearing).
  * `atmosphere` — US Standard Atmosphere 1976 density, geopotential ↔ geometric height.
  * `flightprofile` — ascent / burst / descent models.
  * `windfield` — per-node cache and 4D interpolation, reports missing nodes.
  * `openmeteo` — URL building, JSON parsing, async client.
  * `simulator` — time-stepped integration (midpoint method, 1 s step), burst and landing detection.
  * `predictor` — the simulate/fetch loop, async, reusable cache.
  * `trackerlog` — reads RS41Tracker CSV logs, compares a prediction to a real flight.
* `sp-cli` — command-line predictor: prints a summary, writes the track as CSV,
  `--compare` against a tracker log.
* Qt Test unit tests for each part, using synthetic wind fields and a recorded API response.
* Validation: the 2019 flight cannot be re-run on Open-Meteo (its GFS archive does not go
  back that far). Use newer tracker logs with `--compare`, or add an NCEI GFS archive
  reader later if needed.

### Phase 2 — storage and settings ✅
* SQLite schema for predictions and track points (successor of the v3 `tracks` / `waypoints` tables).
* Settings: locations (name, lat, lon, alt, warning distance), balloon profile, network proxy, units.
* One-time import of a v3 `config.ini` (locations, balloon settings, schedule).

Done: `settings` (INI via QSettings, `<config dir>/SondePredict/sondepredict.ini`),
`predictionstore` (SQLite, `<data dir>/SondePredict/predictions.sqlite`, versioned with
`PRAGMA user_version`, track points cascade-deleted), `v3import` (a small ConfigObj reader;
locations in v3 order, balloon values, grid size, proxy, Google Earth path, batch schedule,
last manual-run choices), and `network` (proxy: system / none / manual). `sp-cli` gained
`--import-v3`, `--locations`, `--save`, `--list`, `--config`, `--db`, and accepts saved
location names for `--launch` / `--home`. 11 test suites pass.

Import rules: v3 descent rate and gain are kept as the Legacy model and only made active if
they differ from v3's defaults (40 m/s, 1.0); locations whose name already exists are kept;
a schedule location missing from `[locations]` is added from the schedule's own coordinates.
Not imported: v3's `data.dat` (old predictions on old forecasts) and window geometry.
Units (km/mi) moved to phase 5.

### Phase 3 — desktop UI ✅
* Main window in the v3 layout: prediction tree by date on the left (bold when the landing is
  within the warning distance of "your location"), details on the right, Run button,
  launch site / your location / date / hour selectors.
* Embedded map: track (ascent and descent coloured differently), burst, landing, your location.
* Settings dialog, progress and status, background calculation.

Done: `sondepredict` (Qt Widgets) with the v3 layout: predictions tree grouped by launch day,
each entry showing the distance from your location (bold within that location's highlight
distance; v3 used the launch site's value), Delete, and the run controls (launch location,
your location, date, time in UTC or local, Start). The right side is an OpenStreetMap view
(QML `Map` in a `QQuickWidget`, OSM tiles fetched directly with an identifying user agent and
cached) above a details panel with clickable coordinates. Selecting several predictions or a
whole day draws them together. Status bar with progress, rate-limit notices and a UTC clock.
Settings dialog tabs: Locations, Balloon (with a live flight-time preview), Wind data & network,
General. File → Import Sonde Predict v3 settings. Window layout is remembered.
The windows live in an `sp_app` library so `tst_settingsdialog` can drive the dialog;
12 test suites pass. Google Earth, "Start a batch" and "Run scheduled batch tasks" are shown
disabled until phase 4.

### Phase 4 — batch runs and export ✅
* Scheduler: days × hours rules, run on demand or at an interval (v3 "Start a batch").
* KML export with the same per-point data as v3 (time, speed, heading, level, altitude),
  plus "open in Google Earth".
* Packaging: Windows installer (windeployqt + Inno Setup or NSIS), Linux AppImage.

Done:
* `batch` (core): `planBatch` turns a schedule rule (days × UTC hours at a launch minute)
  into launches; `BatchRunner` flies them in order through one wind cache and asks the first
  prediction of each 12-hour span to fetch the hours of the later ones too, so a day's batch
  downloads its winds once. Failed launches don't stop the batch.
* App: Settings → Batch tab (launch site, your location, days, hours, minute, repeat interval),
  "Start a batch", "Run scheduled batch tasks" (repeats while the app is open), Start turns
  into Stop while running (as in v3), batch results appear in the tree as they arrive.
* `trackexport` (core): KML with ascent/descent as `gx:Track` (3D, time slider, wind speed and
  direction per point, like v3) and launch/burst/landing/home placemarks; GPX 1.1 with
  landing/burst waypoints and the track for phone and GPS apps. Google Earth button (uses the
  configured program or the system's KML handler), File → Export as KML / GPX.
  `sp-cli` gained `--batch`, `--kml`, `--gpx` and `--export <ids>`.
* `log.txt` next to the prediction database (Help → Open log file), as v3 had.
* Packaging: app icon (`packaging/icons`), desktop entry and AppStream metadata, install rules,
  Windows version resource. `packaging/linux/build-appimage.sh` builds a self-contained
  AppImage with linuxdeploy (tested here: runs in a clean environment with map tiles, SQLite
  and HTTPS). `packaging/windows/sondepredict.iss` + `.github/workflows/release.yml` build the
  Windows installer and zip with windeployqt and Inno Setup, and the AppImage, on version tags.
  The Windows build has not been run yet (no Windows machine here, no GitHub remote).
* 14 test suites pass.

### Phase 5 — optional extras (landing area ✅)
* ~~Uncertainty: GFS ensemble (GEFS via Open-Meteo ensemble API) → landing-area ellipse.~~
  Done differently, see below.
* Wind above 10 hPa for very high bursts.
* Dark mode, units (km/mi), simple update check against GitHub releases.

Done: **landing area**. The GFS ensemble would cost roughly half of the free daily Open-Meteo
quota per prediction (31 members × 86 variables per grid point), so instead `landingarea` re-flies
each prediction 50 times through the already downloaded winds with the balloon varied
(1σ: ascent ±0.4 m/s, burst ±1500 m, descent ±10 %; adjustable in Settings → Balloon) and fits
a 2-D normal distribution to the landings. The 95 % ellipse is drawn on the map (amber), listed
in the details ("17.0 × 0.6 km, long axis 162°–342°"), exported to KML and printed by `sp-cli`
(`--no-area` skips it). Burst altitude, the biggest single source of landing scatter, is
covered; forecast wind errors are not, which the app says next to the figure. Any extra grid
points the varied flights need are fetched once. Results are deterministic (fixed seed) and
stored with the prediction (database schema 2; version-1 databases are upgraded on open).
A run of 50 varied flights takes about 0.3 s in a debug build.

Also: the map only zooms out to include "your location" when it is within 300 km of the landing.

Not done (judged not worth it now): wind above 10 hPa (Open-Meteo has no GFS data above it;
the top wind is held and flagged), km/mi units, an update check (no public repository yet).
Dark mode follows the system palette already, except for the map tiles.

### Validation against real soundings and MEPS ✅

37 FMI Jokioinen soundings (October 2026) were compared with the forecast winds of the global
models and of MET Norway's MEPS (see `tools/validation/README.md`). Outcome:

* Balloon defaults: Jokioinen's SondeHub statistics (≈1 000 flights: 5.1 ± 0.4 m/s, burst
  29 700 ± 1 850 m, descent 12.5 m/s ± 18 %), which the soundings confirm. Settings → Balloon
  offers the statistics of ~320 launch sites (bundled snapshot, `tools/sondehub_sites.py`).
* Default global model: ECMWF IFS (best a day or more ahead), instead of GFS.
* MEPS (2.5 km, 65 model levels, hourly to 66 h, every 3 h) read directly from MET Norway's
  THREDDS server over OPeNDAP (binary DAP2, own parser), on its Lambert grid (every 2nd point),
  winds rotated to east/north, level heights from the hypsometric equation. `WindField` got a
  grid geometry so it can hold that grid.
* Inside the MEPS area the predictor averages MEPS and ECMWF (`BlendedWind`): better than either
  at every lead time. Fallback order: MEPS + global → MEPS alone (when Open-Meteo's quota is
  used up) → global alone; a source that failed is not tried again for the same prediction.

## User manual ✅

Rewritten for Sonde Predict 5 (the v0.2–v0.6 manuals of v3 are in `recovered/docs`):
[docs/manual/user-manual.md](docs/manual/user-manual.md) with screenshots of the real app,
following the old manual's structure (description, settings, predictions, batch, Google Earth,
troubleshooting, installation) and adding the landing area, GPX, the command line, wind data
limits, where files are kept and what changed since v3. It is built into the app
(Help → User manual, F1, with working contents links, Back and Save as PDF) and committed as an
11-page A4 PDF generated by `sondepredict --manual-pdf`. `tst_helpwindow` checks that every
contents link has a heading, every picture loads and the PDF export works. 16 test suites pass.

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=".toolchain/Qt/6.11.3/gcc_64;.toolchain/sysroot/usr"
cmake --build build
ctest --test-dir build --output-on-failure
```

`.toolchain/sysroot` holds the OpenGL development headers (unpacked from Debian's `libgl-dev`,
`libglx-dev`, `libopengl-dev`, `libegl-dev`) that Qt GUI needs; with those packages installed
system-wide it can be left out.
