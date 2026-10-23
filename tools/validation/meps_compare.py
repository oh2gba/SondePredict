"""MEPS model levels vs standard levels vs the measured sounding winds.

    python3 meps_compare.py <soundings folder> <lead hours> [--flip]

lead 0 uses the latest MEPS run before launch; 24 the run issued a day before the nominal
time. --flip negates the grid-to-east rotation (a sanity check: it must be clearly worse)."""
import sys, os, math, datetime as dt, statistics as st
sys.path.insert(0, os.path.dirname(__file__))
import soundings as S, models as M, meps

BANDS = [(0, 3000), (3000, 10000), (10000, 20000), (20000, 40000)]

def evaluate(snd, lead, standard_levels, flip=False):
    err = {b: [] for b in BANDS}; allerr = []; d10 = []; d15 = []; used = 0
    if flip:
        meps.N_SAVE = meps.N
        orig = meps.rotation; meps.rotation = lambda lon: -orig(lon)
    for s in snd:
        ref = s['launch'] if lead == 0 else s['nominal'] - lead * 3600
        run_unix = int(ref // 10800 * 10800)
        run = dt.datetime.fromtimestamp(run_unix, dt.UTC)
        t0 = int((s['launch'] - run_unix) // 3600)
        t1 = min(66, t0 + int(((s['ztop'] - s['z0']) / s['rate']) // 3600) + 2)
        fi, fj = meps.grid_index(s['lat'], s['lon'])
        d = meps.fetch_block(run, t0, t1, int(fj), int(fi))
        if not d: continue
        col = meps.Column(d, run_unix, s['lat'], s['lon'], standard_levels); used += 1
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
    if flip:
        meps.rotation = orig
    rms = lambda xs: math.sqrt(st.mean(xs)) if xs else float('nan')
    fmt = lambda xs: ('%4.1f (%4.1f)' % (st.mean(xs), max(xs))) if xs else '  -  '
    return used, ' '.join('%5.2f' % rms(err[b]) for b in BANDS), rms(allerr), fmt(d10), fmt(d15)

if __name__ == '__main__':
    snd = S.load(sys.argv[1]); lead = int(sys.argv[2])
    print(f'MEPS, lead {lead} h | RMS vector error 0-3 / 3-10 / 10-20 / 20+ km | all | drift error at 10 / 15 km')
    variants = [('model levels (65)', False, False), ('standard levels (28)', True, False)]
    if '--flip' in sys.argv: variants.append(('model levels, rotation flipped', False, True))
    for name, std, flip in variants:
        used, bands, allrms, a, b = evaluate(snd, lead, std, flip)
        print('%-32s runs %2d | %s | all %.2f | %s  %s' % (name, used, bands, allrms, a, b)); sys.stdout.flush()
