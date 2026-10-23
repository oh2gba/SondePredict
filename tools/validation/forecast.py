"""Forecast skill: for each sounding use the model run issued `lead` hours before the nominal
time (latest run of that model at or before it) and compare its winds with the measured ones."""
import sys, os, json, math, time, statistics as st, urllib.request, urllib.parse, datetime as dt
sys.path.insert(0, os.path.dirname(__file__))
import soundings as S, models as M

CYCLE = {'gfs_global': 6, 'ecmwf_ifs025': 6, 'meteofrance_arpege_world': 6, 'ukmo_global_deterministic_10km': 6,
         'gem_global': 12, 'icon_global': 6, 'ecmwf_aifs025_single': 6}

def fetch_run(model, run, lat, lon, hours):
    key = f'run_{model}_{lat:.4f}_{lon:.4f}_{run:%Y%m%d%H}_{hours}.json'
    path = os.path.join(M.CACHE, key)
    if os.path.exists(path):
        d = json.load(open(path))
        return None if d.get('missing') else d
    var = ['wind_speed_10m', 'wind_direction_10m']
    for l in M.LEVELS:
        var += [f'wind_speed_{l}hPa', f'wind_direction_{l}hPa', f'geopotential_height_{l}hPa']
    q = dict(latitude=f'{lat:.4f}', longitude=f'{lon:.4f}', models=model, run=f'{run:%Y-%m-%dT%H:00}',
             hourly=','.join(var), forecast_hours=hours, wind_speed_unit='ms', timeformat='unixtime', timezone='GMT')
    url = 'https://single-runs-api.open-meteo.com/v1/forecast?' + urllib.parse.urlencode(q)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'SondePredict-validation'}), timeout=60) as r:
            d = json.load(r)
    except urllib.error.HTTPError as e:
        d = json.loads(e.read() or b'{}')
        reason = d.get('reason', str(e))
        if 'limit' in reason.lower(): raise RuntimeError(reason)
        json.dump({'missing': reason}, open(path, 'w')); return None
    os.makedirs(M.CACHE, exist_ok=True); json.dump(d, open(path, 'w'))
    time.sleep(1.0)  # pace: stays far below 600 calls/min
    return d

def run_for(model, nominal, lead):
    t = dt.datetime.fromtimestamp(nominal - lead * 3600, dt.UTC)
    c = CYCLE.get(model, 6)
    return t.replace(hour=t.hour // c * c, minute=0, second=0, microsecond=0)

BANDS = [(0, 3000), (3000, 10000), (10000, 20000), (20000, 40000)]
if __name__ == '__main__':
    snd = S.load(sys.argv[1]); lead = int(sys.argv[3])
    print(f'lead {lead} h | RMS vector error by height (m/s) | drift error at 10 / 15 km: mean (max) km')
    for model in sys.argv[2].split(','):
        err = {b: [] for b in BANDS}; allerr = []; d10 = []; d15 = []; used = 0
        for s in snd:
            run = run_for(model, s['nominal'], lead)
            hours = int((s['nominal'] - run.timestamp()) / 3600) + 4
            try:
                d = fetch_run(model, run, s['lat'], s['lon'], hours)
            except RuntimeError as e:
                print(model, 'STOP:', e); sys.exit(1)
            if not d: continue
            col = M.Column(d); used += 1
            for L in s['levels']:
                w = col.wind(L['t'], L['z'])
                if w is None or L['z'] > col.top + 500: continue
                e2 = (w[0] - L['u'])**2 + (w[1] - L['v'])**2; allerr.append(e2)
                for b in BANDS:
                    if b[0] <= L['z'] - s['z0'] < b[1]: err[b].append(e2)
            for zc, out in ((10000, d10), (15000, d15)):
                if s['ztop'] < zc or col.top < zc - 1000: continue
                te, tn = S.drift(s['levels'], lambda L: (L['u'], L['v']), zc)
                me, mn = S.drift(s['levels'], lambda L: col.wind(L['t'], L['z']) or (0, 0), zc)
                out.append(math.hypot(me - te, mn - tn) / 1000)
        rms = lambda xs: math.sqrt(st.mean(xs)) if xs else float('nan')
        fmt = lambda xs: ('%4.1f (%4.1f)' % (st.mean(xs), max(xs))) if xs else '  -  '
        print('%-32s runs %2d | %s | all %.2f | %s  %s' % (model, used, ' '.join('%5.2f' % rms(err[b]) for b in BANDS), rms(allerr), fmt(d10), fmt(d15)))
        sys.stdout.flush()
