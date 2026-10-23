"""Real flights from SondeHub's public history (https://sondehub.org): what the amateur
receivers heard of each radiosonde launched from a site, from launch to (nearly) the ground.

    python3 sondehub.py <site, e.g. 02963> <first day YYYY-MM-DD> <last day> [--tracks N]

Lists the flights (launch, burst, last position heard) and, with --tracks, downloads the full
telemetry of up to N of them for descent and ascent analysis. Everything is cached like the
other validation downloads."""
import sys, os, json, gzip, math, re, http.client, datetime as dt, urllib.request
sys.path.insert(0, os.path.dirname(__file__))
import models as M

S3 = 'https://sondehub-history.s3.amazonaws.com'

def _get(url, binary=False):
    req = urllib.request.Request(url, headers={'User-Agent': 'SondePredict-validation'})
    for attempt in range(4):  # large tracks sometimes break off
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                data = r.read()
            return data if binary else data.decode()
        except (http.client.IncompleteRead, ConnectionError, TimeoutError):
            if attempt == 3: raise

def _cached(name, fetch):
    path = os.path.join(M.CACHE, 'sondehub', name)
    if os.path.exists(path):
        return open(path, 'rb').read()
    data = fetch()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)
    return data

def t(p):
    return dt.datetime.fromisoformat(p['datetime'].replace('Z', '+00:00')).timestamp()

def day_keys(site, day):
    prefix = f'launchsites/{site}/{day:%Y/%m/%d}/'
    xml = _cached(f'list_{site}_{day:%Y%m%d}.xml', lambda: _get(f'{S3}/?prefix={prefix}', True)).decode()
    return re.findall(r'<Key>([^<]+)</Key>', xml)

def flights(site, first, last, site_pos):
    """[{serial, launch (unix), first, burst, last}] for flights that started at the site."""
    out = []
    day = first
    while day <= last:
        for key in day_keys(site, day):
            serial = key.rsplit('/', 1)[-1][:-5]
            pts = json.loads(_cached(f'summary_{serial}_{day:%Y%m%d}.json', lambda k=key: _get(f'{S3}/{k}', True)))
            if len(pts) < 3:
                continue
            a, b, c = pts[0], max(pts, key=lambda p: p['alt']), pts[-1]
            # Heard from near the ground at the site (not a sonde drifting in from elsewhere).
            if a['alt'] > 2500 or dist(a, site_pos) > 15000:
                continue
            out.append(dict(serial=serial, first=a, burst=b, last=c, type=a.get('type', '?')))
        day += dt.timedelta(days=1)
    # The same flight can appear on two days (launch before midnight).
    seen = {}
    for f in out:
        seen.setdefault(f['serial'], f)
    return sorted(seen.values(), key=lambda f: t(f['first']))

def dist(p, q):
    """Metres between two points ({'lat','lon'} or (lat, lon)), spherical."""
    la1, lo1 = (p['lat'], p['lon']) if isinstance(p, dict) else p
    la2, lo2 = (q['lat'], q['lon']) if isinstance(q, dict) else q
    f1, f2 = math.radians(la1), math.radians(la2)
    a = math.sin((f2 - f1) / 2)**2 + math.cos(f1) * math.cos(f2) * math.sin(math.radians(lo2 - lo1) / 2)**2
    return 2 * 6371000 * math.asin(math.sqrt(a))

def track(serial):
    """Full telemetry, one point per second (duplicates from several receivers merged), by time."""
    raw = _cached(f'track_{serial}.json.gz', lambda: _get(f'{S3}/serial/{serial}.json.gz', True))
    pts = json.loads(gzip.decompress(raw))
    by_second = {}
    for p in pts:
        if 'alt' not in p or 'lat' not in p:
            continue
        by_second.setdefault(int(t(p)), p)
    return [dict(t=s, alt=p['alt'], lat=p['lat'], lon=p['lon'], vv=p.get('vel_v')) for s, p in sorted(by_second.items())]

if __name__ == '__main__':
    site = sys.argv[1]
    first = dt.date.fromisoformat(sys.argv[2]); last = dt.date.fromisoformat(sys.argv[3])
    sites = {s['id']: s for s in json.load(open(os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'app', 'data', 'launch-sites.json')))}
    pos = (sites[site]['lat'], sites[site]['lon'])
    fl = flights(site, first, last, pos)
    print(len(fl), 'flights')
    for f in fl:
        b, c = f['burst'], f['last']
        print(f"{f['serial']:10s} {f['first']['datetime'][:16]}  burst {b['alt']/1000:5.1f} km  last {c['alt']:6.0f} m "
              f"{dist(pos, c)/1000:5.1f} km from site")
    if '--tracks' in sys.argv:
        n = int(sys.argv[sys.argv.index('--tracks') + 1])
        for f in fl[:n]:
            print(f['serial'], len(track(f['serial'])), 'points')
