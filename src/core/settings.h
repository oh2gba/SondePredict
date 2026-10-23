#pragma once

#include "flightprofile.h"
#include "geo.h"
#include "landingarea.h"

#include <QList>
#include <QString>
#include <QTime>

#include <optional>

namespace sp {

// Balloon values for launches from one site, when its balloons differ from the general ones
// (stations use different balloons, fillings and parachutes).
struct SiteBalloon {
    double ascentRate = 4.9;        // m/s, average to burst
    bool shapedAscent = true;
    double burstAltitude = 34300;   // m
    double descentRate = 11.7;      // m/s at sea level (density-scaled descent)
    double descentExponent = 0.33;
    // Variation for the landing area (one standard deviation).
    double ascentSd = 0.35;         // m/s
    double burstSd = 2500;          // m
    double descentSd = 0.18;        // fraction
};

struct Location {
    QString name;
    GeoPoint position;
    double altitude = 0.0;            // m above MSL
    double warningDistanceKm = 30.0;  // highlight predictions landing this close (v3: "warning")
    std::optional<SiteBalloon> balloon; // launches from here; empty = the general balloon settings
};

enum class ProxyMode { System, None, Manual };

struct NetworkSettings {
    ProxyMode proxyMode = ProxyMode::System;
    QString proxyUrl; // Manual only, e.g. http://user:pass@proxy.example.com:8080
};

struct WindSettings {
    bool useMeps = true;     // MET Norway's MEPS inside its area (Nordic countries, Baltic), up to 66 h ahead
    bool averageMeps = true; // ...averaged with the global model there (more accurate; uses Open-Meteo quota)
    QString apiUrl;      // empty = Open-Meteo's free API
    QString apiKey;
    QString model = QStringLiteral("ecmwf_ifs025"); // global model, via Open-Meteo
    double gridStep = 0.25; // degrees
};

struct GeneralSettings {
    bool overwritePredictions = true; // replace an older prediction for the same launch
    bool useUtc = true;
    bool checkForUpdates = true; // show when a newer version is out
    QString updateUrl;           // empty = UpdateCheck::defaultUrl; for testing against another server
};

// One launch site of a batch and when it launches.
struct BatchSite {
    QString siteName;
    QList<QTime> times; // UTC, e.g. 05:30, 11:30, 23:30 (FMI launches about 30 min before the synoptic hour)
};

// A batch (v3 "batch"): predict every listed day × each site's launch times.
struct ScheduleRule {
    QList<BatchSite> sites;
    QString homeName;
    QList<int> days;       // 0 = today, 1 = tomorrow, ...
    int intervalHours = 0; // 0 = never: the batch runs only when started from the main window
    bool autoRun = false;  // run every intervalHours while the app is open
};

// "05:30, 11:30 23:30" → times; nothing if any part is not a time. Accepts H:MM, HH:MM and HHMM.
std::optional<QList<QTime>> parseLaunchTimes(const QString &text);
// Sorted, "05:30, 11:30, 23:30".
QString formatLaunchTimes(QList<QTime> times);

// Last choices in the main window's manual-run controls.
struct ManualRunSettings {
    QString siteName;
    QString homeName;
    int hour = 11;
    int minute = 30; // FMI launches 30 min before the synoptic hour (e.g. 11:30 for 12 UTC)
};

struct AppSettings {
    QList<Location> locations;
    FlightProfile profile;
    FlightSpread spread; // landing-area estimate
    NetworkSettings network;
    WindSettings wind;
    GeneralSettings general;
    std::optional<ScheduleRule> schedule;
    ManualRunSettings manualRun;

    const Location *location(const QString &name) const;
    Location *location(const QString &name);

    // The balloon and landing-area values for launches from `site`: its own if it has them,
    // otherwise the general ones. `site` may be null.
    FlightProfile profileFor(const Location *site) const;
    FlightSpread spreadFor(const Location *site) const;

    // A missing file gives the defaults; an unreadable or malformed one gives the defaults
    // and sets `error`.
    static AppSettings load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;

    // <user config dir>/SondePredict/sondepredict.ini
    static QString defaultPath();
};

} // namespace sp
