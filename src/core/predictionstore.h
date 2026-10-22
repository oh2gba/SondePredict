#pragma once

#include "predictor.h"

#include <QList>
#include <QString>

#include <optional>
#include <vector>

namespace sp {

// A stored prediction: the calculation plus where it was for (the v3 "track" row).
struct PredictionRecord {
    qint64 id = 0;        // assigned by the store
    qint64 createdAt = 0; // unix seconds
    QString siteName;     // launch site
    QString homeName;     // "your location", may be empty
    std::optional<GeoPoint> home;
    bool manual = true;   // false for batch/scheduled runs
    QString model;        // wind model, e.g. gfs_global
    Prediction prediction;
};

// What the prediction list needs, without the track points.
struct PredictionSummary {
    qint64 id = 0;
    qint64 createdAt = 0;
    qint64 launchTime = 0;
    QString siteName;
    QString homeName;
    bool manual = true;
    GeoPoint landing;
    double landingTime = 0.0;
    std::optional<double> distanceFromHome; // m
    std::optional<double> bearingFromHome;  // degrees
};

// SQLite storage for predictions and their tracks. Not thread-safe: use one store per thread.
class PredictionStore {
public:
    PredictionStore();
    ~PredictionStore();
    PredictionStore(const PredictionStore &) = delete;
    PredictionStore &operator=(const PredictionStore &) = delete;

    // Opens (creating and migrating as needed) the database file.
    bool open(const QString &path);
    bool isOpen() const;
    void close();
    QString lastError() const { return m_error; }
    int schemaVersion() const;

    // Saves a landed prediction and returns its id, or 0 on failure. With `replaceExisting`
    // (v3's "overwrite predictions"), older predictions for the same launch time, site and
    // home are deleted first.
    qint64 save(const PredictionRecord &record, bool replaceExisting);

    std::vector<PredictionSummary> list() const; // newest launch first
    std::optional<PredictionRecord> load(qint64 id) const;
    bool remove(const QList<qint64> &ids);

    // <user data dir>/SondePredict/predictions.sqlite
    static QString defaultPath();

    static constexpr int kSchemaVersion = 2;

private:
    bool migrate();

    QString m_connection;
    mutable QString m_error;
};

} // namespace sp
