#include "sitestats.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sp {

FlightProfile LaunchSiteStats::applyTo(FlightProfile profile) const
{
    profile.ascentRate = ascentRate;
    profile.burstAltitude = burstAltitude;
    profile.descentModel = DescentModel::DensityScaled;
    profile.seaLevelDescentRate = descentRate;
    profile.descentExponent = exponent();
    return profile;
}

FlightSpread LaunchSiteStats::applyTo(FlightSpread spread) const
{
    if (ascentSd)
        spread.ascentRate = *ascentSd;
    if (burstSd)
        spread.burstAltitude = *burstSd;
    if (descentSd && descentRate > 0.0)
        spread.descentRate = *descentSd / descentRate;
    return spread;
}

double LaunchSiteStats::exponent() const
{
    // Fits to SondeHub tracks (tools/validation/fit_descents.py): parachutes about 0.42–0.46,
    // free-falling sondes 0.32–0.38.
    if (descentExponent)
        return *descentExponent;
    return descentRate < 7.0 ? 0.45 : 0.33;
}

QString LaunchSiteStats::descentSource() const
{
    if (descentMeasured() && descentTracks > 0)
        return QStringLiteral("measured on %1 descents up to %2").arg(descentTracks).arg(descentUntil.toString(QStringLiteral("d.M.yyyy")));
    if (descentMeasured())
        return QStringLiteral("measured on recent descents");
    return QStringLiteral("SondeHub's long-term average, possibly out of date");
}

QList<QTime> LaunchSiteStats::launchTimes() const
{
    QList<QTime> times;
    for (const QTime &t : synopticTimes)
        times << t.addSecs(-30 * 60); // wraps round midnight: 00:00 → 23:30
    std::sort(times.begin(), times.end());
    return times;
}

std::vector<LaunchSiteStats> parseLaunchSiteStats(const QByteArray &json)
{
    std::vector<LaunchSiteStats> out;
    const QJsonArray array = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        LaunchSiteStats s;
        s.id = o.value(QStringLiteral("id")).toString();
        s.name = o.value(QStringLiteral("name")).toString();
        s.position = {o.value(QStringLiteral("lat")).toDouble(), o.value(QStringLiteral("lon")).toDouble()};
        s.ascentRate = o.value(QStringLiteral("ascent")).toDouble();
        s.burstAltitude = o.value(QStringLiteral("burst")).toDouble();
        s.descentRate = o.value(QStringLiteral("descent")).toDouble();
        const auto optional = [&o](const char *key) -> std::optional<double> {
            const QJsonValue v = o.value(QLatin1String(key));
            return v.isDouble() ? std::optional(v.toDouble()) : std::nullopt;
        };
        s.ascentSd = optional("ascentSd");
        s.burstSd = optional("burstSd");
        s.descentSd = optional("descentSd");
        s.flights = o.value(QStringLiteral("flights")).toInt();
        s.descentExponent = optional("descentK");
        s.descentTracks = o.value(QStringLiteral("descentTracks")).toInt();
        s.descentUntil = QDate::fromString(o.value(QStringLiteral("descentUntil")).toString(), Qt::ISODate);
        for (const QJsonValue &t : o.value(QStringLiteral("times")).toArray()) {
            const QTime time = QTime::fromString(t.toString(), QStringLiteral("HH:mm"));
            if (time.isValid())
                s.synopticTimes << time;
        }
        // Skip entries that would not make a usable flight.
        if (s.name.isEmpty() || s.ascentRate <= 0.0 || s.burstAltitude <= 1000.0 || s.descentRate <= 0.0)
            continue;
        out.push_back(std::move(s));
    }
    return out;
}

void sortByDistance(std::vector<LaunchSiteStats> &sites, const std::vector<GeoPoint> &places)
{
    if (places.empty()) {
        std::sort(sites.begin(), sites.end(), [](const LaunchSiteStats &a, const LaunchSiteStats &b) { return a.name < b.name; });
        return;
    }
    std::vector<std::pair<double, std::size_t>> order;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        double nearest = std::numeric_limits<double>::max();
        for (const GeoPoint &p : places)
            nearest = std::min(nearest, inverse(p, sites[i].position).distance);
        order.emplace_back(nearest, i);
    }
    std::sort(order.begin(), order.end());
    std::vector<LaunchSiteStats> sorted;
    sorted.reserve(sites.size());
    for (const auto &[distance, i] : order)
        sorted.push_back(std::move(sites[i]));
    sites = std::move(sorted);
}

const LaunchSiteStats *nearestSite(const std::vector<LaunchSiteStats> &sites, GeoPoint p, double maxKm)
{
    const LaunchSiteStats *best = nullptr;
    double bestDistance = maxKm * 1000.0;
    for (const LaunchSiteStats &s : sites) {
        const double d = inverse(p, s.position).distance;
        if (d <= bestDistance) {
            bestDistance = d;
            best = &s;
        }
    }
    return best;
}

SiteBalloon siteBalloon(const LaunchSiteStats &site, const std::optional<RecentFlightStats> &recent, const SiteBalloon &base)
{
    SiteBalloon b = base;
    b.ascentRate = recent ? recent->ascentRate : site.ascentRate;
    b.burstAltitude = recent ? std::round(recent->burstAltitude / 100.0) * 100.0 : site.burstAltitude;
    b.descentRate = site.descentRate;
    b.descentExponent = site.exponent();
    if (recent) {
        b.ascentSd = recent->ascentSd;
        b.burstSd = std::round(recent->burstSd / 50.0) * 50.0;
    } else {
        b.ascentSd = site.ascentSd.value_or(b.ascentSd);
        b.burstSd = site.burstSd.value_or(b.burstSd);
    }
    if (site.descentSd && site.descentRate > 0.0)
        b.descentSd = *site.descentSd / site.descentRate;
    return b;
}

} // namespace sp
