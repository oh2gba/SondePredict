#pragma once

#include "flightprofile.h"
#include "geo.h"
#include "landingarea.h"

#include <QList>
#include <QString>

#include <optional>

namespace sp {

struct Location {
    QString name;
    GeoPoint position;
    double altitude = 0.0;            // m above MSL
    double warningDistanceKm = 30.0;  // highlight predictions landing this close (v3: "warning")
};

enum class ProxyMode { System, None, Manual };

struct NetworkSettings {
    ProxyMode proxyMode = ProxyMode::System;
    QString proxyUrl; // Manual only, e.g. http://user:pass@proxy.example.com:8080
};

struct WindSettings {
    QString apiUrl; // empty = Open-Meteo's free API
    QString apiKey;
    QString model = QStringLiteral("gfs_global");
    double gridStep = 0.25; // degrees
};

struct GeneralSettings {
    QString googleEarthPath;
    bool overwritePredictions = true; // replace an older prediction for the same launch
    bool useUtc = true;
};

// v3 "batch": predict every listed day offset × launch hour.
struct ScheduleRule {
    QString siteName;
    QString homeName;
    QList<int> days;       // 0 = today, 1 = tomorrow, ...
    QList<int> hours;      // UTC launch hours
    int minute = 0;        // launch minute past each hour
    int intervalHours = 0; // 0 = only when started by hand
    bool autoRun = false;  // run every intervalHours while the app is open
};

// Last choices in the main window's manual-run controls.
struct ManualRunSettings {
    QString siteName;
    QString homeName;
    int hour = 11;
    int minute = 15; // FMI and many others launch 45 min before the synoptic hour
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

    // A missing file gives the defaults; an unreadable or malformed one gives the defaults
    // and sets `error`.
    static AppSettings load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;

    // <user config dir>/SondePredict/sondepredict.ini
    static QString defaultPath();
};

} // namespace sp
