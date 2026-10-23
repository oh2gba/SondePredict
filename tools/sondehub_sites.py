"""Refresh the launch-site statistics bundled with Sonde Predict from SondeHub.

    python3 tools/sondehub_sites.py

SondeHub (https://sondehub.org) collects radiosonde telemetry received by amateurs and keeps,
per launch site, the average ascent rate, burst altitude and descent rate (at sea level) of the
flights it has seen, with their spread. Only sites with all three averages are kept."""
import gzip, json, os, urllib.request

URL = 'https://api.v2.sondehub.org/sites'
# Density-scaled descent (sea-level speed, exponent k) fitted to real descents with
# tools/validation/tracks.py; replaces SondeHub's sea-level average for these sites.
DESCENT_FITS = {'02963': (11.7, 0.33)}  # Jokioinen: 30 flights, Sep–Oct 2026
OUT = os.path.join(os.path.dirname(__file__), '..', 'src', 'app', 'data', 'launch-sites.json')

def main():
    req = urllib.request.Request(URL, headers={'User-Agent': 'SondePredict-tools', 'Accept-Encoding': 'gzip'})
    with urllib.request.urlopen(req, timeout=60) as r:
        body = r.read()
        if r.headers.get('Content-Encoding') == 'gzip' or body[:2] == b'\x1f\x8b':
            body = gzip.decompress(body)
    sites = json.loads(body)
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
        if s['station'] in DESCENT_FITS:
            e['descent'], e['descentK'] = DESCENT_FITS[s['station']]
        out.append(e)
    out.sort(key=lambda e: e['name'])
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w') as f:
        json.dump(out, f, ensure_ascii=False, separators=(',', ':'))
        f.write('\n')
    print(len(out), 'sites written to', os.path.normpath(OUT))

if __name__ == '__main__':
    main()
