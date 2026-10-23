"""What a chaser would actually have had: predictions made `L` hours before launch use the
newest run of each model that was published by then (run time + publication delay ≤ issue time).

    python3 issue.py <soundings folder> <leads, e.g. 3,6,12,24,48> [models]

models: comma-separated Open-Meteo ids and/or 'meps' (default ecmwf_ifs025,gfs_global,meps)."""
import sys, os, math, statistics as st, datetime as dt
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(__file__))
import soundings as S, models as M, forecast as F, meps

# Hours from run time until the whole run can be downloaded (MEPS: THREDDS catalog times;
# the others: typical Open-Meteo update times).
DELAY = {'meps': 3, 'ecmwf_ifs025': 8, 'gfs_global': 5, 'meteofrance_arpege_world': 5,
         'ukmo_global_deterministic_10km': 6, 'gem_global': 6, 'icon_global': 5}
CYCLE = dict(F.CYCLE, meps=3)
BANDS = [(0, 3000), (3000, 10000), (10000, 20000), (20000, 40000)]

def run_for(model, s, lead):
    issue = s['launch'] - lead * 3600
    c = CYCLE.get(model, 6) * 3600
    return int((issue - DELAY.get(model, 6) * 3600) // c * c)

def meps_args(s, run_unix):
    t0 = int((s['launch'] - run_unix) // 3600)
    t1 = t0 + int(((s['ztop'] - s['z0']) / s['rate']) // 3600) + 2
    if t0 > 66: return None
    fi, fj = meps.grid_index(s['lat'], s['lon'])
    return dt.datetime.fromtimestamp(run_unix, dt.UTC), t0, min(66, t1), int(fj), int(fi)

def try_block(a):
    try:
        return a and meps.fetch_block(*a)
    except Exception as e:  # a server error on one file: leave that sounding out
        print('MEPS block', a[0], a[1:], 'failed:', e, file=sys.stderr)
        return None

def column(model, s, lead):
    run_unix = run_for(model, s, lead)
    if model == 'meps':
        a = meps_args(s, run_unix)
        d = try_block(a)
        return meps.Column(d, run_unix, s['lat'], s['lon']) if d else None
    run = dt.datetime.fromtimestamp(run_unix, dt.UTC)
    hours = int((s['nominal'] - run_unix) / 3600) + 4
    d = F.fetch_run(model, run, s['lat'], s['lon'], hours)
    return M.Column(d) if d else None

def evaluate(snd, model, lead):
    err = {b: [] for b in BANDS}; allerr = []; d10 = []; d15 = []; used = 0
    for s in snd:
        col = column(model, s, lead)
        if not col: continue
        used += 1
        for L in s['levels']:
            w = col.wind(L['t'], L['z'])
            if w is None or L['z'] > col.top + 500: continue
            e2 = (w[0] - L['u'])**2 + (w[1] - L['v'])**2; allerr.append(e2)
            for b in BANDS:
                if b[0] <= L['z'] - s['z0'] < b[1]: err[b].append(e2)
        for zc, out in ((10000, d10), (15000, d15)):
            if s['ztop'] < zc: continue
            te, tn = S.drift(s['levels'], lambda L: (L['u'], L['v']), zc)
            me, mn = S.drift(s['levels'], lambda L: col.wind(L['t'], L['z']) or (0, 0), zc)
            out.append(math.hypot(me - te, mn - tn) / 1000)
    rms = lambda xs: math.sqrt(st.mean(xs)) if xs else float('nan')
    fmt = lambda xs: ('%4.1f (%4.1f)' % (st.mean(xs), max(xs))) if xs else '  -  '
    return used, ' '.join('%5.2f' % rms(err[b]) for b in BANDS), rms(allerr), fmt(d10), fmt(d15)

if __name__ == '__main__':
    snd = S.load(sys.argv[1]); leads = [int(x) for x in sys.argv[2].split(',')]
    mods = (sys.argv[3] if len(sys.argv) > 3 else 'ecmwf_ifs025,gfs_global,meps').split(',')
    if 'meps' in mods:  # MEPS downloads are slow (about a second per variable-hour): prefetch in parallel
        jobs = [meps_args(s, run_for('meps', s, L)) for L in leads for s in snd]
        with ThreadPoolExecutor(2) as ex:
            list(ex.map(try_block, jobs))
    print('issued L h before launch | RMS vector error 0-3 / 3-10 / 10-20 / 20+ km | all | drift error at 10 / 15 km')
    for L in leads:
        for m in mods:
            used, bands, a, b, c = evaluate(snd, m, L)
            print('L=%2d h  %-30s runs %2d | %s | all %.2f | %s  %s' % (L, m, used, bands, a, b, c)); sys.stdout.flush()
