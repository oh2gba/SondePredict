#include "mapwidget.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGeoCoordinate>
#include <QGeoRectangle>
#include <QQmlContext>
#include <QStandardPaths>
#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>

namespace {

QGeoCoordinate coordinate(sp::GeoPoint p)
{
    return {p.lat, p.lon};
}

QVariantMap marker(const QString &kind, sp::GeoPoint p, const QString &label, bool current)
{
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("coordinate"), QVariant::fromValue(coordinate(p))},
            {QStringLiteral("label"), label},
            {QStringLiteral("current"), current}};
}

QString hhmm(double unixTime)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(unixTime), QTimeZone::UTC).toString(QStringLiteral("HH:mm"));
}

} // namespace

QString TrackMapModel::userAgent() const
{
    // The OSM tile usage policy asks applications to identify themselves.
    return QStringLiteral("SondePredict/%1 (radiosonde landing predictor)").arg(QCoreApplication::applicationVersion());
}

QString TrackMapModel::cacheDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles");
}

void TrackMapModel::setPredictions(const std::vector<sp::PredictionRecord> &records)
{
    m_lines.clear();
    m_markers.clear();
    m_areas.clear();
    QGeoRectangle bounds;

    // Draw the current (first) prediction last so it ends up on top.
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const sp::PredictionRecord &rec = *it;
        const sp::SimulationResult &r = rec.prediction.result;
        if (r.track.empty())
            continue;
        const bool current = (it + 1 == records.rend());

        QVariantList ascent, descent;
        for (const sp::TrackPoint &p : r.track) {
            const QVariant c = QVariant::fromValue(coordinate(p.position));
            (p.phase == sp::FlightPhase::Ascent ? ascent : descent) << c;
            if (bounds.isValid())
                bounds.extendRectangle(coordinate(p.position));
            else
                bounds = QGeoRectangle(coordinate(p.position), coordinate(p.position));
        }
        if (!descent.isEmpty() && !ascent.isEmpty())
            descent.prepend(ascent.last()); // join the two lines at the burst
        m_lines << QVariantMap{{QStringLiteral("path"), ascent}, {QStringLiteral("color"), QStringLiteral("#1e88e5")},
                               {QStringLiteral("current"), current}};
        m_lines << QVariantMap{{QStringLiteral("path"), descent}, {QStringLiteral("color"), QStringLiteral("#e53935")},
                               {QStringLiteral("current"), current}};

        if (const auto &area = rec.prediction.landingArea) {
            QVariantList outline;
            for (const sp::GeoPoint &p : area->outline(0.95)) {
                outline << QVariant::fromValue(coordinate(p));
                bounds.extendRectangle(coordinate(p));
            }
            m_areas << QVariantMap{{QStringLiteral("path"), outline}, {QStringLiteral("current"), current}};
        }
        m_markers << marker(QStringLiteral("launch"), r.track.front().position, current ? rec.siteName : QString(), current);
        if (r.burst)
            m_markers << marker(QStringLiteral("burst"), r.burst->position, current ? tr("Burst %1").arg(hhmm(r.burst->time)) : QString(), current);
        if (r.landing)
            m_markers << marker(QStringLiteral("landing"), r.landing->position, hhmm(r.landing->time), current);
        if (rec.home) {
            m_markers << marker(QStringLiteral("home"), *rec.home, current ? rec.homeName : QString(), current);
            // Zoom out to include your location only when it is near enough to matter.
            if (r.landing && sp::inverse(*rec.home, r.landing->position).distance < 300000.0)
                bounds.extendRectangle(coordinate(*rec.home));
        }
    }

    emit changed();
    if (bounds.isValid())
        emit fitRequested(bounds);
}

void TrackMapModel::setLocations(const sp::Location *site, const sp::Location *home)
{
    m_lines.clear();
    m_markers.clear();
    m_areas.clear();
    QGeoRectangle bounds;
    for (const auto &[location, kind] : {std::pair{site, QStringLiteral("site")}, std::pair{home, QStringLiteral("home")}}) {
        if (!location)
            continue;
        m_markers << marker(kind, location->position, location->name, true);
        const QGeoCoordinate c = coordinate(location->position);
        if (bounds.isValid())
            bounds.extendRectangle(c);
        else
            bounds = QGeoRectangle(c, c);
    }
    emit changed();
    if (bounds.isValid()) {
        // Leave room around one or two points instead of zooming in to street level.
        bounds.setWidth(std::max(bounds.width(), 2.0));
        bounds.setHeight(std::max(bounds.height(), 1.0));
        emit fitRequested(bounds);
    }
}

MapWidget::MapWidget(QWidget *parent)
    : QQuickWidget(parent)
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    rootContext()->setContextProperty(QStringLiteral("trackModel"), &m_model);
    setSource(QUrl(QStringLiteral("qrc:/qml/TrackMap.qml")));
    setMinimumSize(300, 200);
}

MapWidget::~MapWidget()
{
    // Unload the QML before m_model goes away, so no binding reads a dead object.
    setSource(QUrl());
}
