"""Compare model winds with the measured sounding winds: vector error by height, drift error."""
import sys, math, statistics as st
sys.path.insert(0, __import__('os').path.dirname(__file__))
import soundings as S, models as M

BANDS = [(0, 1000), (1000, 3000), (3000, 6000), (6000, 10000), (10000, 15000), (15000, 20000), (20000, 25000), (25000, 40000)]
snd = S.load(sys.argv[1])
modelnames = sys.argv[2].split(',')
lat, lon = snd[0]['lat'], snd[0]['lon']
start, end = '2026-10-10T23:00', '2026-10-22T16:00'

print('%-26s' % 'model', ' '.join('%7s' % f'{a//1000}-{b//1000}' for a, b in BANDS), '| all  | drift err 10km / 15km (km)  top')
for m in modelnames:
    try:
        col = M.Column(M.fetch(m, lat, lon, start, end))
    except Exception as e:
        print('%-26s %s' % (m, e)); continue
    err = {b: [] for b in BANDS}; allerr = []
    d10, d15 = [], []
    for s in snd:
        for L in s['levels']:
            w = col.wind(L['t'], L['z'])
            if w is None or L['z'] > col.top + 500: continue
            e2 = (w[0] - L['u'])**2 + (w[1] - L['v'])**2
            allerr.append(e2)
            for b in BANDS:
                if b[0] <= L['z'] - s['z0'] < b[1]: err[b].append(e2)
        for zc, out in ((10000, d10), (15000, d15)):
            if s['ztop'] < zc or col.top < zc - 1000: continue
            te, tn = S.drift(s['levels'], lambda L: (L['u'], L['v']), zc)
            def mw(L):
                w = col.wind(L['t'], L['z']); return w if w else (0, 0)
            me, mn = S.drift(s['levels'], mw, zc)
            out.append(math.hypot(me - te, mn - tn) / 1000)
    rms = lambda xs: math.sqrt(st.mean(xs)) if xs else float('nan')
    fmt = lambda xs: ('%5.1f/%5.1f' % (st.mean(xs), max(xs))) if xs else '   -  '
    print('%-26s' % m, ' '.join('%7.2f' % rms(err[b]) for b in BANDS), '| %4.2f | %s  %s   %4.1f km' % (rms(allerr), fmt(d10), fmt(d15), col.top/1000))
