#pragma once

#include "predictionstore.h"

#include <QWidget>

#include <optional>
#include <vector>

// Altitude against time since launch for the selected predictions (the first one in colour,
// the others grey), with the wind speed of the first on a second axis. Hovering shows the
// values at that moment.
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

    std::vector<sp::PredictionRecord> m_records;
    double m_maxMinutes = 1.0;
    double m_maxKm = 1.0;
    double m_maxWind = 1.0;
    std::optional<double> m_hoverMinutes;
};
