"""Fetch (and cache) model winds at one point from Open-Meteo; interpolate like the app does."""
import json, os, math, urllib.request, urllib.parse, time

LEVELS = [1000, 975, 950, 925, 900, 850, 800, 750, 700, 650, 600, 550, 500, 450, 400, 350, 300,
          250, 200, 150, 100, 70, 50, 40, 30, 20, 15, 10]
R0 = 6356766.0
# Downloads are cached here so a re-run costs no Open-Meteo quota.
CACHE = os.environ.get('SP_VALIDATION_CACHE', os.path.join(os.path.expanduser('~'), '.cache', 'SondePredict', 'validation'))

def geometric(h):
    return R0 * h / (R0 - h)

def fetch(model, lat, lon, start, end):
    os.makedirs(CACHE, exist_ok=True)
    key = f'{model}_{lat:.4f}_{lon:.4f}_{start}_{end}.json'.replace(':', '')
    path = os.path.join(CACHE, key)
    if os.path.exists(path):
        return json.load(open(path))
    var = ['wind_speed_10m', 'wind_direction_10m']
    for l in LEVELS:
        var += [f'wind_speed_{l}hPa', f'wind_direction_{l}hPa', f'geopotential_height_{l}hPa']
    q = dict(latitude=f'{lat:.4f}', longitude=f'{lon:.4f}', models=model, hourly=','.join(var),
             wind_speed_unit='ms', timeformat='unixtime', timezone='GMT', start_hour=start, end_hour=end)
    url = 'https://api.open-meteo.com/v1/forecast?' + urllib.parse.urlencode(q)
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'SondePredict-validation'}), timeout=60) as r:
        d = json.load(r)
    if d.get('error'):
        raise RuntimeError(d.get('reason'))
    json.dump(d, open(path, 'w'))
    return d

class Column:
    """Hourly profiles at one point: profiles[hour_unix] = sorted [(z, u, v)]."""
    def __init__(self, d):
        h = d['hourly']; elev = d['elevation']; self.profiles = {}
        self.top = 0
        for i, t in enumerate(h['time']):
            prof = []
            s, dd = h['wind_speed_10m'][i], h['wind_direction_10m'][i]
            if s is not None and dd is not None:
                prof.append((elev + 10, *uv(s, dd)))
            for l in LEVELS:
                s, dd, z = (h[f'wind_speed_{l}hPa'][i], h[f'wind_direction_{l}hPa'][i], h[f'geopotential_height_{l}hPa'][i])
                if None in (s, dd, z): continue
                zg = geometric(z)
                if zg <= elev + 10: continue
                prof.append((zg, *uv(s, dd)))
            prof.sort()
            if prof:
                self.profiles[int(t)] = prof
                self.top = max(self.top, prof[-1][0])

    def wind(self, t, z):
        h0 = int(t // 3600 * 3600); f = (t - h0) / 3600
        a, b = self.profiles.get(h0), self.profiles.get(h0 + 3600)
        if a is None or b is None: return None
        ua, va = interp(a, z); ub, vb = interp(b, z)
        return (ua + f * (ub - ua), va + f * (vb - va))

def uv(speed, direction):
    return (-speed * math.sin(math.radians(direction)), -speed * math.cos(math.radians(direction)))

def interp(prof, z):
    if z <= prof[0][0]: return prof[0][1:]
    if z >= prof[-1][0]: return prof[-1][1:]
    for (z0, u0, v0), (z1, u1, v1) in zip(prof, prof[1:]):
        if z0 <= z <= z1:
            f = (z - z0) / (z1 - z0)
            return (u0 + f * (u1 - u0), v0 + f * (v1 - v0))
