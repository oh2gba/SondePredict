#pragma once

#include "predictionstore.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace sp {

// GPS / phone navigation apps: landing and burst waypoints and the track (GPX 1.1).
QByteArray toGpx(const std::vector<PredictionRecord> &records);

// Writes `data` to `path`; returns an error message, or an empty string on success.
QString writeFile(const QString &path, const QByteArray &data);

// "Jokioinen 2026-10-23 11:15 UTC"
QString predictionTitle(const PredictionRecord &record);

} // namespace sp
