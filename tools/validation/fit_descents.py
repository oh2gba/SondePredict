"""Fit the density-scaled descent (sea-level speed v0, exponent k) of launch sites to their
recent SondeHub tracks, for the sites the app ships with fitted descents.

    python3 fit_descents.py <output.json> [--days 30] [--tracks 8] [--min-flights 30]
                            [--box lat0,lat1,lon0,lon1]

SondeHub's own descent averages can be far off (Tallinn: 13.7 m/s listed, 7.3 m/s measured in
October 2026, a parachute), so tools/sondehub_sites.py prefers these fits. Sites in the box
(default the Nordic countries, the Baltic and around: 50–72 N, 10 W–40 E) with at least
--min-flights flights in SondeHub's statistics are fitted from up to --tracks flights of the
last --days days that were heard to near the ground. Downloads are cached (SP_VALIDATION_CACHE)."""
import sys, os, json, datetime as dt
sys.path.insert(0, os.path.dirname(__file__))
import sondehub as H, stations as S

def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

def main():
    out = sys.argv[1]
    days, ntracks, minflights = int(arg('--days', 30)), int(arg('--tracks', 8)), int(arg('--min-flights', 30))
    lat0, lat1, lon0, lon1 = (float(x) for x in arg('--box', '50,72,-10,40').split(','))
    last = dt.datetime.now(dt.UTC).date() - dt.timedelta(days=1)
    first = last - dt.timedelta(days=days)
    fits = json.load(open(out)) if os.path.exists(out) else {}
    for sid, site in sorted(S.SITES.items(), key=lambda kv: kv[1]['name']):
        if not (lat0 <= site['lat'] <= lat1 and lon0 <= site['lon'] <= lon1):
            continue
        if site.get('flights', 0) < minflights or sid.startswith('-'):
            continue
        if sid in fits and fits[sid].get('until') == str(last):
            continue
        flights = [f for f in H.flights(sid, first, last, (site['lat'], site['lon']))
                   if 15000 < f['burst']['alt'] < 42000 and f['last']['alt'] < 1500]
        fitted, n = S.fit_descent(flights, ntracks) if len(flights) >= 3 else (None, 0)
        if not fitted:
            print(f"{site['name']:32s} {len(flights):3d} flights landed: too few complete descents"); sys.stdout.flush()
            continue
        v0, k, rms = fitted
        fits[sid] = {'name': site['name'], 'v0': round(v0, 1), 'k': round(k, 2), 'tracks': n,
                     'rmsSeconds': round(rms), 'from': str(first), 'until': str(last)}
        print(f"{site['name']:32s} v0 {v0:5.1f} m/s  k {k:.2f}  ({n} tracks, fall-time rms {rms:.0f} s; SondeHub average {site['descent']} m/s)")
        sys.stdout.flush()
        json.dump(fits, open(out, 'w'), indent=1, ensure_ascii=False)
    json.dump(fits, open(out, 'w'), indent=1, ensure_ascii=False)

if __name__ == '__main__':
    main()
