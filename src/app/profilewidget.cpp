#include "profilewidget.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

const QColor kAscent(0x1e, 0x88, 0xe5);
const QColor kDescent(0xe5, 0x39, 0x35);
const QColor kBurst(0xef, 0x6c, 0x00);

double niceStep(double range, int ticks)
{
    const double raw = range / std::max(ticks, 1);
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    for (double f : {1.0, 2.0, 5.0, 10.0}) {
        if (f * magnitude >= raw)
            return f * magnitude;
    }
    return 10.0 * magnitude;
}

double windSpeed(const sp::TrackPoint &p)
{
    return std::hypot(p.u, p.v);
}

double windFrom(const sp::TrackPoint &p)
{
    const double deg = std::atan2(-p.u, -p.v) * 180.0 / std::numbers::pi;
    return deg < 0 ? deg + 360.0 : deg;
}

QString hhmm(double unixTime)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(unixTime), QTimeZone::UTC).toString(QStringLiteral("HH:mm"));
}

} // namespace

ProfileWidget::ProfileWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(300, 160);
    setAutoFillBackground(true);
}

void ProfileWidget::setPredictions(const std::vector<sp::PredictionRecord> &records)
{
    m_records = records;
    m_maxMinutes = 1.0;
    m_maxKm = 1.0;
    m_maxWind = 1.0;
    for (std::size_t i = 0; i < m_records.size(); ++i) {
        const auto &track = m_records[i].prediction.result.track;
        if (track.empty())
            continue;
        m_maxMinutes = std::max(m_maxMinutes, (track.back().time - track.front().time) / 60.0);
        for (const sp::TrackPoint &p : track) {
            m_maxKm = std::max(m_maxKm, p.altitude / 1000.0);
            if (i == 0)
                m_maxWind = std::max(m_maxWind, windSpeed(p));
        }
    }
    update();
}

QRectF ProfileWidget::plotArea() const
{
    return QRectF(QPointF(52, 14), QPointF(width() - 52, height() - 30));
}

void ProfileWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF area = plotArea();
    if (m_records.empty() || m_records.front().prediction.result.track.empty() || area.width() < 50 || area.height() < 40) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Select a prediction to see its altitude profile."));
        return;
    }

    const double minuteStep = niceStep(m_maxMinutes, 8);
    const double kmStep = niceStep(m_maxKm, 6);
    const double windStep = niceStep(m_maxWind, 6);
    const double xMax = std::ceil(m_maxMinutes / minuteStep) * minuteStep;
    const double yMax = std::ceil(m_maxKm / kmStep) * kmStep;
    const double wMax = std::ceil(m_maxWind / windStep) * windStep;
    const auto px = [&](double minutes) { return area.left() + minutes / xMax * area.width(); };
    const auto py = [&](double km) { return area.bottom() - km / yMax * area.height(); };
    const auto pw = [&](double ms) { return area.bottom() - ms / wMax * area.height(); };

    // Grid and axes.
    QColor grid = pal.color(QPalette::Mid);
    grid.setAlpha(90);
    p.setFont(QFont(font().family(), 8));
    const QColor text = pal.color(QPalette::WindowText);
    for (double m = 0; m <= xMax + 1e-6; m += minuteStep) {
        p.setPen(grid);
        p.drawLine(QPointF(px(m), area.top()), QPointF(px(m), area.bottom()));
        p.setPen(text);
        p.drawText(QRectF(px(m) - 30, area.bottom() + 2, 60, 14), Qt::AlignHCenter, QString::number(m, 'f', 0));
    }
    for (double k = 0; k <= yMax + 1e-6; k += kmStep) {
        p.setPen(grid);
        p.drawLine(QPointF(area.left(), py(k)), QPointF(area.right(), py(k)));
        p.setPen(text);
        p.drawText(QRectF(0, py(k) - 7, area.left() - 6, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(k, 'f', kmStep < 1 ? 1 : 0));
    }
    QColor windColor = pal.color(QPalette::WindowText);
    windColor.setAlpha(140);
    for (double w = 0; w <= wMax + 1e-6; w += windStep) {
        p.setPen(windColor);
        p.drawText(QRectF(area.right() + 6, pw(w) - 7, 46, 14), Qt::AlignLeft | Qt::AlignVCenter, QString::number(w, 'f', 0));
    }
    p.setPen(text);
    p.drawText(QRectF(area.left(), area.bottom() + 14, area.width(), 14), Qt::AlignHCenter, tr("minutes after launch"));
    p.save();
    p.translate(10, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-60, -8, 120, 14), Qt::AlignCenter, tr("altitude (km)"));
    p.restore();
    p.save();
    p.translate(width() - 8, area.center().y());
    p.rotate(90);
    p.setPen(windColor);
    p.drawText(QRectF(-60, -8, 120, 14), Qt::AlignCenter, tr("wind (m/s)"));
    p.restore();

    // Other selected predictions in grey, behind.
    QColor other = pal.color(QPalette::Mid);
    other.setAlpha(160);
    for (std::size_t i = 1; i < m_records.size(); ++i) {
        const auto &track = m_records[i].prediction.result.track;
        if (track.empty())
            continue;
        QPainterPath path;
        for (std::size_t k = 0; k < track.size(); ++k) {
            const QPointF pt(px((track[k].time - track.front().time) / 60.0), py(track[k].altitude / 1000.0));
            k == 0 ? path.moveTo(pt) : path.lineTo(pt);
        }
        p.setPen(QPen(other, 1.2));
        p.drawPath(path);
    }

    // The current prediction: wind speed (dashed), then the altitude in ascent/descent colours.
    const auto &track = m_records.front().prediction.result.track;
    const double t0 = track.front().time;
    QPainterPath wind;
    for (std::size_t k = 0; k < track.size(); ++k) {
        const QPointF pt(px((track[k].time - t0) / 60.0), pw(windSpeed(track[k])));
        k == 0 ? wind.moveTo(pt) : wind.lineTo(pt);
    }
    p.setPen(QPen(windColor, 1.0, Qt::DashLine));
    p.drawPath(wind);

    for (const auto &[phase, color] : {std::pair{sp::FlightPhase::Ascent, kAscent}, std::pair{sp::FlightPhase::Descent, kDescent}}) {
        QPainterPath path;
        bool started = false;
        for (std::size_t k = 0; k < track.size(); ++k) {
            const bool burstJoin = phase == sp::FlightPhase::Descent && k + 1 < track.size() && track[k + 1].phase == phase
                                && track[k].phase != phase; // start the descent at the burst point
            if (track[k].phase != phase && !burstJoin)
                continue;
            const QPointF pt(px((track[k].time - t0) / 60.0), py(track[k].altitude / 1000.0));
            started ? path.lineTo(pt) : path.moveTo(pt);
            started = true;
        }
        p.setPen(QPen(color, 2.2));
        p.drawPath(path);
    }

    const auto &r = m_records.front().prediction.result;
    p.setPen(Qt::NoPen);
    if (r.burst) {
        const QPointF b(px((r.burst->time - t0) / 60.0), py(r.burst->altitude / 1000.0));
        p.setBrush(kBurst);
        p.drawEllipse(b, 4.5, 4.5);
        p.setPen(text);
        p.drawText(QRectF(b.x() + 7, b.y() - 2, 160, 14), Qt::AlignLeft,
                   tr("Burst %1 km, %2").arg(r.burst->altitude / 1000.0, 0, 'f', 1).arg(hhmm(r.burst->time)));
        p.setPen(Qt::NoPen);
    }
    if (r.landing) {
        const QPointF l(px((r.landing->time - t0) / 60.0), py(r.landing->altitude / 1000.0));
        p.setBrush(kDescent);
        p.drawEllipse(l, 4.5, 4.5);
        p.setPen(text);
        p.drawText(QRectF(l.x() - 166, l.y() - 18, 160, 14), Qt::AlignRight, tr("Landing %1").arg(hhmm(r.landing->time)));
    }

    // Hover readout.
    if (m_hoverMinutes) {
        const double t = t0 + *m_hoverMinutes * 60.0;
        const auto it = std::lower_bound(track.begin(), track.end(), t,
                                         [](const sp::TrackPoint &a, double time) { return a.time < time; });
        if (it != track.end()) {
            const sp::TrackPoint &pt = *it;
            const double x = px((pt.time - t0) / 60.0);
            p.setPen(QPen(pal.color(QPalette::Highlight), 1));
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            const QString info = tr("+%1 min (%2 UTC)  %3 km  wind %4 m/s from %5°")
                                     .arg(qRound((pt.time - t0) / 60.0)).arg(hhmm(pt.time))
                                     .arg(pt.altitude / 1000.0, 0, 'f', 1).arg(windSpeed(pt), 0, 'f', 0).arg(qRound(windFrom(pt)));
            const QRectF box = p.fontMetrics().boundingRect(info).adjusted(-5, -3, 5, 3);
            const QPointF at(std::clamp(x + 8, area.left(), area.right() - box.width()), area.top() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(pal.color(QPalette::ToolTipBase));
            p.drawRoundedRect(QRectF(at, box.size()), 3, 3);
            p.setPen(pal.color(QPalette::ToolTipText));
            p.drawText(QRectF(at, box.size()), Qt::AlignCenter, info);
        }
    }
}

void ProfileWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QRectF area = plotArea();
    if (area.contains(event->position()) && !m_records.empty()) {
        const double step = niceStep(m_maxMinutes, 8);
        const double xMax = std::ceil(m_maxMinutes / step) * step;
        m_hoverMinutes = (event->position().x() - area.left()) / area.width() * xMax;
    } else {
        m_hoverMinutes.reset();
    }
    update();
}

void ProfileWidget::leaveEvent(QEvent *)
{
    m_hoverMinutes.reset();
    update();
}
