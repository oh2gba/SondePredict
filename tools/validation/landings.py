"""Whole predictions against real flights: Sonde Predict (sp-cli --replay) flies each SondeHub
flight from Jokioinen with several balloon settings, and the predicted burst and landing points
are compared with where the sonde really was.

    python3 landings.py <site> <first day> <last day> <sp-cli> [--model meps-only|average]

Settings compared (all with the winds the app would have used at launch time):
  old    the app's earlier defaults: 5.1 m/s, 29 700 m, descent 12.5 m/s, k 0.24, constant ascent
  new    recent site statistics: median ascent and burst of the flights before, descent 11.7 / 0.33, shaped ascent
  true-c each flight's real ascent rate and burst, constant ascent
  true-s each flight's real ascent rate and burst, shaped ascent
The last two separate the wind and model errors from not knowing the balloon in advance."""
import sys, os, math, subprocess, statistics as st, datetime as dt
sys.path.insert(0, os.path.dirname(__file__))
import sondehub as H

SITE = {'02963': (60.8146, 23.4987, 104.0)}

def iso(ts):
    return dt.datetime.fromtimestamp(ts, dt.UTC).strftime('%Y-%m-%dT%H:%M:%SZ')

if __name__ == '__main__':
    site, first, last, cli = sys.argv[1], dt.date.fromisoformat(sys.argv[2]), dt.date.fromisoformat(sys.argv[3]), sys.argv[4]
    lat, lon, alt = SITE[site]
    history = H.flights(site, first - dt.timedelta(days=30), first - dt.timedelta(days=1), (lat, lon))
    hist_up = [(f['burst']['alt'] - f['first']['alt']) / (H.t(f['burst']) - H.t(f['first'])) for f in history if 20000 < f['burst']['alt'] < 45000]
    hist_burst = [f['burst']['alt'] for f in history if 20000 < f['burst']['alt'] < 45000]
    med_up, med_burst = st.median(hist_up), st.median(hist_burst)
    print(f'site statistics from the 30 days before: ascent {med_up:.2f} m/s, burst {med_burst:.0f} m ({len(hist_burst)} flights)')

    flights = [f for f in H.flights(site, first, last, (lat, lon)) if 20000 < f['burst']['alt'] < 42000 and f['last']['alt'] < 1000]
    jobs, truth = [], {}
    for f in flights:
        a, b, c = f['first'], f['burst'], f['last']
        up = (b['alt'] - a['alt']) / (H.t(b) - H.t(a))
        launch = H.t(a) - (a['alt'] - alt) / up
        truth[f['serial']] = (b, c, launch)
        L = iso(launch)
        for name, asc, burst, v0, k, shaped in (('old', 5.1, 29700, 12.5, 0.24, 0), ('new', med_up, med_burst, 11.7, 0.33, 1),
                                                ('true-c', up, b['alt'], 11.7, 0.33, 0), ('true-s', up, b['alt'], 11.7, 0.33, 1)):
            jobs.append(f"{f['serial']}:{name},{L},{lat},{lon},{alt},{asc:.3f},{burst:.0f},{v0},{k},{shaped}")
    path = os.path.join(os.environ.get('TMPDIR', '/tmp'), f'replay_{site}.csv')
    open(path, 'w').write('\n'.join(jobs) + '\n')
    model = sys.argv[sys.argv.index('--model') + 1] if '--model' in sys.argv else 'meps-only'
    args = [cli, '--replay', path, '--config', os.path.join(os.environ.get('TMPDIR', '/tmp'), 'replay.ini')]
    if model == 'meps-only': args.append('--meps-only')
    print(f'{len(flights)} flights, {len(jobs)} predictions with', model)
    res = subprocess.run(args, capture_output=True, text=True)
    out = {}
    for line in res.stdout.splitlines():
        p = line.split(',')
        out[p[0]] = p
    errs = {}
    for key, p in out.items():
        serial, name = key.split(':')
        if len(p) < 7 or not p[4]:
            print('failed', key, p[-1]); continue
        b, c, launch = truth[serial]
        eb = H.dist((float(p[1]), float(p[2])), b) / 1000
        el = H.dist((float(p[4]), float(p[5])), c) / 1000
        errs.setdefault(name, []).append((eb, el, serial))
    print('\nsetting   flights   burst point error km (median / mean / p90)   landing error km (median / mean / p90)')
    for name in ('old', 'new', 'true-c', 'true-s'):
        e = errs.get(name, [])
        if not e: continue
        b = sorted(x[0] for x in e); l = sorted(x[1] for x in e)
        print(f'{name:8s} {len(e):5d}      {st.median(b):5.1f} / {st.mean(b):5.1f} / {b[9*len(b)//10]:5.1f}'
              f'                      {st.median(l):5.1f} / {st.mean(l):5.1f} / {l[9*len(l)//10]:5.1f}')
    if res.returncode != 0:
        print(res.stderr[-2000:])
