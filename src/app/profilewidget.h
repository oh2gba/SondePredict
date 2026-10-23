#pragma once

#include "predictionstore.h"

#include <QWidget>

#include <functional>

class QPainter;
class QPainterPath;

#include <optional>
#include <vector>

// Altitude against time since launch. One prediction: ascent and descent in their colours,
// with the wind speed and direction on a second axis. Several: one colour each, with a
// legend. Hovering shows the values at that moment.
class ProfileWidget : public QWidget {
    Q_OBJECT
public:
    explicit ProfileWidget(QWidget *parent = nullptr);

    void setPredictions(const std::vector<sp::PredictionRecord> &records);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF plotArea() const;
    void paintSingle(QPainter &p, const std::function<double(double)> &px, const std::function<double(double)> &py,
                     const std::function<double(double)> &pw, const QColor &windColor);
    void paintSeveral(QPainter &p, const std::function<double(double)> &px, const std::function<double(double)> &py,
                      const std::function<double(double)> &pw);
    static QPainterPath phasePath(const std::vector<sp::TrackPoint> &track, sp::FlightPhase phase,
                                  const std::function<double(double)> &px, const std::function<double(double)> &py);
    static QColor seriesColor(std::size_t index);
    static QString label(const sp::PredictionRecord &record);

    std::vector<sp::PredictionRecord> m_records;
    double m_maxMinutes = 1.0;
    double m_maxKm = 1.0;
    double m_maxWind = 1.0;
    std::optional<double> m_hoverMinutes;
};
