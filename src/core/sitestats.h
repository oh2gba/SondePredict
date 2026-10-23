#pragma once

#include "flightprofile.h"
#include "geo.h"
#include "landingarea.h"
#include "settings.h"
#include "sondehub.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QTime>

#include <optional>
#include <vector>

namespace sp {

// What radiosondes from one launch site typically do, from SondeHub's statistics of the
// flights its receivers tracked (https://sondehub.org).
struct LaunchSiteStats {
    QString id;   // WMO station number, e.g. "02963"
    QString name; // e.g. "Jokioinen (Finland)"
    GeoPoint position;
    double ascentRate = 0.0;    // m/s
    double burstAltitude = 0.0; // m
    double descentRate = 0.0;   // m/s at sea level
    std::optional<double> descentExponent; // k, when fitted to the site's recent descents
    int descentTracks = 0;                 // descents behind that fit (0: SondeHub's average)
    QDate descentUntil;                    // last day of the flights fitted

    bool descentMeasured() const { return descentExponent.has_value(); }
    // The exponent to use: the fitted one, or for a site without a fit one that suits its
    // speed: parachutes (slow) slow down more near the ground than sondes falling free.
    double exponent() const;
    // One standard deviation, when SondeHub has enough flights.
    std::optional<double> ascentSd;
    std::optional<double> burstSd;
    std::optional<double> descentSd;
    int flights = 0; // flights behind the burst average; 0 if unknown
    QList<QTime> synopticTimes; // the observation times the site launches for (UTC)

    // Typical launch times: 30 minutes before each synoptic time.
    QList<QTime> launchTimes() const;

    // "measured on 12 descents up to 22.10.2026" or "SondeHub's long-term average", for people.
    QString descentSource() const;

    // `profile` with this site's ascent, burst and (density-scaled) descent rate.
    FlightProfile applyTo(FlightProfile profile) const;
    // `spread` with this site's variation where known.
    FlightSpread applyTo(FlightSpread spread) const;
};

// Parses the bundled list (tools/sondehub_sites.py). Returns nothing on malformed data.
std::vector<LaunchSiteStats> parseLaunchSiteStats(const QByteArray &json);

// Sorted by distance from the nearest of `places` (all of them if `places` is empty: by name).
void sortByDistance(std::vector<LaunchSiteStats> &sites, const std::vector<GeoPoint> &places);

// The site within `maxKm` of `p`, nearest first; null if none.
const LaunchSiteStats *nearestSite(const std::vector<LaunchSiteStats> &sites, GeoPoint p, double maxKm = 25.0);

// Balloon values for a launch location from a site's statistics: the recent flights' ascent
// and burst when known (they change as stations change balloons), otherwise the long-term
// averages; the descent measured on recent tracks where Sonde Predict has a fit, otherwise
// SondeHub's average with an exponent that suits its speed (LaunchSiteStats::exponent()).
SiteBalloon siteBalloon(const LaunchSiteStats &site, const std::optional<RecentFlightStats> &recent,
                        const SiteBalloon &base = {});

} // namespace sp
