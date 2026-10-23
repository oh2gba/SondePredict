"""Refresh the launch-site statistics bundled with Sonde Predict from SondeHub.

    python3 tools/sondehub_sites.py

SondeHub (https://sondehub.org) collects radiosonde telemetry received by amateurs and keeps,
per launch site, the average ascent rate, burst altitude and descent rate (at sea level) of the
flights it has seen, with their spread. Only sites with all three averages are kept."""
import gzip, json, os, urllib.request

URL = 'https://api.v2.sondehub.org/sites'
# Density-scaled descents (sea-level speed v0, exponent k) fitted to recent SondeHub tracks by
# tools/validation/fit_descents.py; they replace SondeHub's long-term descent average, which can
# be far off (a station that started using a parachute).
FITS = os.path.join(os.path.dirname(__file__), 'descent-fits.json')
# Where whole predictions were checked against real landings (tools/validation/stations.py,
# 8–22 Oct 2026), the values that matched the landings best win over the fall-time fit:
# Jokioinen its fit to 30 flights, Stavanger SondeHub's own average.
DESCENT_OVERRIDES = {'02963': {'v0': 11.7, 'k': 0.33, 'tracks': 30, 'until': '2026-10-22'},
                     '01415': {'v0': 14.6, 'k': 0.33, 'tracks': 16, 'until': '2026-10-22'}}
OUT = os.path.join(os.path.dirname(__file__), '..', 'src', 'app', 'data', 'launch-sites.json')

def main():
    req = urllib.request.Request(URL, headers={'User-Agent': 'SondePredict-tools', 'Accept-Encoding': 'gzip'})
    with urllib.request.urlopen(req, timeout=60) as r:
        body = r.read()
        if r.headers.get('Content-Encoding') == 'gzip' or body[:2] == b'\x1f\x8b':
            body = gzip.decompress(body)
    sites = json.loads(body)
    fits = json.load(open(FITS)) if os.path.exists(FITS) else {}
    fits.update(DESCENT_OVERRIDES)
    out = []
    for s in sites.values():
        if not all(k in s for k in ('ascent_rate', 'burst_altitude', 'descent_rate', 'position')):
            continue
        e = {'id': s['station'], 'name': s['station_name'], 'lat': round(s['position'][1], 4),
             'lon': round(s['position'][0], 4), 'ascent': s['ascent_rate'], 'burst': s['burst_altitude'],
             'descent': s['descent_rate']}
        for k, short in (('ascent_std', 'ascentSd'), ('burst_std', 'burstSd'), ('descent_std', 'descentSd'),
                         ('burst_samples', 'flights')):
            if k in s:
                e[short] = s[k]
        # Synoptic launch times, "D:HH:MM" (day of week, 0 = every day) → "HH:MM".
        times = sorted({':'.join(t.split(':')[1:3]) for t in s.get('times', []) if t.count(':') == 2})
        if times:
            e['times'] = times
        if s['station'] in fits:
            f = fits[s['station']]
            e['descent'], e['descentK'] = f['v0'], f['k']
            e['descentTracks'], e['descentUntil'] = f['tracks'], f['until']
        out.append(e)
    out.sort(key=lambda e: e['name'])
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w') as f:
        json.dump(out, f, ensure_ascii=False, separators=(',', ':'))
        f.write('\n')
    print(len(out), 'sites written to', os.path.normpath(OUT))

if __name__ == '__main__':
    main()
