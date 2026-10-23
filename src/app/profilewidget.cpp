#include "profilewidget.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
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
    const bool single = m_records.size() == 1;

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
    p.setPen(text);
    p.drawText(QRectF(area.left(), area.bottom() + 14, area.width(), 14), Qt::AlignHCenter, tr("minutes after launch"));
    p.save();
    p.translate(10, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-60, -8, 120, 14), Qt::AlignCenter, tr("altitude (km)"));
    p.restore();

    for (double w = 0; w <= wMax + 1e-6; w += windStep) {
        p.setPen(windColor);
        p.drawText(QRectF(area.right() + 6, pw(w) - 7, 46, 14), Qt::AlignLeft | Qt::AlignVCenter, QString::number(w, 'f', 0));
    }
    p.save();
    p.translate(width() - 8, area.center().y());
    p.rotate(90);
    p.setPen(windColor);
    p.drawText(QRectF(-80, -8, 160, 14), Qt::AlignCenter, single ? tr("wind (m/s), arrows: blowing to") : tr("wind (m/s), dotted"));
    p.restore();
    if (single)
        paintSingle(p, px, py, pw, windColor);
    else
        paintSeveral(p, px, py, pw);

    // Hover readout: the values of every shown prediction at that moment.
    if (m_hoverMinutes) {
        const double x = px(*m_hoverMinutes);
        p.setPen(QPen(pal.color(QPalette::Highlight), 1));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        QStringList lines;
        for (std::size_t i = 0; i < m_records.size(); ++i) {
            const auto &track = m_records[i].prediction.result.track;
            if (track.empty())
                continue;
            const double t = track.front().time + *m_hoverMinutes * 60.0;
            const auto it = std::lower_bound(track.begin(), track.end(), t,
                                             [](const sp::TrackPoint &a, double time) { return a.time < time; });
            if (it == track.end())
                continue;
            const QString values = tr("%1 km, wind %2 m/s from %3°").arg(it->altitude / 1000.0, 0, 'f', 1)
                                       .arg(windSpeed(*it), 0, 'f', 0).arg(qRound(windFrom(*it)));
            lines << (single ? tr("+%1 min (%2 UTC)  %3").arg(qRound(*m_hoverMinutes)).arg(hhmm(it->time)).arg(values)
                             : QStringLiteral("%1: %2").arg(label(m_records[i]), values));
        }
        if (!single)
            lines.prepend(tr("+%1 min after launch").arg(qRound(*m_hoverMinutes)));
        if (lines.size() > (single ? 0 : 1)) {
            const QFontMetricsF fm(p.font());
            double w = 0;
            for (const QString &line : lines)
                w = std::max(w, fm.horizontalAdvance(line));
            const QSizeF size(w + 10, fm.height() * lines.size() + 6);
            const QPointF at(std::clamp(x + 8, area.left(), area.right() - size.width()), area.top() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(pal.color(QPalette::ToolTipBase));
            p.drawRoundedRect(QRectF(at, size), 3, 3);
            p.setPen(pal.color(QPalette::ToolTipText));
            for (int k = 0; k < lines.size(); ++k)
                p.drawText(QRectF(at.x() + 5, at.y() + 3 + k * fm.height(), w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, lines[k]);
            p.setBrush(Qt::NoBrush);
        }
    }
}

void ProfileWidget::paintSingle(QPainter &p, const std::function<double(double)> &px, const std::function<double(double)> &py,
                                const std::function<double(double)> &pw, const QColor &windColor)
{
    const QRectF area = plotArea();
    const QColor text = palette().color(QPalette::WindowText);
    const auto &track = m_records.front().prediction.result.track;
    const double t0 = track.front().time;

    // Wind speed (dashed), with arrows pointing where the wind blows to (up = north, as on
    // the map), about every 40 px.
    QPainterPath wind;
    for (std::size_t k = 0; k < track.size(); ++k) {
        const QPointF pt(px((track[k].time - t0) / 60.0), pw(windSpeed(track[k])));
        k == 0 ? wind.moveTo(pt) : wind.lineTo(pt);
    }
    p.setPen(QPen(windColor, 1.0, Qt::DashLine));
    p.drawPath(wind);

    const double xMax = (px(1.0) - px(0.0)) > 0 ? area.width() / (px(1.0) - px(0.0)) : 1.0;
    const double arrowSpacingMinutes = std::max(1.0, 40.0 / area.width() * xMax);
    p.setPen(QPen(windColor, 1.4));
    p.setBrush(windColor);
    double nextArrow = arrowSpacingMinutes / 2;
    for (const sp::TrackPoint &pt : track) {
        const double minutes = (pt.time - t0) / 60.0;
        if (minutes < nextArrow)
            continue;
        nextArrow += arrowSpacingMinutes;
        const double speed = windSpeed(pt);
        if (speed < 0.5)
            continue;
        const QPointF centre(px(minutes), pw(speed));
        const QPointF dir(pt.u / speed, -pt.v / speed); // screen y grows downwards
        constexpr double half = 7.0, head = 4.0;
        const QPointF tip = centre + dir * half;
        const QPointF tail = centre - dir * half;
        const QPointF side(-dir.y(), dir.x());
        p.drawLine(tail, tip);
        p.drawPolygon(QPolygonF{tip, tip - dir * head + side * head * 0.6, tip - dir * head - side * head * 0.6});
    }
    p.setBrush(Qt::NoBrush);

    for (const auto &[phase, color] : {std::pair{sp::FlightPhase::Ascent, kAscent}, std::pair{sp::FlightPhase::Descent, kDescent}}) {
        p.setPen(QPen(color, 2.2));
        p.drawPath(phasePath(track, phase, px, py));
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
    p.setBrush(Qt::NoBrush);
}

void ProfileWidget::paintSeveral(QPainter &p, const std::function<double(double)> &px, const std::function<double(double)> &py,
                                 const std::function<double(double)> &pw)
{
    const QRectF area = plotArea();
    const QPalette pal = palette();
    // Launches with the same balloon climb alike; their winds are what differs. Draw those
    // first, thin and dotted, so the altitude lines stay on top.
    for (std::size_t i = 0; i < m_records.size(); ++i) {
        const auto &track = m_records[i].prediction.result.track;
        if (track.empty())
            continue;
        QPainterPath wind;
        for (std::size_t k = 0; k < track.size(); ++k) {
            const QPointF pt(px((track[k].time - track.front().time) / 60.0), pw(windSpeed(track[k])));
            k == 0 ? wind.moveTo(pt) : wind.lineTo(pt);
        }
        QColor color = seriesColor(i);
        color.setAlpha(170);
        p.setPen(QPen(color, 1.3, Qt::DotLine));
        p.drawPath(wind);
    }
    for (std::size_t i = 0; i < m_records.size(); ++i) {
        const auto &track = m_records[i].prediction.result.track;
        if (track.empty())
            continue;
        const QColor color = seriesColor(i);
        p.setPen(QPen(color, 2.0));
        p.drawPath(phasePath(track, sp::FlightPhase::Ascent, px, py));
        p.setPen(QPen(color, 2.0, Qt::DashLine));
        p.drawPath(phasePath(track, sp::FlightPhase::Descent, px, py));

        const double t0 = track.front().time;
        const auto &r = m_records[i].prediction.result;
        p.setPen(QPen(pal.color(QPalette::Base), 1.2));
        p.setBrush(color);
        if (r.burst)
            p.drawEllipse(QPointF(px((r.burst->time - t0) / 60.0), py(r.burst->altitude / 1000.0)), 4.0, 4.0);
        if (r.landing)
            p.drawRect(QRectF(QPointF(px((r.landing->time - t0) / 60.0), py(r.landing->altitude / 1000.0)) - QPointF(3.5, 3.5), QSizeF(7, 7)));
        p.setBrush(Qt::NoBrush);
    }

    // Legend, top right inside the plot.
    const QFontMetricsF fm(p.font());
    constexpr int kMaxRows = 10;
    const int rows = int(std::min<std::size_t>(m_records.size(), kMaxRows));
    double w = fm.horizontalAdvance(tr("solid: ascent, dashed: descent, dotted: wind"));
    for (int i = 0; i < rows; ++i)
        w = std::max(w, fm.horizontalAdvance(label(m_records[std::size_t(i)])) + 22);
    const int lineCount = rows + 1 + (m_records.size() > kMaxRows ? 1 : 0);
    const QRectF box(area.right() - w - 14, area.top() + 4, w + 10, fm.height() * lineCount + 6);
    QColor background = pal.color(QPalette::Base);
    background.setAlpha(225);
    p.setPen(QPen(pal.color(QPalette::Mid), 0.8));
    p.setBrush(background);
    p.drawRoundedRect(box, 3, 3);
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < rows; ++i) {
        const double y = box.top() + 3 + i * fm.height();
        p.setPen(QPen(seriesColor(std::size_t(i)), 2.5));
        p.drawLine(QPointF(box.left() + 5, y + fm.height() / 2), QPointF(box.left() + 20, y + fm.height() / 2));
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(QRectF(box.left() + 25, y, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, label(m_records[std::size_t(i)]));
    }
    p.setPen(pal.color(QPalette::PlaceholderText));
    double y = box.top() + 3 + rows * fm.height();
    if (m_records.size() > kMaxRows) {
        p.drawText(QRectF(box.left() + 25, y, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("and %1 more").arg(m_records.size() - kMaxRows));
        y += fm.height();
    }
    p.drawText(QRectF(box.left() + 5, y, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, tr("solid: ascent, dashed: descent, dotted: wind"));
}

QPainterPath ProfileWidget::phasePath(const std::vector<sp::TrackPoint> &track, sp::FlightPhase phase,
                                      const std::function<double(double)> &px, const std::function<double(double)> &py)
{
    QPainterPath path;
    bool started = false;
    const double t0 = track.front().time;
    for (std::size_t k = 0; k < track.size(); ++k) {
        const bool burstJoin = phase == sp::FlightPhase::Descent && k + 1 < track.size() && track[k + 1].phase == phase
                            && track[k].phase != phase; // start the descent at the burst point
        if (track[k].phase != phase && !burstJoin)
            continue;
        const QPointF pt(px((track[k].time - t0) / 60.0), py(track[k].altitude / 1000.0));
        started ? path.lineTo(pt) : path.moveTo(pt);
        started = true;
    }
    return path;
}

QColor ProfileWidget::seriesColor(std::size_t index)
{
    // Distinct colours that read on light and dark backgrounds.
    static const QColor colors[] = {QColor(0x1e, 0x88, 0xe5), QColor(0xe5, 0x39, 0x35), QColor(0x43, 0xa0, 0x47),
                                    QColor(0x8e, 0x24, 0xaa), QColor(0xfb, 0x8c, 0x00), QColor(0x00, 0x89, 0x7b),
                                    QColor(0xd8, 0x1b, 0x60), QColor(0x79, 0x55, 0x48)};
    return colors[index % std::size(colors)];
}

QString ProfileWidget::label(const sp::PredictionRecord &record)
{
    return tr("%1 %2 UTC").arg(record.siteName,
                               QDateTime::fromSecsSinceEpoch(record.prediction.params.launchTime, QTimeZone::UTC)
                                   .toString(QStringLiteral("dd.MM. HH:mm")));
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
