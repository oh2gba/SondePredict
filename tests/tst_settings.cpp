#include "network.h"
#include "settings.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void missingFileGivesDefaults()
    {
        QTemporaryDir dir;
        QString error;
        const sp::AppSettings s = sp::AppSettings::load(dir.filePath(QStringLiteral("none.ini")), &error);
        QVERIFY(error.isEmpty());
        QVERIFY(s.locations.isEmpty());
        QCOMPARE(s.profile.ascentRate, sp::FlightProfile{}.ascentRate);
        QCOMPARE(s.wind.gridStep, 0.25);
        QVERIFY(!s.schedule);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sub/dir/sondepredict.ini")); // created on save

        sp::AppSettings s;
        s.locations << sp::Location{QStringLiteral("Jokioinen"), {60.814, 23.499}, 104.0, 30.0}
                    << sp::Location{QStringLiteral("Home, Espoo"), {60.2055, 24.6559}, 25.0, 40.0}
                    << sp::Location{QStringLiteral("Sodankylä"), {67.367, 26.63}, 179.0, 30.0};
        s.profile.ascentRate = 5.7;
        s.profile.burstAltitude = 31000;
        s.profile.descentModel = sp::DescentModel::Legacy;
        s.profile.legacyDescentRate = 9.9;
        s.profile.legacyDescentGain = 0.63;
        s.network = {sp::ProxyMode::Manual, QStringLiteral("http://user:pw@proxy:3128")};
        s.wind = {QStringLiteral("https://example.org/v1/gfs"), QStringLiteral("key"), QStringLiteral("gfs_seamless"), 0.5};
        s.general = {QStringLiteral("C:\\Program Files\\Google Earth\\googleearth.exe"), false, false};
        s.schedule = sp::ScheduleRule{QStringLiteral("Jokioinen"), QStringLiteral("Home, Espoo"), {0, 1, 2}, {11, 23}, 15, 6, true};
        s.manualRun = {QStringLiteral("Jokioinen"), QStringLiteral("Sodankylä"), 23, 15};

        QString error;
        QVERIFY2(s.save(path, &error), qPrintable(error));
        const sp::AppSettings r = sp::AppSettings::load(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QCOMPARE(r.locations.size(), 3);
        QCOMPARE(r.locations[1].name, QStringLiteral("Home, Espoo"));
        QCOMPARE(r.locations[2].name, QStringLiteral("Sodankylä"));
        QCOMPARE(r.locations[0].position.lat, 60.814);
        QCOMPARE(r.locations[1].warningDistanceKm, 40.0);
        QCOMPARE(r.profile.ascentRate, 5.7);
        QCOMPARE(r.profile.burstAltitude, 31000.0);
        QCOMPARE(r.profile.descentModel, sp::DescentModel::Legacy);
        QCOMPARE(r.profile.legacyDescentGain, 0.63);
        QCOMPARE(r.network.proxyMode, sp::ProxyMode::Manual);
        QCOMPARE(r.network.proxyUrl, s.network.proxyUrl);
        QCOMPARE(r.wind.model, QStringLiteral("gfs_seamless"));
        QCOMPARE(r.wind.gridStep, 0.5);
        QCOMPARE(r.wind.apiKey, QStringLiteral("key"));
        QCOMPARE(r.general.googleEarthPath, s.general.googleEarthPath);
        QCOMPARE(r.general.overwritePredictions, false);
        QVERIFY(r.schedule);
        QCOMPARE(r.schedule->days, (QList<int>{0, 1, 2}));
        QCOMPARE(r.schedule->hours, (QList<int>{11, 23}));
        QCOMPARE(r.schedule->homeName, QStringLiteral("Home, Espoo"));
        QCOMPARE(r.schedule->intervalHours, 6);
        QCOMPARE(r.schedule->minute, 15);
        QCOMPARE(r.schedule->autoRun, true);
        QCOMPARE(r.manualRun.homeName, QStringLiteral("Sodankylä"));
        QCOMPARE(r.manualRun.hour, 23);
        QCOMPARE(r.manualRun.minute, 15);
    }

    void readsHandEditedLists()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("edited.ini"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[schedule]\nsite=Jokioinen\ndays=0, 1\nhours=11\n");
        f.close();
        const sp::AppSettings s = sp::AppSettings::load(path);
        QVERIFY(s.schedule);
        QCOMPARE(s.schedule->days, (QList<int>{0, 1}));
        QCOMPARE(s.schedule->hours, (QList<int>{11}));
    }

    void findsLocationsByNameIgnoringCase()
    {
        sp::AppSettings s;
        s.locations << sp::Location{QStringLiteral("Jokioinen"), {60.814, 23.499}, 104.0, 30.0};
        QVERIFY(s.location(QStringLiteral("jokioinen")));
        QVERIFY(!s.location(QStringLiteral("Tampere")));
    }

    void proxyFromUrl()
    {
        const QNetworkProxy p = sp::proxyFromUrl(QUrl(QStringLiteral("http://user:pw@proxy.example.com:3128")));
        QCOMPARE(p.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(p.hostName(), QStringLiteral("proxy.example.com"));
        QCOMPARE(p.port(), quint16(3128));
        QCOMPARE(p.user(), QStringLiteral("user"));
        QCOMPARE(p.password(), QStringLiteral("pw"));
        QCOMPARE(sp::proxyFromUrl(QUrl(QStringLiteral("socks5://h"))).type(), QNetworkProxy::Socks5Proxy);
        QCOMPARE(sp::proxyFromUrl(QUrl()).type(), QNetworkProxy::NoProxy);
    }

    void applyManualProxy()
    {
        // v3 style address without scheme.
        QVERIFY(sp::applyProxy({sp::ProxyMode::Manual, QStringLiteral("proxy.example.com:8080")}).isEmpty());
        QCOMPARE(QNetworkProxy::applicationProxy().hostName(), QStringLiteral("proxy.example.com"));
        QCOMPARE(QNetworkProxy::applicationProxy().port(), quint16(8080));

        QVERIFY(!sp::applyProxy({sp::ProxyMode::Manual, QStringLiteral("http://")}).isEmpty());
        QCOMPARE(QNetworkProxy::applicationProxy().type(), QNetworkProxy::NoProxy);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
