#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

namespace sp {

namespace {

constexpr int kFormatVersion = 1;

QString joinInts(const QList<int> &values)
{
    QStringList parts;
    for (int v : values)
        parts << QString::number(v);
    return parts.join(u',');
}

// Accepts "0,1,2" whether QSettings hands it back as a string or (unquoted) a list.
QList<int> splitInts(const QVariant &value)
{
    const QString text = value.typeId() == QMetaType::QStringList ? value.toStringList().join(u',') : value.toString();
    QList<int> values;
    for (const QString &part : text.split(u',', Qt::SkipEmptyParts)) {
        bool ok;
        const int v = part.trimmed().toInt(&ok);
        if (ok)
            values << v;
    }
    return values;
}

QString descentModelName(DescentModel model)
{
    return model == DescentModel::Legacy ? QStringLiteral("legacy") : QStringLiteral("density");
}

QString proxyModeName(ProxyMode mode)
{
    switch (mode) {
    case ProxyMode::None: return QStringLiteral("none");
    case ProxyMode::Manual: return QStringLiteral("manual");
    case ProxyMode::System: break;
    }
    return QStringLiteral("system");
}

ProxyMode proxyModeFromName(const QString &name)
{
    if (name == QLatin1String("none"))
        return ProxyMode::None;
    if (name == QLatin1String("manual"))
        return ProxyMode::Manual;
    return ProxyMode::System;
}

double readDouble(const QSettings &s, const QString &key, double fallback)
{
    bool ok;
    const double v = s.value(key).toString().toDouble(&ok);
    return ok ? v : fallback;
}

bool readBool(const QSettings &s, const QString &key, bool fallback)
{
    const QVariant v = s.value(key);
    return v.isValid() ? v.toBool() : fallback;
}

} // namespace

const Location *AppSettings::location(const QString &name) const
{
    for (const Location &l : locations) {
        if (l.name.compare(name, Qt::CaseInsensitive) == 0)
            return &l;
    }
    return nullptr;
}

Location *AppSettings::location(const QString &name)
{
    return const_cast<Location *>(std::as_const(*this).location(name));
}

FlightProfile AppSettings::profileFor(const Location *site) const
{
    FlightProfile p = profile;
    if (site && site->balloon) {
        const SiteBalloon &b = *site->balloon;
        p.ascentRate = b.ascentRate;
        p.shapedAscent = b.shapedAscent;
        p.burstAltitude = b.burstAltitude;
        p.descentModel = DescentModel::DensityScaled;
        p.seaLevelDescentRate = b.descentRate;
        p.descentExponent = b.descentExponent;
    }
    return p;
}

FlightSpread AppSettings::spreadFor(const Location *site) const
{
    FlightSpread s = spread;
    if (site && site->balloon) {
        s.ascentRate = site->balloon->ascentSd;
        s.burstAltitude = site->balloon->burstSd;
        s.descentRate = site->balloon->descentSd;
    }
    return s;
}

std::optional<QList<QTime>> parseLaunchTimes(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    QList<QTime> times;
    for (const QString &part : text.split(separators, Qt::SkipEmptyParts)) {
        QTime t = QTime::fromString(part, QStringLiteral("H:mm"));
        if (!t.isValid())
            t = QTime::fromString(part, QStringLiteral("HHmm"));
        if (!t.isValid())
            return std::nullopt;
        if (!times.contains(t))
            times << t;
    }
    std::sort(times.begin(), times.end());
    return times;
}

QString formatLaunchTimes(QList<QTime> times)
{
    std::sort(times.begin(), times.end());
    QStringList parts;
    for (const QTime &t : times)
        parts << t.toString(QStringLiteral("HH:mm"));
    return parts.join(QStringLiteral(", "));
}

QString AppSettings::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
         + QStringLiteral("/SondePredict/sondepredict.ini");
}

AppSettings AppSettings::load(const QString &path, QString *error)
{
    AppSettings out;
    if (!QFileInfo::exists(path))
        return out;

    QSettings s(path, QSettings::IniFormat);
    if (s.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("cannot read settings file %1").arg(path);
        return out;
    }

    const int count = s.beginReadArray(QStringLiteral("locations"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        Location l;
        l.name = s.value(QStringLiteral("name")).toString();
        l.position.lat = readDouble(s, QStringLiteral("lat"), 0.0);
        l.position.lon = readDouble(s, QStringLiteral("lon"), 0.0);
        l.altitude = readDouble(s, QStringLiteral("alt"), 0.0);
        l.warningDistanceKm = readDouble(s, QStringLiteral("warning_km"), l.warningDistanceKm);
        if (s.value(QStringLiteral("balloon")).toString() == QLatin1String("own")) {
            SiteBalloon b;
            b.ascentRate = readDouble(s, QStringLiteral("ascent_rate"), b.ascentRate);
            b.shapedAscent = readBool(s, QStringLiteral("shaped_ascent"), b.shapedAscent);
            b.burstAltitude = readDouble(s, QStringLiteral("burst_altitude"), b.burstAltitude);
            b.descentRate = readDouble(s, QStringLiteral("descent_rate"), b.descentRate);
            b.descentExponent = readDouble(s, QStringLiteral("descent_exponent"), b.descentExponent);
            b.ascentSd = readDouble(s, QStringLiteral("ascent_sigma"), b.ascentSd);
            b.burstSd = readDouble(s, QStringLiteral("burst_sigma"), b.burstSd);
            b.descentSd = readDouble(s, QStringLiteral("descent_sigma"), b.descentSd);
            l.balloon = b;
        }
        if (!l.name.isEmpty())
            out.locations << l;
    }
    s.endArray();

    FlightProfile &p = out.profile;
    s.beginGroup(QStringLiteral("balloon"));
    p.ascentRate = readDouble(s, QStringLiteral("ascent_rate"), p.ascentRate);
    p.shapedAscent = s.value(QStringLiteral("shaped_ascent"), p.shapedAscent).toBool();
    p.burstAltitude = readDouble(s, QStringLiteral("burst_altitude"), p.burstAltitude);
    p.descentModel = s.value(QStringLiteral("descent_model")).toString() == QLatin1String("legacy")
        ? DescentModel::Legacy : DescentModel::DensityScaled;
    p.seaLevelDescentRate = readDouble(s, QStringLiteral("descent_rate"), p.seaLevelDescentRate);
    p.descentExponent = readDouble(s, QStringLiteral("descent_exponent"), p.descentExponent);
    p.legacyDescentRate = readDouble(s, QStringLiteral("legacy_descent_rate"), p.legacyDescentRate);
    p.legacyDescentGain = readDouble(s, QStringLiteral("legacy_descent_gain"), p.legacyDescentGain);
    s.endGroup();

    s.beginGroup(QStringLiteral("landing_area"));
    out.spread.enabled = readBool(s, QStringLiteral("enabled"), out.spread.enabled);
    out.spread.ascentRate = readDouble(s, QStringLiteral("ascent_sigma"), out.spread.ascentRate);
    out.spread.burstAltitude = readDouble(s, QStringLiteral("burst_sigma"), out.spread.burstAltitude);
    out.spread.descentRate = readDouble(s, QStringLiteral("descent_sigma"), out.spread.descentRate);
    out.spread.samples = s.value(QStringLiteral("samples"), out.spread.samples).toInt();
    s.endGroup();

    s.beginGroup(QStringLiteral("network"));
    out.network.proxyMode = proxyModeFromName(s.value(QStringLiteral("proxy_mode")).toString());
    out.network.proxyUrl = s.value(QStringLiteral("proxy_url")).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("wind"));
    out.wind.useMeps = s.value(QStringLiteral("use_meps"), out.wind.useMeps).toBool();
    out.wind.averageMeps = s.value(QStringLiteral("average_meps"), out.wind.averageMeps).toBool();
    out.wind.apiUrl = s.value(QStringLiteral("api_url")).toString();
    out.wind.apiKey = s.value(QStringLiteral("api_key")).toString();
    out.wind.model = s.value(QStringLiteral("model"), out.wind.model).toString();
    out.wind.gridStep = readDouble(s, QStringLiteral("grid_step"), out.wind.gridStep);
    s.endGroup();

    // Written as [options]. Older files used "general", which QSettings writes as [%General]
    // (because [General] is its own section) and reads back as "General".
    QString optionsGroup = QStringLiteral("options");
    if (!s.childGroups().contains(optionsGroup)) {
        for (const QString &group : s.childGroups()) {
            if (group.compare(QLatin1String("general"), Qt::CaseInsensitive) == 0)
                optionsGroup = group;
        }
    }
    s.beginGroup(optionsGroup);
    out.general.overwritePredictions = readBool(s, QStringLiteral("overwrite_predictions"), out.general.overwritePredictions);
    out.general.useUtc = readBool(s, QStringLiteral("use_utc"), out.general.useUtc);
    s.endGroup();

    if (s.childGroups().contains(QStringLiteral("schedule"))) {
        s.beginGroup(QStringLiteral("schedule"));
        ScheduleRule rule;
        rule.homeName = s.value(QStringLiteral("home")).toString();
        rule.days = splitInts(s.value(QStringLiteral("days")));
        rule.intervalHours = s.value(QStringLiteral("interval_hours"), 0).toInt();
        rule.autoRun = readBool(s, QStringLiteral("auto_run"), false);
        const int sites = s.beginReadArray(QStringLiteral("sites"));
        for (int i = 0; i < sites; ++i) {
            s.setArrayIndex(i);
            BatchSite site;
            site.siteName = s.value(QStringLiteral("site")).toString();
            site.times = parseLaunchTimes(s.value(QStringLiteral("times")).toStringList().join(u',')).value_or(QList<QTime>{});
            if (!site.siteName.isEmpty())
                rule.sites << site;
        }
        s.endArray();
        // Before 5.1 a batch had one site and launch hours with one minute for all.
        if (sites == 0 && s.contains(QStringLiteral("site"))) {
            BatchSite site;
            site.siteName = s.value(QStringLiteral("site")).toString();
            const int minute = std::clamp(s.value(QStringLiteral("minute"), 0).toInt(), 0, 59);
            for (int hour : splitInts(s.value(QStringLiteral("hours"))))
                site.times << QTime(std::clamp(hour, 0, 23), minute);
            if (!site.siteName.isEmpty())
                rule.sites << site;
        }
        out.schedule = rule;
        s.endGroup();
    }

    s.beginGroup(QStringLiteral("manual_run"));
    out.manualRun.siteName = s.value(QStringLiteral("site")).toString();
    out.manualRun.homeName = s.value(QStringLiteral("home")).toString();
    out.manualRun.hour = s.value(QStringLiteral("hour"), out.manualRun.hour).toInt();
    out.manualRun.minute = s.value(QStringLiteral("minute"), out.manualRun.minute).toInt();
    s.endGroup();

    return out;
}

bool AppSettings::save(const QString &path, QString *error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings s(path, QSettings::IniFormat);
    s.clear();
    s.setValue(QStringLiteral("format"), kFormatVersion);

    s.beginWriteArray(QStringLiteral("locations"), static_cast<int>(locations.size()));
    for (int i = 0; i < locations.size(); ++i) {
        const Location &l = locations[i];
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), l.name);
        s.setValue(QStringLiteral("lat"), QString::number(l.position.lat, 'f', 6));
        s.setValue(QStringLiteral("lon"), QString::number(l.position.lon, 'f', 6));
        s.setValue(QStringLiteral("alt"), QString::number(l.altitude, 'g', 10));
        s.setValue(QStringLiteral("warning_km"), QString::number(l.warningDistanceKm, 'g', 10));
        if (const auto &b = l.balloon) {
            s.setValue(QStringLiteral("balloon"), QStringLiteral("own"));
            s.setValue(QStringLiteral("ascent_rate"), QString::number(b->ascentRate, 'g', 10));
            s.setValue(QStringLiteral("shaped_ascent"), b->shapedAscent);
            s.setValue(QStringLiteral("burst_altitude"), QString::number(b->burstAltitude, 'g', 10));
            s.setValue(QStringLiteral("descent_rate"), QString::number(b->descentRate, 'g', 10));
            s.setValue(QStringLiteral("descent_exponent"), QString::number(b->descentExponent, 'g', 10));
            s.setValue(QStringLiteral("ascent_sigma"), QString::number(b->ascentSd, 'g', 10));
            s.setValue(QStringLiteral("burst_sigma"), QString::number(b->burstSd, 'g', 10));
            s.setValue(QStringLiteral("descent_sigma"), QString::number(b->descentSd, 'g', 10));
        }
    }
    s.endArray();

    s.beginGroup(QStringLiteral("balloon"));
    s.setValue(QStringLiteral("ascent_rate"), QString::number(profile.ascentRate, 'g', 10));
    s.setValue(QStringLiteral("shaped_ascent"), profile.shapedAscent);
    s.setValue(QStringLiteral("burst_altitude"), QString::number(profile.burstAltitude, 'g', 10));
    s.setValue(QStringLiteral("descent_model"), descentModelName(profile.descentModel));
    s.setValue(QStringLiteral("descent_rate"), QString::number(profile.seaLevelDescentRate, 'g', 10));
    s.setValue(QStringLiteral("descent_exponent"), QString::number(profile.descentExponent, 'g', 10));
    s.setValue(QStringLiteral("legacy_descent_rate"), QString::number(profile.legacyDescentRate, 'g', 10));
    s.setValue(QStringLiteral("legacy_descent_gain"), QString::number(profile.legacyDescentGain, 'g', 10));
    s.endGroup();

    s.beginGroup(QStringLiteral("landing_area"));
    s.setValue(QStringLiteral("enabled"), spread.enabled);
    s.setValue(QStringLiteral("ascent_sigma"), QString::number(spread.ascentRate, 'g', 10));
    s.setValue(QStringLiteral("burst_sigma"), QString::number(spread.burstAltitude, 'g', 10));
    s.setValue(QStringLiteral("descent_sigma"), QString::number(spread.descentRate, 'g', 10));
    s.setValue(QStringLiteral("samples"), spread.samples);
    s.endGroup();

    s.beginGroup(QStringLiteral("network"));
    s.setValue(QStringLiteral("proxy_mode"), proxyModeName(network.proxyMode));
    s.setValue(QStringLiteral("proxy_url"), network.proxyUrl);
    s.endGroup();

    s.beginGroup(QStringLiteral("wind"));
    s.setValue(QStringLiteral("use_meps"), wind.useMeps);
    s.setValue(QStringLiteral("average_meps"), wind.averageMeps);
    s.setValue(QStringLiteral("api_url"), wind.apiUrl);
    s.setValue(QStringLiteral("api_key"), wind.apiKey);
    s.setValue(QStringLiteral("model"), wind.model);
    s.setValue(QStringLiteral("grid_step"), QString::number(wind.gridStep, 'g', 10));
    s.endGroup();

    s.beginGroup(QStringLiteral("options"));
    s.setValue(QStringLiteral("overwrite_predictions"), general.overwritePredictions);
    s.setValue(QStringLiteral("use_utc"), general.useUtc);
    s.endGroup();

    if (schedule) {
        s.beginGroup(QStringLiteral("schedule"));
        s.setValue(QStringLiteral("home"), schedule->homeName);
        s.setValue(QStringLiteral("days"), joinInts(schedule->days));
        s.setValue(QStringLiteral("interval_hours"), schedule->intervalHours);
        s.setValue(QStringLiteral("auto_run"), schedule->autoRun);
        s.beginWriteArray(QStringLiteral("sites"), int(schedule->sites.size()));
        for (int i = 0; i < schedule->sites.size(); ++i) {
            s.setArrayIndex(i);
            s.setValue(QStringLiteral("site"), schedule->sites[i].siteName);
            s.setValue(QStringLiteral("times"), formatLaunchTimes(schedule->sites[i].times));
        }
        s.endArray();
        s.endGroup();
    }

    s.beginGroup(QStringLiteral("manual_run"));
    s.setValue(QStringLiteral("site"), manualRun.siteName);
    s.setValue(QStringLiteral("home"), manualRun.homeName);
    s.setValue(QStringLiteral("hour"), manualRun.hour);
    s.setValue(QStringLiteral("minute"), manualRun.minute);
    s.endGroup();

    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("cannot write settings file %1").arg(path);
        return false;
    }
    return true;
}

} // namespace sp
