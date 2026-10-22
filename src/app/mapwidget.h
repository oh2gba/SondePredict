#pragma once

#include "predictionstore.h"
#include "settings.h"

#include <QGeoShape>
#include <QObject>
#include <QQuickWidget>
#include <QVariantList>

#include <vector>

// Data behind TrackMap.qml: polylines and markers as plain QVariant lists.
class TrackMapModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed)
    Q_PROPERTY(QString userAgent READ userAgent CONSTANT)
    Q_PROPERTY(QString cacheDirectory READ cacheDirectory CONSTANT)
public:
    using QObject::QObject;

    QVariantList lines() const { return m_lines; }
    QVariantList markers() const { return m_markers; }
    QString userAgent() const;
    QString cacheDirectory() const;

    // The first record is the "current" one (drawn on top, labelled).
    void setPredictions(const std::vector<sp::PredictionRecord> &records);
    // Shown when no prediction is selected.
    void setLocations(const sp::Location *site, const sp::Location *home);

signals:
    void changed();
    void fitRequested(const QGeoShape &shape);

private:
    QVariantList m_lines;
    QVariantList m_markers;
};

class MapWidget : public QQuickWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget *parent = nullptr);
    ~MapWidget() override;

    TrackMapModel *model() { return &m_model; }

private:
    TrackMapModel m_model;
};
