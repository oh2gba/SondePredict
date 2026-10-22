#include "atmosphere.h"
#include "openmeteo.h"

#include <QFile>
#include <QTest>
#include <QUrlQuery>

#include <cmath>
#include <numbers>

namespace {

QByteArray fixture()
{
    QFile f(QStringLiteral(SP_TEST_DATA "/openmeteo_2nodes_2hours.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing test fixture");
    return f.readAll();
}

// The fixture was requested for 60.75 N and 61.00 N at 23.50 E, 2026-10-23 11:00–12:00 UTC.
const std::vector<sp::NodeIndex> kNodes{{243, 94}, {244, 94}};
constexpr qint64 kFirstHour = 1792753200 / 3600;

} // namespace

class TestOpenMeteo : public QObject {
    Q_OBJECT
private slots:
    void buildsUrl()
    {
        sp::WindField field(0.25);
        const QUrl url = sp::openmeteo::buildUrl({}, field, kNodes, kFirstHour, kFirstHour + 1);
        const QUrlQuery q(url);
        QCOMPARE(url.host(), QStringLiteral("api.open-meteo.com"));
        QCOMPARE(q.queryItemValue(QStringLiteral("latitude")), QStringLiteral("60.7500,61.0000"));
        QCOMPARE(q.queryItemValue(QStringLiteral("longitude")), QStringLiteral("23.5000,23.5000"));
        QCOMPARE(q.queryItemValue(QStringLiteral("start_hour")), QStringLiteral("2026-10-23T11:00"));
        QCOMPARE(q.queryItemValue(QStringLiteral("end_hour")), QStringLiteral("2026-10-23T12:00"));
        QCOMPARE(q.queryItemValue(QStringLiteral("timeformat")), QStringLiteral("unixtime"));
        const QString hourly = q.queryItemValue(QStringLiteral("hourly"));
        QVERIFY(hourly.contains(QStringLiteral("geopotential_height_10hPa")));
        QCOMPARE(hourly.count(u',') + 1, 2 + 3 * int(sp::openmeteo::pressureLevels().size()));
        QVERIFY(!q.hasQueryItem(QStringLiteral("apikey")));

        sp::OpenMeteoOptions subscription;
        subscription.baseUrl = QStringLiteral("https://customer-api.open-meteo.com/v1/gfs");
        subscription.apiKey = QStringLiteral("abc123");
        const QUrl paid = sp::openmeteo::buildUrl(subscription, field, kNodes, kFirstHour, kFirstHour + 1);
        QCOMPARE(paid.host(), QStringLiteral("customer-api.open-meteo.com"));
        QCOMPARE(QUrlQuery(paid).queryItemValue(QStringLiteral("apikey")), QStringLiteral("abc123"));
    }

    void parsesRecordedResponse()
    {
        sp::WindField field(0.25);
        QCOMPARE(sp::openmeteo::parseInto(fixture(), kNodes, field), QString());
        for (const sp::NodeIndex &n : kNodes) {
            QVERIFY(field.contains({n, kFirstHour}));
            QVERIFY(field.contains({n, kFirstHour + 1}));
        }

        // At the node, hour and exact 850 hPa height the wind is the reported one:
        // 15.13 m/s from 162°, height 1481 gpm.
        const double altitude = sp::atmosphere::geometricFromGeopotential(1481.0);
        const auto s = field.sample(field.position(kNodes[0]), altitude, kFirstHour * 3600.0);
        QVERIFY(s);
        constexpr double deg = std::numbers::pi / 180.0;
        QVERIFY(qAbs(s->u - (-15.13 * std::sin(162 * deg))) < 1e-6);
        QVERIFY(qAbs(s->v - (-15.13 * std::cos(162 * deg))) < 1e-6);
        QVERIFY(s->v > 0); // wind from SSE blows towards the north
        QCOMPARE(s->groundElevation, 100.0);

        // Near the top the 10 hPa level (~30.5 km) is still below us at 31 km.
        QVERIFY(field.sample(field.position(kNodes[0]), 31000.0, kFirstHour * 3600.0)->aboveTopLevel);
        QVERIFY(!field.sample(field.position(kNodes[0]), 30000.0, kFirstHour * 3600.0)->aboveTopLevel);
    }

    void requestWeight()
    {
        QCOMPARE(sp::openmeteo::variableCount(), 86);
        QVERIFY(qAbs(sp::openmeteo::requestWeight(16, 86, 0.25) - 16 * 8.6) < 1e-9);
        QCOMPARE(sp::openmeteo::requestWeight(1, 5, 1.0), 1.0);   // at least one call per location
        QCOMPARE(sp::openmeteo::requestWeight(1, 15, 28.0), 3.0); // Open-Meteo's own example × 2
    }

    void rateBudgetSpacesRequests()
    {
        sp::RateBudget budget(550, 4800);
        QCOMPARE(budget.reserve(430, 0), qint64(0));
        QCOMPARE(budget.reserve(100, 1000), qint64(1000));   // 530 ≤ 550: right away
        QCOMPARE(budget.reserve(100, 2000), qint64(60000));  // would be 630: wait for the first to expire
        QCOMPARE(budget.reserve(20, 3000), qint64(60000));   // keeps booking order
        QCOMPARE(budget.reserve(900, 61000), qint64(120000)); // oversize: once the minute is empty
    }

    void rateBudgetHourLimit()
    {
        sp::RateBudget budget(550, 1000);
        qint64 t = 0;
        for (int i = 0; i < 5; ++i)
            t = budget.reserve(200, t); // 5 × 200 = 1000 within the first minutes
        QVERIFY(t < 3600 * 1000);
        QCOMPARE(budget.reserve(200, t), qint64(3600 * 1000)); // the hour is full until the first expires
    }

    void reportsApiError()
    {
        sp::WindField field(0.25);
        const QString error = sp::openmeteo::parseInto(
            R"({"error":true,"reason":"Parameter 'start_hour' is out of allowed range"})", kNodes, field);
        QVERIFY(error.contains(QStringLiteral("out of allowed range")));
        QVERIFY(field.isEmpty());
    }

    void rejectsLocationCountMismatch()
    {
        sp::WindField field(0.25);
        const QString error = sp::openmeteo::parseInto(fixture(), {kNodes[0]}, field);
        QVERIFY(error.contains(QStringLiteral("expected 1")));
    }

    void rejectsGarbage()
    {
        sp::WindField field(0.25);
        QVERIFY(!sp::openmeteo::parseInto("<html>busy</html>", kNodes, field).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestOpenMeteo)
#include "tst_openmeteo.moc"
