#include "scene3d.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <numbers>

// ---------------------------------------------------------------- geometries

void ScaledGeometry::setPoints(const QVariantList &points)
{
    m_variant = points;
    m_points.clear();
    m_points.reserve(static_cast<std::size_t>(points.size()));
    for (const QVariant &p : points)
        m_points.push_back(p.value<QVector3D>());
    rebuild();
    emit pointsChanged();
}

void ScaledGeometry::setVerticalScale(float scale)
{
    if (qFuzzyCompare(scale, m_scale))
        return;
    m_scale = scale;
    rebuild();
    emit verticalScaleChanged();
}

std::vector<QVector3D> ScaledGeometry::scaledPoints() const
{
    std::vector<QVector3D> out = m_points;
    for (QVector3D &p : out)
        p.setY(p.y() * m_scale);
    return out;
}

void ScaledGeometry::upload(const std::vector<float> &vertices)
{
    clear();
    setStride(6 * sizeof(float));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (std::size_t i = 0; i + 5 < vertices.size(); i += 6) {
        const QVector3D v(vertices[i], vertices[i + 1], vertices[i + 2]);
        lo = QVector3D(std::min(lo.x(), v.x()), std::min(lo.y(), v.y()), std::min(lo.z(), v.z()));
        hi = QVector3D(std::max(hi.x(), v.x()), std::max(hi.y(), v.y()), std::max(hi.z(), v.z()));
    }
    if (vertices.empty())
        lo = hi = QVector3D();
    setVertexData(QByteArray(reinterpret_cast<const char *>(vertices.data()), qsizetype(vertices.size() * sizeof(float))));
    setBounds(lo, hi);
    update();
}

namespace {

void addVertex(std::vector<float> &v, QVector3D p, QVector3D n)
{
    v.insert(v.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z()});
}

void addQuad(std::vector<float> &v, QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n)
{
    for (const QVector3D &p : {a, b, c, a, c, d})
        addVertex(v, p, n);
}

} // namespace

void TubeGeometry::setRadius(float radius)
{
    if (qFuzzyCompare(radius, m_radius))
        return;
    m_radius = radius;
    rebuild();
    emit radiusChanged();
}

void TubeGeometry::rebuild()
{
    const std::vector<QVector3D> pts = scaledPoints();
    std::vector<float> v;
    v.reserve(pts.size() * 24 * 6);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const QVector3D d = (pts[i] - pts[i - 1]);
        if (d.lengthSquared() < 1e-8f)
            continue;
        const QVector3D dir = d.normalized();
        QVector3D a = QVector3D::crossProduct(dir, QVector3D(0, 1, 0));
        if (a.lengthSquared() < 1e-6f)
            a = QVector3D(1, 0, 0);
        a.normalize();
        const QVector3D b = QVector3D::crossProduct(a, dir).normalized();
        const QVector3D corners[4] = {(a + b) * radius(), (b - a) * radius(), (-a - b) * radius(), (a - b) * radius()};
        for (int k = 0; k < 4; ++k) {
            const QVector3D c0 = corners[k], c1 = corners[(k + 1) % 4];
            const QVector3D n = (c0 + c1).normalized();
            addQuad(v, pts[i - 1] + c0, pts[i] + c0, pts[i] + c1, pts[i - 1] + c1, n);
        }
    }
    upload(v);
}

void CurtainGeometry::rebuild()
{
    const std::vector<QVector3D> pts = scaledPoints();
    std::vector<float> v;
    v.reserve(pts.size() * 6 * 6);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const QVector3D p0 = pts[i - 1], p1 = pts[i];
        const QVector3D g0(p0.x(), 0, p0.z()), g1(p1.x(), 0, p1.z());
        QVector3D n = QVector3D::crossProduct(p1 - p0, QVector3D(0, 1, 0));
        n = n.lengthSquared() > 1e-8f ? n.normalized() : QVector3D(1, 0, 0);
        addQuad(v, g0, p0, p1, g1, n);
    }
    upload(v);
}

void AreaGeometry::rebuild()
{
    const std::vector<QVector3D> pts = scaledPoints();
    std::vector<float> v;
    if (pts.size() >= 3) {
        QVector3D centre;
        for (const QVector3D &p : pts)
            centre += p;
        centre /= float(pts.size());
        const QVector3D up(0, 1, 0);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            addVertex(v, centre, up);
            addVertex(v, pts[i - 1], up);
            addVertex(v, pts[i], up);
        }
    }
    upload(v);
}

// ---------------------------------------------------------------- model

Scene3DModel::Scene3DModel(QObject *parent)
    : QObject(parent)
{
    connect(&m_ground, &GroundTexture::ready, this, [this](const QUrl &image, const GeoBox &box) {
        if (!(box == m_box))
            return; // an older request
        m_groundImage = image;
        emit groundImageChanged();
    });
}

QVector3D Scene3DModel::toScene(sp::GeoPoint p, double altitude) const
{
    const double east = sp::normalizeLon(p.lon - m_centre.lon) * m_metresPerDegLon;
    const double north = (p.lat - m_centre.lat) * m_metresPerDegLat;
    return QVector3D(float(east / kMetresPerSceneUnit), float(altitude / kMetresPerSceneUnit), float(-north / kMetresPerSceneUnit));
}

void Scene3DModel::setPredictions(const std::vector<sp::PredictionRecord> &records)
{
    m_tracks.clear();
    m_markers.clear();
    m_areas.clear();

    // Box around all tracks and landing areas, plus a margin.
    GeoBox box{90, 180, -90, -180};
    double maxAlt = 0;
    const auto extend = [&box](sp::GeoPoint p) {
        box.south = std::min(box.south, p.lat);
        box.north = std::max(box.north, p.lat);
        box.west = std::min(box.west, p.lon);
        box.east = std::max(box.east, p.lon);
    };
    for (const sp::PredictionRecord &rec : records) {
        for (const sp::TrackPoint &p : rec.prediction.result.track) {
            extend(p.position);
            maxAlt = std::max(maxAlt, p.altitude);
        }
        if (const auto &area = rec.prediction.landingArea) {
            for (const sp::GeoPoint &p : area->outline(0.95, 36))
                extend(p);
        }
    }
    if (!box.isValid()) {
        m_box = {};
        emit changed();
        return;
    }
    // Pad the box and make it roughly square (in metres), at least 50 km across, so the
    // track has some surroundings.
    const double midLatRad = (box.north + box.south) / 2.0 * std::numbers::pi / 180.0;
    const double lonScale = std::max(std::cos(midLatRad), 0.1); // degrees of longitude per degree of latitude, in metres
    double halfLat = (box.north - box.south) / 2.0 * 1.25;
    double halfLon = (box.east - box.west) / 2.0 * 1.25;
    const double half = std::max({halfLat, halfLon * lonScale, 0.225}); // 0.225° ≈ 25 km
    halfLat = std::max(halfLat, half * 0.8);
    halfLon = std::max(halfLon, half * 0.8 / lonScale);
    const sp::GeoPoint mid{(box.north + box.south) / 2.0, (box.east + box.west) / 2.0};
    box = {mid.lat - halfLat, mid.lon - halfLon, mid.lat + halfLat, mid.lon + halfLon};

    m_box = box;
    m_centre = {(box.north + box.south) / 2.0, (box.east + box.west) / 2.0};
    const sp::GeoPoint n = sp::offset(m_centre, 1000.0, 0.0), e = sp::offset(m_centre, 0.0, 1000.0);
    m_metresPerDegLat = 1000.0 / (n.lat - m_centre.lat);
    m_metresPerDegLon = 1000.0 / sp::normalizeLon(e.lon - m_centre.lon);
    m_groundSize = QSizeF((box.east - box.west) * m_metresPerDegLon / kMetresPerSceneUnit,
                          (box.north - box.south) * m_metresPerDegLat / kMetresPerSceneUnit);
    m_sceneRadius = float(std::max({m_groundSize.width(), m_groundSize.height(), maxAlt / kMetresPerSceneUnit}) / 2.0);
    m_maxAltitude = float(maxAlt / kMetresPerSceneUnit);

    const auto marker = [this](const QString &kind, sp::GeoPoint p, double altitude, bool current) {
        m_markers << QVariantMap{{QStringLiteral("kind"), kind},
                                 {QStringLiteral("position"), QVariant::fromValue(toScene(p, altitude))},
                                 {QStringLiteral("current"), current}};
    };

    for (std::size_t i = 0; i < records.size(); ++i) {
        const sp::PredictionRecord &rec = records[i];
        const sp::SimulationResult &r = rec.prediction.result;
        if (r.track.empty())
            continue;
        const bool current = i == 0;
        QVariantList ascent, descent;
        for (const sp::TrackPoint &p : r.track)
            (p.phase == sp::FlightPhase::Ascent ? ascent : descent) << QVariant::fromValue(toScene(p.position, p.altitude));
        if (!ascent.isEmpty() && !descent.isEmpty())
            descent.prepend(ascent.last());
        m_tracks << QVariantMap{{QStringLiteral("ascent"), ascent}, {QStringLiteral("descent"), descent},
                                {QStringLiteral("current"), current}};

        marker(QStringLiteral("launch"), r.track.front().position, r.track.front().altitude, current);
        if (r.burst)
            marker(QStringLiteral("burst"), r.burst->position, r.burst->altitude, current);
        if (r.landing)
            marker(QStringLiteral("landing"), r.landing->position, r.landing->altitude, current);
        if (rec.home && box.south <= rec.home->lat && rec.home->lat <= box.north
            && box.west <= rec.home->lon && rec.home->lon <= box.east) {
            marker(QStringLiteral("home"), *rec.home, 0.0, current);
        }
        if (const auto &area = rec.prediction.landingArea) {
            QVariantList outline;
            for (const sp::GeoPoint &p : area->outline(0.95, 72))
                outline << QVariant::fromValue(toScene(p, 0.0));
            m_areas << QVariantMap{{QStringLiteral("outline"), outline}, {QStringLiteral("current"), current}};
        }
    }

    // Look at the current flight from the side: perpendicular to launch → landing, from the
    // more southerly side, turned 20° for some depth.
    m_viewYaw = -20.0f;
    if (!records.empty() && !records.front().prediction.result.track.empty() && records.front().prediction.result.landing) {
        const auto &r = records.front().prediction.result;
        const sp::Geodesic g = sp::inverse(r.track.front().position, r.landing->position);
        if (g.distance > 5000.0) {
            const auto towardsSouth = [](double b) { return std::abs(std::remainder(b - 180.0, 360.0)); };
            const double side1 = g.initialBearing + 90.0, side2 = g.initialBearing - 90.0;
            const double cameraBearing = (towardsSouth(side1) <= towardsSouth(side2) ? side1 : side2) + 20.0;
            m_viewYaw = float(std::remainder(180.0 - cameraBearing, 360.0));
        }
    }

    emit changed();
    emit viewReset();
    m_groundImage = QUrl();
    emit groundImageChanged();
    if (m_fetchGround)
        m_ground.request(box);
}

// ---------------------------------------------------------------- widget

Scene3DWidget::Scene3DWidget(QWidget *parent)
    : QQuickWidget(parent)
{
    static const bool registered = [] {
        qmlRegisterType<TubeGeometry>("SondePredict", 1, 0, "TubeGeometry");
        qmlRegisterType<CurtainGeometry>("SondePredict", 1, 0, "CurtainGeometry");
        qmlRegisterType<AreaGeometry>("SondePredict", 1, 0, "AreaGeometry");
        return true;
    }();
    Q_UNUSED(registered);

    setResizeMode(QQuickWidget::SizeRootObjectToView);
    rootContext()->setContextProperty(QStringLiteral("scene3d"), &m_model);
    setSource(QUrl(QStringLiteral("qrc:/qml/Track3D.qml")));
    setMinimumSize(300, 200);
}

Scene3DWidget::~Scene3DWidget()
{
    setSource(QUrl()); // unload the QML before m_model goes away
}
