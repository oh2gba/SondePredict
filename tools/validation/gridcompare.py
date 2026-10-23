"""Like the app: model winds on a 0.25° grid, interpolated bilinearly at the sonde's (dead-reckoned)
position, linearly in height and time. Compares predicted ascent drift with the measured one."""
import sys, os, math, json, statistics as st, urllib.request, urllib.parse
sys.path.insert(0, os.path.dirname(__file__))
import soundings as S, models as M

STEP = 0.25
def grid_fetch(model, lat0, lon0, nlat, nlon, start, end, levels=None):
    """Fetch all nodes (in chunks of 50 locations) and return {(i, j): Column}."""
    i0 = round(lat0 / STEP) - nlat; j0 = round(lon0 / STEP) - nlon
    nodes = [(i, j) for i in range(i0, i0 + 2 * nlat + 1) for j in range(j0, j0 + 2 * nlon + 1)]
    cols = {}
    for k in range(0, len(nodes), 50):
        chunk = nodes[k:k + 50]
        key = f'grid_{model}_{chunk[0][0]}_{chunk[0][1]}_{len(chunk)}_{start}_{end}.json'.replace(':', '')
        path = os.path.join(M.CACHE, key)
        if not os.path.exists(path):
            var = ['wind_speed_10m', 'wind_direction_10m']
            for l in M.LEVELS:
                var += [f'wind_speed_{l}hPa', f'wind_direction_{l}hPa', f'geopotential_height_{l}hPa']
            q = dict(latitude=','.join(f'{i*STEP:.4f}' for i, j in chunk), longitude=','.join(f'{j*STEP:.4f}' for i, j in chunk),
                     models=model, hourly=','.join(var), wind_speed_unit='ms', timeformat='unixtime', timezone='GMT',
                     start_hour=start, end_hour=end)
            url = 'https://api.open-meteo.com/v1/forecast?' + urllib.parse.urlencode(q)
            with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'SondePredict-validation'}), timeout=120) as r:
                d = json.load(r)
            if isinstance(d, dict) and d.get('error'): raise RuntimeError(d.get('reason'))
            os.makedirs(M.CACHE, exist_ok=True); json.dump(d, open(path, 'w'))
        d = json.load(open(path))
        for node, loc in zip(chunk, d if isinstance(d, list) else [d]):
            cols[node] = M.Column(loc)
    return cols

def wind_at(cols, lat, lon, t, z):
    y, x = lat / STEP, lon / STEP; i, j = math.floor(y), math.floor(x); fy, fx = y - i, x - j
    u = v = 0.0
    for di, wy in ((0, 1 - fy), (1, fy)):
        for dj, wx in ((0, 1 - fx), (1, fx)):
            c = cols.get((i + di, j + dj))
            w = c.wind(t, z) if c else None
            if w is None: return None
            u += wy * wx * w[0]; v += wy * wx * w[1]
    return u, v

def positions(s):
    """Dead-reckoned sonde position at each level from the measured winds."""
    lat, lon = s['lat'], s['lon']; out = [(lat, lon)]
    for a, b in zip(s['levels'], s['levels'][1:]):
        dt = b['t'] - a['t']
        de = 0.5 * (a['u'] + b['u']) * dt; dn = 0.5 * (a['v'] + b['v']) * dt
        lat += dn / 111200.0; lon += de / (111200.0 * math.cos(math.radians(lat)))
        out.append((lat, lon))
    return out

def evaluate(model, snd, cols, zcuts=(10000, 15000, 20000)):
    """Drift error: integrate the model wind along the *model's own* path vs the measured path."""
    res = {z: [] for z in zcuts}; vec = []
    for s in snd:
        pos = positions(s)
        for L, (la, lo) in zip(s['levels'], pos):
            w = wind_at(cols, la, lo, L['t'], L['z'])
            if w: vec.append((w[0] - L['u'])**2 + (w[1] - L['v'])**2)
        for zc in zcuts:
            if s['ztop'] < zc: continue
            # model trajectory: own winds, own path
            lat, lon = s['lat'], s['lon']; ok = True
            for a, b in zip(s['levels'], s['levels'][1:]):
                if b['z'] > zc: break
                w = wind_at(cols, lat, lon, a['t'], a['z'])
                if w is None: ok = False; break
                dt = b['t'] - a['t']
                lat += w[1] * dt / 111200.0; lon += w[0] * dt / (111200.0 * math.cos(math.radians(lat)))
            if not ok: continue
            te, tn = S.drift(s['levels'], lambda L: (L['u'], L['v']), zc)
            tla = s['lat'] + tn / 111200.0; tlo = s['lon'] + te / (111200.0 * math.cos(math.radians(tla)))
            res[zc].append(math.hypot((lat - tla) * 111200.0, (lon - tlo) * 111200.0 * math.cos(math.radians(lat))) / 1000)
    return math.sqrt(st.mean(vec)) if vec else float('nan'), res

if __name__ == '__main__':
    snd = S.load(sys.argv[1])
    for model in sys.argv[2].split(','):
        try:
            cols = grid_fetch(model, snd[0]['lat'], snd[0]['lon'], 3, 5, '2026-10-10T23:00', '2026-10-22T16:00')
        except Exception as e:
            print(model, 'ERROR', e); continue
        rms, res = evaluate(model, snd, cols)
        print('%-26s vector RMS %.2f m/s |' % (model, rms), ' '.join('%dkm: mean %.1f median %.1f max %.1f (n=%d)' % (z/1000, st.mean(v), st.median(v), max(v), len(v)) for z, v in res.items() if v))
