# Validating the wind data against real soundings

These scripts compare forecast winds with the winds measured by radiosondes, to choose the wind
sources and defaults of Sonde Predict. They are not part of the app.

## Data

37 soundings from FMI's Jokioinen station (60.81 N, 23.50 E), 11–22 October 2026, as
`*.json.gz` files (pressure, height, wind per level, launch and end times; `soundings.py`
reads them). The sonde's position is not in the files; drift is reconstructed by integrating the
measured wind over time (dead reckoning), which is also how the model drift is computed, so the
two compare like for like.

## Scripts

All downloads are cached (`SP_VALIDATION_CACHE`, default `~/.cache/SondePredict/validation`),
so re-runs cost no quota. Run them with `python3 -I script.py <soundings folder> ...`.

| Script | What it compares |
|---|---|
| `compare.py <models>` | Open-Meteo models at launch time (their stitched "best" series) |
| `gridcompare.py` | Like the app: a 0.25° grid interpolated along the sonde's path |
| `forecast.py <models> <lead>` | The run issued `lead` hours before the nominal time (Open-Meteo single-runs API) |
| `meps_compare.py <lead> [--flip]` | MEPS from MET Norway's THREDDS server: model levels vs standard levels; `--flip` checks the wind rotation |
| `issue.py <leads> [models]` | What a chaser would have had: the newest run *published* that many hours before launch |
| `blend.py <lead> <a+b,...>` | Averages of several models |

Measures: RMS of the vector wind error over all measured levels (m/s), by height band, and the
error in the dead-reckoned position at 10 and 15 km altitude (km).

## Results

**At launch time** (`compare.py`, `meps_compare.py 0`), RMS m/s / position error at 10 km:
ARPEGE 2.22 / 0.7 km, UKMO 2.35, MEPS 2.54 / 1.4 km, GEM 2.54, ECMWF 2.62 / 1.0 km, ICON 2.63
(only to 24 km), GFS 2.68 / 1.4 km.

**Forecasts issued 24 h before the nominal time** (`forecast.py`, `meps_compare.py 24`):
ECMWF 3.08 / 1.7 km, GEM 3.49, MEPS 3.51 / 2.5 km, UKMO 3.53, GFS 3.56 / 2.9 km, ARPEGE 3.60.
At 48 h: ECMWF 3.98 / 3.5 km, GFS 4.68 / 5.3 km.

**What was available when the prediction is made** (`issue.py`; runs count once published:
MEPS 3 h after run time, GFS 5 h, ECMWF 8 h), RMS m/s with the position error at 10 km:

| Made before launch | MEPS | ECMWF | GFS | MEPS + ECMWF averaged |
|---|---|---|---|---|
| 3 h | 2.66 / 1.5 | 2.79 / 1.4 | 3.20 / 2.3 | **2.52 / 1.1** |
| 6 h | 2.74 / 1.3 | 2.96 / 1.6 | 3.20 / 2.3 | **2.63 / 1.3** |
| 12 h | 3.02 / 1.6 | 3.07 / 1.7 | 3.41 / 2.9 | **2.84 / 1.5** |
| 24 h | 3.59 / 3.2 | 3.57 / 3.0 | 3.82 / 3.5 | **3.37 / 3.0** |
| 48 h | 4.75 / 4.7 | 4.57 / 4.4 | 5.78 / 7.7 | **4.48 / 4.4** |

Findings that shaped the app:

* **ECMWF is the best global model** a day or more ahead; it is the default global model.
* **MEPS is as good as the best global models** and slightly better a few hours ahead (it runs
  every 3 hours and is published sooner). Its weak spot is above 20 km (4.4 vs 3.6 m/s), near its
  model top.
* **Averaging MEPS and ECMWF beats both at every lead time**, by 2–6 % in wind and up to 20 % in
  position a few hours ahead. Averaging two global models gains almost nothing (ECMWF + GEM +
  UKMO: 3.05 vs 3.08), because their errors are alike; MEPS's are different. Using MEPS below a
  height and ECMWF above it gains nothing either. So inside the MEPS area the app averages them.
* **MEPS model levels vs standard pressure levels**: practically the same here (2.54 vs 2.58 at
  launch time, 3.51 vs 3.48 at 24 h), a little better at 10–20 km. The app uses the model levels,
  which cost nothing extra.
* **Wind rotation check**: MEPS winds are along its Lambert grid; with the rotation to east/north
  flipped, the error nearly doubles (4.55 vs 2.54 m/s), confirming the sign.
* **Vertical interpolation** in height or in log-pressure makes no difference.
* **Ascent rate**: complete flights climbed at 4.7–5.4 m/s (4.9 on average); 5.5 m/s put the
  burst point 2.5–4.7 km off, 4.8 m/s mostly within 1 km.
* **Burst**: the end time of each sounding gives its top even when the profile stops early
  (checked against the complete flights: within 0.3 km in 6 of 7). 25 flights reached 27–37 km
  (median 32.5); 12, mostly at 00 UTC, ended at 16–18 km (100 hPa). SondeHub's record of about
  1 000 Jokioinen flights (burst 29.7 ± 1.85 km, ascent 5.1 ± 0.41 m/s, descent 12.5 m/s at sea
  level) shows no such early bursts, so those are most likely recordings stopped at 100 hPa. The
  app's defaults are SondeHub's Jokioinen values.

**Whole predictions against real landings** (`landings.py`, 45 Jokioinen flights 8–22 October
2026 from SondeHub, MEPS winds from the run before each launch; errors in km, median / mean / p90):

| Balloon settings | Burst point | Landing point |
|---|---|---|
| Earlier defaults (5.1 m/s, 29 700 m, descent 12.5 m/s k 0.24, constant ascent) | 12.3 / 14.1 / 24.8 | 13.7 / 15.4 / 27.1 |
| New defaults (30-day medians 4.97 m/s, 34 241 m; descent 11.7 m/s k 0.33; shaped ascent) | 6.7 / 8.5 / 17.6 | **8.8 / 9.4 / 17.2** |
| Each flight's real ascent and burst, constant ascent | 4.9 / 5.7 / 11.3 | 7.6 / 8.0 / 14.9 |
| Each flight's real ascent and burst, shaped ascent | 4.4 / 4.6 / 7.8 | 6.5 / 7.3 / 12.3 |

The new defaults cut the landing error by about 40 %. Knowing each flight's real balloon would
save another 2 km; the rest is wind and descent. The measured ascent shape (`tracks.py`: 13 %
slower than average at 9–18 km, up to 20 % faster above 25 km) helps clearly, so it is on.

Real descents (`tracks.py`, 30 flights): fitting the time to fall from 25 to 10 km and from 10 to
2 km gives v0 11.7 m/s and k 0.33; the earlier 12 m/s and k 0.24 fell 107 s too slowly above
10 km. Flight-to-flight spread about ± 18 %.

**Other stations** (`stations.py`, 8–22 October 2026, MEPS winds from the run before launch;
median landing error, km). *App*: the 30-day medians of ascent and burst and SondeHub's
long-term descent; *fitted*: the same with the descent fitted to the station's own tracks;
*real*: each flight's own ascent and burst.

| Station | Flights | App | Fitted descent | Real balloon |
|---|---|---|---|---|
| Jokioinen | 46 | 9.3 | 9.6 | 7.0 |
| Tallinn | 12 | 8.7 | **5.0** | 5.5 |
| Stavanger | 16 | 6.2 | 8.0 | 3.2 |
| Greifswald (parachute) | 18 | 22.4 | 22.2 | 20.7 |

**SondeHub's descent averages can be far off.** `fit_descents.py` fitted the descent of 22
stations around the Baltic and the North Sea to their recent tracks: Tallinn falls at 7.2 m/s, not
the 13.7 listed (a parachute); Skriveri 7.7 (12.9 listed), Kaunas 9.7 (13.4), Wroclaw 8.7 (11.5).
Parachute stations need a larger exponent (k 0.40–0.54) than free-falling sondes (0.32–0.38).
The app ships these fits (`tools/descent-fits.json`, merged by `tools/sondehub_sites.py`); for
other stations it takes k 0.45 below 7 m/s and 0.33 above. Where whole landings were checked,
the values that matched them best win (Jokioinen 11.7 m/s, Stavanger 14.6 m/s).

**The landing area** (`stations.py --sigmas`): varying only the balloon, the 95 % area held only
28–83 % of the real landings. With a steady wind error per varied flight it holds them:

| Inside the 95 % area | 0 | 0.5 m/s | 1 m/s | 1.5 m/s |
|---|---|---|---|---|
| Jokioinen | 43 % | 93 % | 100 % | 100 % |
| Tallinn | 83 % | 92 % | 100 % | 100 % |
| Stavanger | 56 % | 100 % | 100 % | 100 % |
| Greifswald (parachute, ~4 m/s) | 28 % | 56 % | 83 % | 100 % |

The app uses 0.5 m/s for forecasts a few hours old, growing with lead time as the forecast
errors do (`issue.py`), and scaled up for slow descents by (11.7 / v0)^0.8 (Greifswald: about
1.2 m/s), since parachutes spend an hour or more in the low-level winds.

**MEPS + ECMWF on real landings** (`stations.py --compare-average`, 25 Jokioinen flights
15–22 October 2026, fitted balloon values, winds as available at launch): MEPS alone missed by
10.0 km (median; mean 10.6, p90 18.9), MEPS + ECMWF averaged by 8.5 km (mean 10.2, p90 21.2).
The average is better on the typical flight, as on the soundings; a few flights in both runs fell
back to ECMWF alone when MET Norway's server was busy.

Caveats: 37 soundings from one station over twelve days of autumn weather. The differences
between the models are small compared with their spread from day to day, so treat the order of
close models as uncertain; the gain from averaging was consistent at every lead time.

## MEPS access notes

`meps.py` reads MEPS over OPeNDAP (ASCII here, binary `.dods` in the app). The server takes about
a second per variable and forecast hour whatever the area, and answers 429 when one address sends
too many requests at once: keep downloads to two or three in parallel.
