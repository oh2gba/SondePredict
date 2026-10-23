"""Multi-model mean: does averaging the winds of several forecasts beat the best single one?

    python3 blend.py <soundings folder> <lead hours> model1+model2,model3,...

Uses the runs forecast.py downloaded (same cache); 'meps' means MEPS on model levels."""
import sys, os, math, itertools, statistics as st, datetime as dt
sys.path.insert(0, os.path.dirname(__file__))
import soundings as S, models as M, forecast as F, meps

def column(model, s, lead):
    if model == 'meps':
        ref = s['launch'] if lead == 0 else s['nominal'] - lead * 3600
        run_unix = int(ref // 10800 * 10800)
        run = dt.datetime.fromtimestamp(run_unix, dt.UTC)
        t0 = int((s['launch'] - run_unix) // 3600)
        t1 = min(66, t0 + int(((s['ztop'] - s['z0']) / s['rate']) // 3600) + 2)
        fi, fj = meps.grid_index(s['lat'], s['lon'])
        d = meps.fetch_block(run, t0, t1, int(fj), int(fi))
        return meps.Column(d, run_unix, s['lat'], s['lon']) if d else None
    run = F.run_for(model, s['nominal'], lead)
    hours = int((s['nominal'] - run.timestamp()) / 3600) + 4
    d = F.fetch_run(model, run, s['lat'], s['lon'], hours)
    return M.Column(d) if d else None

def evaluate(snd, lead, combo):
    allerr = []; d10 = []; d15 = []
    for s in snd:
        cols = [column(m, s, lead) for m in combo]
        if any(c is None for c in cols): continue
        def wind(L):
            ws = [c.wind(L['t'], L['z']) for c in cols]
            if any(w is None for w in ws): return None
            return sum(w[0] for w in ws) / len(ws), sum(w[1] for w in ws) / len(ws)
        top = min(c.top for c in cols)
        for L in s['levels']:
            w = wind(L)
            if w is None or L['z'] > top + 500: continue
            allerr.append((w[0] - L['u'])**2 + (w[1] - L['v'])**2)
        for zc, out in ((10000, d10), (15000, d15)):
            if s['ztop'] < zc: continue
            te, tn = S.drift(s['levels'], lambda L: (L['u'], L['v']), zc)
            me, mn = S.drift(s['levels'], lambda L: wind(L) or (0, 0), zc)
            out.append(math.hypot(me - te, mn - tn) / 1000)
    return math.sqrt(st.mean(allerr)), st.mean(d10), max(d10), st.mean(d15), max(d15)

if __name__ == '__main__':
    snd = S.load(sys.argv[1]); lead = int(sys.argv[2])
    print(f'lead {lead} h | all RMS (m/s) | drift error at 10 km mean (max) | at 15 km')
    for spec in sys.argv[3].split(','):
        combo = spec.split('+')
        r = evaluate(snd, lead, combo)
        print('%-60s %.2f | %4.1f (%4.1f) | %4.1f (%4.1f)' % (spec, *r)); sys.stdout.flush()
