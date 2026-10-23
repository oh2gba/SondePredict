#include "sitestats.h"
#include "sondehub.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QTest>

#include <algorithm>
#include <cmath>

class TestSiteStats : public QObject {
    Q_OBJECT
private slots:
    void parsesAndApplies()
    {
        const QByteArray json = R"json([
            {"id":"02963","name":"Jokioinen (Finland)","lat":60.8146,"lon":23.4987,"ascent":5.1,"burst":29700,
             "descent":12.5,"ascentSd":0.41,"burstSd":1850,"descentSd":2.2,"flights":924},
            {"id":"10393","name":"Lindenberg (Germany)","lat":52.21,"lon":14.12,"ascent":5.0,"burst":33000,"descent":5.0},
            {"id":"bad","name":"No burst","lat":0,"lon":0,"ascent":5,"burst":0,"descent":5}
        ])json";
        const auto sites = sp::parseLaunchSiteStats(json);
        QCOMPARE(sites.size(), std::size_t(2)); // the unusable entry is skipped
        const sp::LaunchSiteStats &jok = sites[0];
        QCOMPARE(jok.id, QStringLiteral("02963"));
        QCOMPARE(jok.flights, 924);

        sp::FlightProfile legacy;
        legacy.descentModel = sp::DescentModel::Legacy;
        legacy.descentExponent = 0.3;
        const sp::FlightProfile p = jok.applyTo(legacy);
        QCOMPARE(p.ascentRate, 5.1);
        QCOMPARE(p.burstAltitude, 29700.0);
        QCOMPARE(p.seaLevelDescentRate, 12.5);
        QCOMPARE(p.descentModel, sp::DescentModel::DensityScaled);
        QCOMPARE(p.descentExponent, 0.33); // no fit: the free-fall exponent for a fast descent

        const sp::FlightSpread spread = jok.applyTo(sp::FlightSpread{});
        QCOMPARE(spread.ascentRate, 0.41);
        QCOMPARE(spread.burstAltitude, 1850.0);
        QVERIFY(std::abs(spread.descentRate - 2.2 / 12.5) < 1e-12);
        // Without spreads the current ones stay.
        sp::FlightSpread custom;
        custom.burstAltitude = 1234;
        QCOMPARE(sites[1].applyTo(custom).burstAltitude, 1234.0);
    }

    void descentExponentFittedOrFromItsSpeed()
    {
        const auto sites = sp::parseLaunchSiteStats(R"json([
            {"id":"a","name":"Fitted","lat":59.4,"lon":24.6,"ascent":5.2,"burst":23000,"descent":7.3,"descentK":0.46,
             "descentTracks":12,"descentUntil":"2026-10-22"},
            {"id":"b","name":"Parachute","lat":54.1,"lon":13.4,"ascent":5.0,"burst":32000,"descent":3.8},
            {"id":"c","name":"Free fall","lat":60.8,"lon":23.5,"ascent":5.0,"burst":34000,"descent":12.5}])json");
        QCOMPARE(sites.size(), std::size_t(3));
        QVERIFY(sites[0].descentMeasured());
        QCOMPARE(sites[0].exponent(), 0.46);
        QCOMPARE(sites[0].descentUntil, QDate(2026, 10, 22));
        QVERIFY(sites[0].descentSource().contains(QStringLiteral("12 descents")));
        QCOMPARE(sites[1].exponent(), 0.45); // slow: a parachute
        QCOMPARE(sites[2].exponent(), 0.33); // fast: falling free
        QVERIFY(!sites[2].descentMeasured());
        QCOMPARE(sites[1].applyTo(sp::FlightProfile{}).descentExponent, 0.45);
        QCOMPARE(sp::siteBalloon(sites[0], std::nullopt).descentRate, 7.3);
        QCOMPARE(sp::siteBalloon(sites[0], std::nullopt).descentExponent, 0.46);
    }

    void sortsByDistance()
    {
        auto sites = sp::parseLaunchSiteStats(R"json([
            {"id":"a","name":"A far","lat":52.0,"lon":14.0,"ascent":5,"burst":30000,"descent":5},
            {"id":"b","name":"B near","lat":60.8,"lon":23.5,"ascent":5,"burst":30000,"descent":5}])json");
        sp::sortByDistance(sites, {{61.0, 24.0}});
        QCOMPARE(sites.front().id, QStringLiteral("b"));
        sp::sortByDistance(sites, {});
        QCOMPARE(sites.front().id, QStringLiteral("a")); // by name
    }

    void parsesSondeHubSummaries()
    {
        const auto keys = sp::sondehub::parseListing(
            "<ListBucketResult><Contents><Key>launchsites/02963/2026/10/22/X4747640.json</Key></Contents>"
            "<Contents><Key>launchsites/02963/2026/10/22/X4747652.json</Key></Contents></ListBucketResult>");
        QCOMPARE(keys.size(), std::size_t(2));
        QCOMPARE(keys[1], QStringLiteral("launchsites/02963/2026/10/22/X4747652.json"));

        // A real Jokioinen flight: first heard, burst, last heard (SondeHub writes 3 or 6 decimals).
        const QByteArray flight = R"json([{"datetime": "2026-10-22T11:32:32.995000Z", "alt": 735.38854, "lat": 60.82548, "lon": 23.48278},
            {"datetime": "2026-10-22T13:30:46.000Z", "alt": 34771.35938, "lat": 61.02353, "lon": 23.38817},
            {"datetime": "2026-10-22T13:59:12.002000Z", "alt": 311.66292, "lat": 61.12062, "lon": 23.31005}])json";
        const sp::GeoPoint jokioinen{60.8146, 23.4987};
        const auto f = sp::sondehub::parseSummary(flight, jokioinen);
        QVERIFY(f);
        QCOMPARE(f->burstAltitude, 34771.35938);
        QVERIFY(std::abs((f->burstTime - f->firstTime) - 7093.0) < 1.0);
        // Heard first far from the site: a sonde from elsewhere.
        QVERIFY(!sp::sondehub::parseSummary(flight, {62.0, 25.0}));

        std::vector<sp::sondehub::FlightSummary> flights;
        for (int i = 0; i < 10; ++i)
            flights.push_back({0.0, 100.0, jokioinen, 7000.0 + 100.0 * i, 34000.0 + 100.0 * i});
        const auto stats = sp::sondehub::summarize(flights);
        QVERIFY(stats);
        QCOMPARE(stats->flights, 10);
        QVERIFY(std::abs(stats->burstAltitude - 34450.0) < 1.0);
        QVERIFY(stats->ascentRate > 4.5 && stats->ascentRate < 5.0);
        QVERIFY(!sp::sondehub::summarize({flights.begin(), flights.begin() + 4})); // too few
    }

    // Online: set SP_LIVE_TESTS=1 to ask SondeHub for Jokioinen's flights of the last 30 days.
    void recentFlightsLive()
    {
        if (!qEnvironmentVariableIsSet("SP_LIVE_TESTS"))
            QSKIP("set SP_LIVE_TESTS=1 to query SondeHub");
        QNetworkAccessManager network;
        sp::SondeHubClient client(&network);
        std::optional<sp::RecentFlightStats> result;
        QString error;
        bool done = false;
        client.recentFlights(QStringLiteral("02963"), {60.8146, 23.4987}, 30, [&](const auto &stats, const QString &e) {
            result = stats;
            error = e;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY2(result, qPrintable(error));
        qInfo("%d flights %s – %s: ascent %.2f ± %.2f m/s, burst %.0f ± %.0f m", result->flights,
              qPrintable(result->first.toString(Qt::ISODate)), qPrintable(result->last.toString(Qt::ISODate)),
              result->ascentRate, result->ascentSd, result->burstAltitude, result->burstSd);
        QVERIFY(result->flights > 40);
    }

    void bundledListIsUsable()
    {
        QFile file(QStringLiteral(SP_TEST_DATA "/../../src/app/data/launch-sites.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto sites = sp::parseLaunchSiteStats(file.readAll());
        QVERIFY(sites.size() > 200);
        QVERIFY(std::any_of(sites.begin(), sites.end(), [](const sp::LaunchSiteStats &s) { return s.id == QLatin1String("02963"); }));
        for (const auto &s : sites)
            QVERIFY2(!*s.applyTo(sp::FlightProfile{}).validationError(), qPrintable(s.name));
    }
};

QTEST_GUILESS_MAIN(TestSiteStats)
#include "tst_sitestats.moc"
