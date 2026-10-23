"""Ascent and descent of real flights (SondeHub tracks): how the ascent rate changes with height,
and which density-scaled descent v = v0 · (ρ0/ρ)^k fits the measured fall.

    python3 tracks.py <site> <first day> <last day> <max flights>"""
import sys, os, math, statistics as st, datetime as dt
sys.path.insert(0, os.path.dirname(__file__))
import sondehub as H

# US Standard Atmosphere 1976 density (kg/m³) by geometric altitude, as the app uses.
_LAYERS = [(0, 288.15, -0.0065, 101325.0), (11000, 216.65, 0.0, 22632.06), (20000, 216.65, 0.001, 5474.889),
           (32000, 228.65, 0.0028, 868.0187), (47000, 270.65, 0.0, 110.9063), (51000, 270.65, -0.0028, 66.93887),
           (71000, 214.65, -0.002, 3.956420)]
def density(z):
    h = 6356766.0 * z / (6356766.0 + z)
    for base, T0, L, P0 in reversed(_LAYERS):
        if h >= base:
            break
    T = T0 + L * (h - base)
    P = P0 * math.exp(-9.80665 * 0.0289644 * (h - base) / (8.3144598 * T0)) if L == 0 else \
        P0 * (T0 / T) ** (9.80665 * 0.0289644 / (8.3144598 * L))
    return P * 0.0289644 / (8.3144598 * T)
RHO0 = density(0)

def rates(track, phase_up):
    """(altitude, vertical speed) every ~30 s from position differences (robust to bad vel_v)."""
    out = []
    i = 0
    while i < len(track):
        j = i
        while j < len(track) and track[j]['t'] - track[i]['t'] < 30: j += 1
        if j >= len(track): break
        a, b = track[i], track[j]
        v = (b['alt'] - a['alt']) / (b['t'] - a['t'])
        if (v > 0) == phase_up and abs(v) < 120:
            out.append((0.5 * (a['alt'] + b['alt']), abs(v)))
        i = j
    return out

def fit_descent(samples):
    """Least squares in log space: log v = log v0 + k log(ρ0/ρ)."""
    xs = [math.log(RHO0 / density(z)) for z, v in samples]
    ys = [math.log(v) for z, v in samples]
    mx, my = st.mean(xs), st.mean(ys)
    k = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx)**2 for x in xs)
    return math.exp(my - k * mx), k

if __name__ == '__main__':
    site = sys.argv[1]; first = dt.date.fromisoformat(sys.argv[2]); last = dt.date.fromisoformat(sys.argv[3]); n = int(sys.argv[4])
    pos = (60.8146, 23.4987)
    fl = [f for f in H.flights(site, first, last, pos) if 20000 < f['burst']['alt'] < 45000 and f['last']['alt'] < 2000][-n:]
    up_bins = {}; down = []; per_flight = []
    for f in fl:
        tr = H.track(f['serial'])
        tb = H.t(f['burst'])
        asc = [p for p in tr if p['t'] <= tb]; des = [p for p in tr if p['t'] > tb]
        ua = rates(asc, True)
        mean_up = (asc[-1]['alt'] - asc[0]['alt']) / max(1, asc[-1]['t'] - asc[0]['t']) if len(asc) > 2 else None
        if mean_up:
            for z, v in ua:
                up_bins.setdefault(int(z // 3000) * 3, []).append(v / mean_up)
        ds = [(z, v) for z, v in rates(des, False) if z > 300]
        if len(ds) > 20:
            down += ds
            v0, k = fit_descent(ds)
            per_flight.append((f['serial'], v0, k, len(ds)))
        print(f"{f['serial']} {f['first']['datetime'][:16]} burst {f['burst']['alt']/1000:.1f} km, mean ascent {mean_up or 0:.2f} m/s, "
              f"{len(ua)} ascent / {len(ds)} descent samples" + (f", descent fit v0 {per_flight[-1][1]:.1f} k {per_flight[-1][2]:.2f}" if per_flight and per_flight[-1][0] == f['serial'] else ''))
        sys.stdout.flush()
    print('\nAscent rate by height, relative to the flight average:')
    for b in sorted(up_bins):
        xs = up_bins[b]
        print(f'  {b:2d}–{b+3:2d} km  {st.median(xs):.2f}  (n {len(xs)})')
    v0, k = fit_descent(down)
    print(f'\nDescent fit over {len(per_flight)} flights ({len(down)} samples): v0 {v0:.2f} m/s, k {k:.3f}')
    print('per flight v0: median %.1f (p10 %.1f, p90 %.1f); k median %.2f' % (
        st.median(p[1] for p in per_flight), sorted(p[1] for p in per_flight)[len(per_flight)//10],
        sorted(p[1] for p in per_flight)[9*len(per_flight)//10], st.median(p[2] for p in per_flight)))
    for name, (a, b) in (('fit', (v0, k)), ('app before (12, 0.24)', (12.0, 0.24)), ('textbook k 0.5 (v0 fit)', (None, 0.5))):
        if a is None:  # best v0 for k = 0.5
            a = math.exp(st.mean(math.log(v) - b * math.log(RHO0 / density(z)) for z, v in down))
        err = [v - a * (RHO0 / density(z)) ** b for z, v in down]
        # Time to fall from 30 km to 300 m with this model vs measured (mean over samples in bins)
        print(f'  {name:26s} v0 {a:5.2f} k {b:.2f}: rms speed error {math.sqrt(st.mean(e*e for e in err)):.2f} m/s, bias {st.mean(err):+.2f}')
    print('\nMeasured descent speed by height (median):')
    for lo in range(0, 36000, 3000):
        vs = [v for z, v in down if lo <= z < lo + 3000]
        if vs: print(f'  {lo//1000:2d}–{lo//1000+3:2d} km  {st.median(vs):5.1f} m/s  model {v0 * (RHO0/density(lo+1500))**k:5.1f}  (n {len(vs)})')
