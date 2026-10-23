"""MEPS (MetCoOp, 2.5 km, 65 model levels) from MET Norway's THREDDS server via OPeNDAP.

Reads winds on the model's own hybrid levels (not standard pressure levels), rotates them from
the Lambert grid to east/north and computes each level's height hypsometrically from surface
pressure, temperature and humidity. Results are cached like the Open-Meteo downloads."""
import os, math, json, time, urllib.request, urllib.error
import models as M

BASE = 'https://thredds.met.no/thredds/dodsC/meps25epsarchive'
R = 6371000.0
PHI1 = math.radians(63.3)
LON0 = math.radians(15.0)
N = math.sin(PHI1)
F = math.cos(PHI1) * math.tan(math.pi / 4 + PHI1 / 2) ** N / N
RHO0 = R * F / math.tan(math.pi / 4 + PHI1 / 2) ** N
X0, Y0, DX = -1060084.0, -1332517.9, 2500.0
NX, NY, NLEV = 949, 1069, 65
RD, G = 287.05, 9.80665

def grid_index(lat, lon):
    """Fractional (i, j) grid position of a point."""
    rho = R * F / math.tan(math.pi / 4 + math.radians(lat) / 2) ** N
    theta = N * (math.radians(lon) - LON0)
    x = rho * math.sin(theta); y = RHO0 - rho * math.cos(theta)
    return (x - X0) / DX, (y - Y0) / DX

def rotation(lon):
    """Angle between grid x and east at a longitude (radians)."""
    return N * (math.radians(lon) - LON0)

def _ascii(url):
    for attempt in range(6):  # THREDDS answers 429 when one address sends too much at once
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'SondePredict-validation'}), timeout=180) as r:
                text = r.read().decode()
            break
        except urllib.error.HTTPError as e:
            if e.code not in (429, 500, 502, 503) or attempt == 5: raise
            time.sleep(5 * 2 ** attempt)
    # After the header: "[i][j], v, v, ..." lines for multi-dimensional arrays, or a bare
    # "v, v, ..." line for one-dimensional ones; names and shapes are skipped.
    vals = []
    body = text.split('-' * 20, 1)[-1]
    for line in body.splitlines():
        parts = [p.strip() for p in line.split(',')]
        if line.startswith('['):
            vals += [float(v) for v in parts[1:]]
        elif len(parts) > 1:
            try:
                vals += [float(v) for v in parts]
            except ValueError:
                pass
    return vals

def _q(s):
    return s.replace('[', '%5B').replace(']', '%5D')

def fetch_block(run, t0, t1, j, i, nj=2, ni=2):
    """Raw data for hours t0..t1 of a run, grid cells j..j+nj-1, i..i+ni-1. Cached."""
    key = f'meps_{run:%Y%m%d%H}_{t0}_{t1}_{j}_{i}_{nj}_{ni}.json'
    path = os.path.join(M.CACHE, key)
    if os.path.exists(path):
        d = json.load(open(path)); return None if d.get('missing') else d
    ml = f'{BASE}/{run:%Y/%m/%d}/meps_det_ml_{run:%Y%m%dT%H}Z.ncml'
    sfc = f'{BASE}/{run:%Y/%m/%d}/meps_det_2_5km_{run:%Y%m%dT%H}Z.nc'
    box = f'[{j}:1:{j+nj-1}][{i}:1:{i+ni-1}]'
    lev = f'[{t0}:1:{t1}][0:1:{NLEV-1}]'
    d = {}
    try:
        for var in ('x_wind_ml', 'y_wind_ml', 'air_temperature_ml', 'specific_humidity_ml'):
            d[var] = _ascii(f'{ml}.ascii?' + _q(f'{var}.{var}{lev}{box}'))
        d['ps'] = _ascii(f'{ml}.ascii?' + _q(f'surface_air_pressure.surface_air_pressure[{t0}:1:{t1}][0:1:0]{box}'))
        d['phis'] = _ascii(f'{sfc}.ascii?' + _q(f'surface_geopotential.surface_geopotential[0:1:0][0:1:0]{box}'))
        coef = _ascii(f'{ml}.ascii?ap,b')
        d['ap'], d['b'] = coef[:NLEV], coef[NLEV:]
        d.update(t0=t0, t1=t1, j=j, i=i, nj=nj, ni=ni)
    except urllib.error.HTTPError as e:
        if e.code != 404: raise  # only a missing file is worth remembering
        os.makedirs(M.CACHE, exist_ok=True); json.dump({'missing': str(e)}, open(path, 'w')); return None
    os.makedirs(M.CACHE, exist_ok=True); json.dump(d, open(path, 'w'))
    return d

def profiles(d, lon, standard_levels=False):
    """{(hour_index, dj, di): sorted [(z_geometric, u, v)]} for every cell and hour of a block.
    With standard_levels, the profile is first reduced to Open-Meteo's 28 standard pressure
    levels (to measure what the model levels add)."""
    nt = d['t1'] - d['t0'] + 1; nj, ni = d['nj'], d['ni']
    a = rotation(lon); ca, sa = math.cos(a), math.sin(a)
    out = {}
    def at(var, t, k, jj, ii):
        return d[var][((t * NLEV + k) * nj + jj) * ni + ii]
    for t in range(nt):
        for jj in range(nj):
            for ii in range(ni):
                ps = d['ps'][(t * nj + jj) * ni + ii]
                zs = d['phis'][jj * ni + ii] / G
                p = [d['ap'][k] + d['b'][k] * ps for k in range(NLEV)]
                tv = [at('air_temperature_ml', t, k, jj, ii) * (1 + 0.608 * at('specific_humidity_ml', t, k, jj, ii)) for k in range(NLEV)]
                z = [0.0] * NLEV
                z[NLEV - 1] = zs + RD * tv[NLEV - 1] / G * math.log(ps / p[NLEV - 1])
                for k in range(NLEV - 2, -1, -1):
                    z[k] = z[k + 1] + RD * 0.5 * (tv[k] + tv[k + 1]) / G * math.log(p[k + 1] / p[k])
                prof = []
                for k in range(NLEV):
                    gx, gy = at('x_wind_ml', t, k, jj, ii), at('y_wind_ml', t, k, jj, ii)
                    u = gx * ca + gy * sa; v = -gx * sa + gy * ca
                    prof.append((M.geometric(z[k]), u, v, p[k]))
                prof.sort()
                if standard_levels:
                    prof = _to_standard(prof)
                out[(t, jj, ii)] = [(zz, u, v) for zz, u, v, *_ in prof]
    return out

def _to_standard(prof):
    """Keep only what Open-Meteo would give: values at the 28 standard pressure levels."""
    out = [prof[0]]  # lowest model level ~12 m, like the 10 m wind
    for lv in M.LEVELS:
        pl = lv * 100.0
        for lo, hi in zip(prof, prof[1:]):  # sorted by height: pressure decreasing
            if lo[3] >= pl >= hi[3]:
                f = math.log(lo[3] / pl) / math.log(lo[3] / hi[3])
                out.append(tuple(lo[n] + f * (hi[n] - lo[n]) for n in range(3)) + (pl,))
                break
    return sorted(out)

class Column:
    """Bilinear-in-space, linear-in-time-and-height winds at one point from a MEPS block."""
    def __init__(self, d, run_unix, lat, lon, standard_levels=False):
        self.prof = profiles(d, lon, standard_levels)
        fi, fj = grid_index(lat, lon)
        self.fx, self.fy = fi - d['i'], fj - d['j']
        self.t0 = run_unix + d['t0'] * 3600; self.nt = d['t1'] - d['t0'] + 1
        self.top = min(p[-1][0] for p in self.prof.values())

    def wind(self, t, z):
        h = (t - self.t0) / 3600.0; k = int(math.floor(h)); f = h - k
        if k < 0 or k + 1 >= self.nt: return None
        u = v = 0.0
        for kk, wt in ((k, 1 - f), (k + 1, f)):
            for jj, wy in ((0, 1 - self.fy), (1, self.fy)):
                for ii, wx in ((0, 1 - self.fx), (1, self.fx)):
                    uu, vv = M.interp(self.prof[(kk, jj, ii)], z)
                    w = wt * wy * wx; u += w * uu; v += w * vv
        return u, v
