"""Load the FMI Jokioinen soundings: measured wind profiles, timing, drift by dead reckoning."""
import gzip, json, glob, os, math

SAMPLE_S = 2.0  # raw sample interval: duration ≈ 2 s × n_raw for flights recorded to the end

def load(folder):
    out = []
    for f in sorted(glob.glob(os.path.join(folder, '*.json.gz'))):
        j = json.load(gzip.open(f))
        c = j['cols']; ip, iz, iwd, iws = c.index('p'), c.index('z_m'), c.index('wd'), c.index('ws_kt')
        lv = [(r[ip], r[iz], r[iwd], r[iws] * 0.514444) for r in j['lv'] if r[iwd] is not None and r[iws] is not None]
        z0, ztop = lv[0][1], lv[-1][1]
        dur = j['end'] - j['launch']
        recorded = SAMPLE_S * j['n_raw']           # seconds of data in the profile
        complete = abs(dur - recorded) < 0.08 * dur  # data until the end (burst), no reception gap
        rate = (ztop - z0) / recorded
        levels = []
        for p, z, wd, ws in lv:
            u = -ws * math.sin(math.radians(wd)); v = -ws * math.cos(math.radians(wd))
            levels.append(dict(p=p, z=z, u=u, v=v, t=j['launch'] + (z - z0) / rate))
        out.append(dict(name=os.path.basename(f)[10:20], nominal=j['nominal'], launch=j['launch'], end=j['end'],
                        lat=j['lat'], lon=j['lon'], z0=z0, ztop=ztop, rate=rate, complete=complete, levels=levels))
    return out

def drift(levels, wind_at, zmax):
    """Dead-reckon the ascent: integrate wind_at(level) → (u, v) between levels up to zmax. Returns (east, north) m."""
    e = n = 0.0
    for a, b in zip(levels, levels[1:]):
        if b['z'] > zmax: break
        dt = b['t'] - a['t']
        ua, va = wind_at(a); ub, vb = wind_at(b)
        e += 0.5 * (ua + ub) * dt; n += 0.5 * (va + vb) * dt
    return e, n
