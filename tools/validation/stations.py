"""Whole predictions for any SondeHub launch site inside the MEPS area, and the size of the
landing area.

    python3 stations.py <site id> <first day> <last day> <sp-cli> [--sigmas 0,0.5,1,1.5] [--tracks 12]
    python3 stations.py <site id> <first day> <last day> <sp-cli> --compare-average

--compare-average instead predicts each flight (fitted balloon, no landing area) twice: with
MEPS alone and with MEPS averaged with ECMWF. That one uses Open-Meteo quota.

For the site: the median ascent and burst of its flights in the 30 days before <first day>
(what "Use" / "From SondeHub" give in the app), its descent fitted to up to --tracks full
tracks from the test days, then every flight of the test days predicted (sp-cli --replay,
MEPS winds from the run before launch) with
  app    the 30-day medians and SondeHub's long-term descent (k 0.33 unless fitted)
  fit    the 30-day medians and the fitted descent, with the landing area for each forecast
         wind error in --sigmas (m/s): how often does the real landing fall inside the 95 % area?
  true   each flight's real ascent and burst, fitted descent
"""
import sys, os, json, math, statistics as st, subprocess, datetime as dt
sys.path.insert(0, os.path.dirname(__file__))
import sondehub as H, tracks as T

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
SITES = {s['id']: s for s in json.load(open(os.path.join(ROOT, 'src', 'app', 'data', 'launch-sites.json')))}

def iso(ts):
    return dt.datetime.fromtimestamp(ts, dt.UTC).strftime('%Y-%m-%dT%H:%M:%SZ')

def robust_sd(xs):
    xs = sorted(xs)
    return (xs[9 * len(xs) // 10] - xs[len(xs) // 10]) / 2.563

def fit_descent(flights, n):
    """v0, k from fall times 25 (or burst − 3) → 10 km and 10 → 2 km of up to n flights."""
    seg = []
    for f in flights[-n:]:
        try:
            tr = H.track(f['serial'])
        except Exception as e:
            print('  track', f['serial'], 'not loaded:', e, file=sys.stderr); continue
        tb = H.t(f['burst'])
        des = [p for p in tr if p['t'] > tb]
        top = min(25000, f['burst']['alt'] - 3000)
        a = next((p for p in des if p['alt'] < top), None)
        m = next((p for p in des if p['alt'] < 10000), None)
        b = next((p for p in des if p['alt'] < 2000), None)
        if a and m and b and 0 < b['t'] - a['t'] < 6000:
            seg.append((a['alt'], m['alt'], m['t'] - a['t']))
            seg.append((m['alt'], b['alt'], b['t'] - m['t']))
    if len(seg) < 6:
        return None, 0
    def fall(v0, k, z1, z0):
        t, z = 0.0, z1
        while z > z0:
            t += 100 / (v0 * (T.RHO0 / T.density(z)) ** k); z -= 100
        return t
    best = None
    for k in [x / 100 for x in range(10, 61, 2)]:
        # For a given k the best v0 scales the total time.
        v0 = sum(fall(1.0, k, z1, z0) for z1, z0, _ in seg) / sum(tt for *_, tt in seg)
        rms = math.sqrt(st.mean((fall(v0, k, z1, z0) - tt) ** 2 for z1, z0, tt in seg))
        if best is None or rms < best[2]:
            best = (v0, k, rms)
    return best, len(seg) // 2

def main():
    site_id, first, last, cli = sys.argv[1], dt.date.fromisoformat(sys.argv[2]), dt.date.fromisoformat(sys.argv[3]), sys.argv[4]
    sigmas = [float(x) for x in (sys.argv[sys.argv.index('--sigmas') + 1] if '--sigmas' in sys.argv else '0,0.5,1,1.5').split(',')]
    ntracks = int(sys.argv[sys.argv.index('--tracks') + 1]) if '--tracks' in sys.argv else 12
    site = SITES[site_id]
    pos = (site['lat'], site['lon'])
    print(f"== {site['name']} ({site_id}) {first} … {last}")

    ok = lambda f: 15000 < f['burst']['alt'] < 42000
    hist = [f for f in H.flights(site_id, first - dt.timedelta(days=30), first - dt.timedelta(days=1), pos) if ok(f)]
    if len(hist) < 5:
        print('  too few flights before the test days'); return
    ups = [(f['burst']['alt'] - f['first']['alt']) / (H.t(f['burst']) - H.t(f['first'])) for f in hist]
    bursts = [f['burst']['alt'] for f in hist]
    med_up, med_burst = st.median(ups), st.median(bursts)
    sd_up, sd_burst = robust_sd(ups), robust_sd(bursts)
    print(f'  30 days before: {len(hist)} flights, ascent {med_up:.2f} ± {sd_up:.2f} m/s, burst {med_burst:.0f} ± {sd_burst:.0f} m')

    flights = [f for f in H.flights(site_id, first, last, pos) if ok(f) and f['last']['alt'] < 1500]
    if not flights:
        print('  no landed flights in the test days'); return
    app_v0, app_k = site['descent'], site.get('descentK', 0.33)
    fitted, n = fit_descent(flights, ntracks)
    if fitted:
        fit_v0, fit_k, rms = fitted
        print(f'  descent fitted to {n} tracks: v0 {fit_v0:.1f} m/s, k {fit_k:.2f} (fall-time rms {rms:.0f} s); SondeHub average {app_v0} m/s')
    else:
        fit_v0, fit_k = app_v0, app_k
        print(f'  descent: too few complete tracks, using SondeHub average {app_v0} m/s, k {app_k}')

    if '--compare-average' in sys.argv:
        compare_average(site_id, flights, pos, med_up, med_burst, fit_v0, fit_k, cli)
        return

    jobs, truth = [], {}
    for f in flights:
        a, b, c = f['first'], f['burst'], f['last']
        up = (b['alt'] - a['alt']) / (H.t(b) - H.t(a))
        launch = H.t(a) - max(0.0, a['alt'] - 100.0) / up
        truth[f['serial']] = c
        L = iso(launch)
        row = lambda name, asc, burst, v0, k, sigma: f"{f['serial']}:{name},{L},{pos[0]},{pos[1]},,{asc:.3f},{burst:.0f},{v0:.2f},{k:.2f},1,{sigma}"
        jobs.append(row('app', med_up, med_burst, app_v0, app_k, 0))
        for s in sigmas:
            jobs.append(row(f'fit/{s:g}', med_up, med_burst, fit_v0, fit_k, s))
        jobs.append(row('true', up, b['alt'], fit_v0, fit_k, 0))

    tmp = os.environ.get('TMPDIR', '/tmp')
    path = os.path.join(tmp, f'replay_{site_id}.csv'); open(path, 'w').write('\n'.join(jobs) + '\n')
    ini = os.path.join(tmp, f'replay_{site_id}.ini')
    open(ini, 'w').write(f'[landing_area]\nenabled=true\nascent_sigma={sd_up:.3f}\nburst_sigma={sd_burst:.0f}\ndescent_sigma=0.18\nsamples=50\n')
    print(f'  {len(flights)} flights, {len(jobs)} predictions with MEPS …'); sys.stdout.flush()
    res = subprocess.run([cli, '--replay', path, '--config', ini, '--meps-only'], capture_output=True, text=True)
    open(os.path.join(tmp, f'replay_{site_id}.out'), 'w').write(res.stdout)  # for re-analysis without re-running

    stats = {}
    for line in res.stdout.splitlines():
        p = line.split(',')
        if len(p) < 8 or not p[4]:
            continue
        serial, name = p[0].split(':')
        c = truth[serial]
        err = H.dist((float(p[4]), float(p[5])), c) / 1000
        inside = None; size = None
        # id, burst lat/lon/time, landing lat/lon/time, model, then the area: lat, lon, σ major, σ minor, bearing
        if len(p) >= 13 and p[8]:
            clat, clon, smaj, smin, bearing = float(p[8]), float(p[9]), float(p[10]), float(p[11]), float(p[12])
            # Inside the 95 % ellipse (scale 2.448 σ), in a local flat frame.
            north = (c['lat'] - clat) * 111320.0
            east = (c['lon'] - clon) * 111320.0 * math.cos(math.radians(clat))
            th = math.radians(bearing)
            along = north * math.cos(th) + east * math.sin(th); across = -north * math.sin(th) + east * math.cos(th)
            k = 2.448
            inside = (along / max(smaj * k, 1)) ** 2 + (across / max(smin * k, 1)) ** 2 <= 1
            size = (2 * k * smaj / 1000, 2 * k * smin / 1000)
        stats.setdefault(name, []).append((err, inside, size))
    order = ['app'] + [f'fit/{s:g}' for s in sigmas] + ['true']
    print('  setting      n   landing error km (median / mean / p90)   inside 95 % area   area km (median)')
    for name in order:
        e = stats.get(name, [])
        if not e:
            continue
        errs = sorted(x[0] for x in e)
        ins = [x[1] for x in e if x[1] is not None]
        sizes = [x[2] for x in e if x[2]]
        cover = f'{100 * sum(ins) / len(ins):5.0f} %' if ins else '    -'
        area = f'{st.median(s[0] for s in sizes):5.1f} × {st.median(s[1] for s in sizes):4.1f}' if sizes else '-'
        print(f'  {name:10s} {len(e):3d}   {st.median(errs):5.1f} / {st.mean(errs):5.1f} / {errs[9 * len(errs) // 10]:5.1f}'
              f'                 {cover}          {area}')
    if res.returncode:
        print(res.stderr[-1500:])
    sys.stdout.flush()

def compare_average(site_id, flights, pos, med_up, med_burst, v0, k, cli):
    tmp = os.environ.get('TMPDIR', '/tmp')
    rows = []
    for f in flights:
        a, b = f['first'], f['burst']
        up = (b['alt'] - a['alt']) / (H.t(b) - H.t(a))
        launch = H.t(a) - max(0.0, a['alt'] - 100.0) / up
        rows.append(f"{f['serial']}:x,{iso(launch)},{pos[0]},{pos[1]},,{med_up:.3f},{med_burst:.0f},{v0:.2f},{k:.2f},1")
    path = os.path.join(tmp, f'replay_{site_id}_avg.csv'); open(path, 'w').write('\n'.join(rows) + '\n')
    ini = os.path.join(tmp, f'replay_{site_id}_avg.ini')
    open(ini, 'w').write('[landing_area]\nenabled=false\n[wind]\nmodel=ecmwf_ifs025\nuse_meps=true\naverage_meps=true\n')
    truth = {f['serial']: f['last'] for f in flights}
    print(f'  {len(flights)} flights: MEPS alone, then MEPS + ECMWF averaged (uses Open-Meteo quota) …'); sys.stdout.flush()
    for name, extra in (('MEPS alone', ['--meps-only']), ('MEPS + ECMWF', [])):
        res = subprocess.run([cli, '--replay', path, '--config', ini, *extra], capture_output=True, text=True)
        errs, models = [], set()
        for line in res.stdout.splitlines():
            p = line.split(',')
            if len(p) >= 8 and p[4]:
                errs.append(H.dist((float(p[4]), float(p[5])), truth[p[0].split(':')[0]]) / 1000)
                models.add(p[7])
        errs.sort()
        if errs:
            print(f'  {name:13s} n {len(errs):3d}   landing error km median {st.median(errs):5.1f}  mean {st.mean(errs):5.1f}  '
                  f'p90 {errs[9 * len(errs) // 10]:5.1f}   models used: {", ".join(sorted(models))}')
        if res.returncode:
            print(res.stderr[-800:])
        sys.stdout.flush()

if __name__ == '__main__':
    main()
