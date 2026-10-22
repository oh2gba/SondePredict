#pragma once

#include "groundtexture.h"
#include "predictionstore.h"

#include <QQuick3DGeometry>
#include <QQuickWidget>
#include <QSizeF>
#include <QUrl>
#include <QVariantList>
#include <QVector3D>

#include <vector>

// Scene coordinates: x east, y up, z south, in units of 100 m. Altitudes are stored
// unexaggerated; geometries and markers multiply y by the view's vertical scale.
constexpr double kMetresPerSceneUnit = 100.0;

// Shared by the geometries below: a list of points (QVector3D, from QML) and a vertical scale.
class ScaledGeometry : public QQuick3DGeometry {
    Q_OBJECT
    Q_PROPERTY(QVariantList points READ points WRITE setPoints NOTIFY pointsChanged)
    Q_PROPERTY(float verticalScale READ verticalScale WRITE setVerticalScale NOTIFY verticalScaleChanged)
public:
    using QQuick3DGeometry::QQuick3DGeometry;

    QVariantList points() const { return m_variant; }
    void setPoints(const QVariantList &points);
    float verticalScale() const { return m_scale; }
    void setVerticalScale(float scale);

signals:
    void pointsChanged();
    void verticalScaleChanged();

protected:
    virtual void rebuild() = 0;
    std::vector<QVector3D> scaledPoints() const;
    // Uploads position+normal triangles and their bounds.
    void upload(const std::vector<float> &vertices);

private:
    QVariantList m_variant;
    std::vector<QVector3D> m_points;
    float m_scale = 1.0f;
};

// A square tube along the points: the flight track.
class TubeGeometry : public ScaledGeometry {
    Q_OBJECT
    Q_PROPERTY(float radius READ radius WRITE setRadius NOTIFY radiusChanged)
public:
    using ScaledGeometry::ScaledGeometry;
    float radius() const { return m_radius; }
    void setRadius(float radius);
signals:
    void radiusChanged();
protected:
    void rebuild() override;
private:
    float m_radius = 1.0f;
};

// A vertical wall from the ground (y = 0) up to the points: the curtain under the track.
class CurtainGeometry : public ScaledGeometry {
    Q_OBJECT
public:
    using ScaledGeometry::ScaledGeometry;
protected:
    void rebuild() override;
};

// A flat filled polygon slightly above the ground: the landing area.
class AreaGeometry : public ScaledGeometry {
    Q_OBJECT
public:
    using ScaledGeometry::ScaledGeometry;
protected:
    void rebuild() override;
};

// The data behind Track3D.qml.
class Scene3DModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed)
    Q_PROPERTY(QVariantList areas READ areas NOTIFY changed)
    Q_PROPERTY(QSizeF groundSize READ groundSize NOTIFY changed)
    Q_PROPERTY(float sceneRadius READ sceneRadius NOTIFY changed)
    Q_PROPERTY(float maxAltitude READ maxAltitude NOTIFY changed)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(float viewYaw READ viewYaw NOTIFY changed)
    Q_PROPERTY(QUrl groundImage READ groundImage NOTIFY groundImageChanged)
public:
    explicit Scene3DModel(QObject *parent = nullptr);

    // The first record is the current one. Starts fetching the ground image.
    void setPredictions(const std::vector<sp::PredictionRecord> &records);

    QVariantList tracks() const { return m_tracks; }
    QVariantList markers() const { return m_markers; }
    QVariantList areas() const { return m_areas; }
    QSizeF groundSize() const { return m_groundSize; }
    float sceneRadius() const { return m_sceneRadius; }
    float maxAltitude() const { return m_maxAltitude; }
    bool hasData() const { return !m_tracks.isEmpty(); }
    // Camera turn (degrees about the vertical, 0 = from the south) that shows the current
    // flight from the side.
    float viewYaw() const { return m_viewYaw; }
    QUrl groundImage() const { return m_groundImage; }

    // Scene position of a point, relative to the ground centre (exposed for tests).
    QVector3D toScene(sp::GeoPoint p, double altitude) const;
    // Whether setPredictions() downloads a ground image (tests switch it off).
    void setFetchGround(bool fetch) { m_fetchGround = fetch; }

signals:
    void changed();
    void groundImageChanged();
    void viewReset(); // new data: the QML puts the camera back to its overview

private:
    GeoBox m_box;
    sp::GeoPoint m_centre;
    double m_metresPerDegLat = 111000.0;
    double m_metresPerDegLon = 55000.0;
    QVariantList m_tracks, m_markers, m_areas;
    QSizeF m_groundSize;
    float m_sceneRadius = 100.0f;
    float m_maxAltitude = 0.0f;
    float m_viewYaw = -20.0f;
    QUrl m_groundImage;
    GroundTexture m_ground;
    bool m_fetchGround = true;
};

// The 3D view: a QQuickWidget showing Track3D.qml. Shows a message instead if this computer's
// graphics cannot run it.
class Scene3DWidget : public QQuickWidget {
    Q_OBJECT
public:
    explicit Scene3DWidget(QWidget *parent = nullptr);
    ~Scene3DWidget() override;

    Scene3DModel *model() { return &m_model; }

private:
    Scene3DModel m_model;
};
