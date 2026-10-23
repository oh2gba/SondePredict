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
        QVERIFY(s.wind.useMeps);
        QVERIFY(s.wind.averageMeps);
        QVERIFY(!s.schedule);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sub/dir/sondepredict.ini")); // created on save

        sp::AppSettings s;
        s.locations << sp::Location{QStringLiteral("Jokioinen"), {60.814, 23.499}, 104.0, 30.0}
                    << sp::Location{QStringLiteral("Home, Espoo"), {60.2055, 24.6559}, 25.0, 40.0}
                    << sp::Location{QStringLiteral("Sodankylä"), {67.367, 26.63}, 179.0, 30.0,
                                    sp::SiteBalloon{4.6, false, 28700, 5.2, 0.5, 0.45, 2260, 0.2}};
        s.profile.ascentRate = 5.7;
        s.profile.burstAltitude = 31000;
        s.profile.descentModel = sp::DescentModel::Legacy;
        s.profile.legacyDescentRate = 9.9;
        s.profile.legacyDescentGain = 0.63;
        s.network = {sp::ProxyMode::Manual, QStringLiteral("http://user:pw@proxy:3128")};
        s.wind = {false, false, QStringLiteral("https://example.org/v1/gfs"), QStringLiteral("key"), QStringLiteral("gfs_seamless"), 0.5};
        s.general = {false, false};
        s.schedule = sp::ScheduleRule{{{QStringLiteral("Jokioinen"), {QTime(23, 30), QTime(5, 30), QTime(11, 30)}},
                                       {QStringLiteral("Sodankylä"), {QTime(11, 15)}}},
                                      QStringLiteral("Home, Espoo"), {0, 1, 2}, 6, true};
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
        QVERIFY(!r.wind.useMeps);
        QVERIFY(!r.wind.averageMeps);
        QCOMPARE(r.wind.apiKey, QStringLiteral("key"));
        QCOMPARE(r.general.overwritePredictions, false);
        QVERIFY(r.schedule);
        QCOMPARE(r.schedule->days, (QList<int>{0, 1, 2}));
        QCOMPARE(r.schedule->sites.size(), 2);
        QCOMPARE(r.schedule->sites[0].siteName, QStringLiteral("Jokioinen"));
        QCOMPARE(r.schedule->sites[0].times, (QList<QTime>{QTime(5, 30), QTime(11, 30), QTime(23, 30)})); // sorted
        QCOMPARE(r.schedule->sites[1].siteName, QStringLiteral("Sodankylä"));
        QCOMPARE(r.schedule->sites[1].times, (QList<QTime>{QTime(11, 15)}));
        QCOMPARE(r.schedule->homeName, QStringLiteral("Home, Espoo"));
        QCOMPARE(r.schedule->intervalHours, 6);
        QCOMPARE(r.schedule->autoRun, true);
        // A site's own balloon values, and the general ones for sites without.
        QVERIFY(!r.locations[0].balloon);
        QVERIFY(r.locations[2].balloon);
        QCOMPARE(r.locations[2].balloon->burstAltitude, 28700.0);
        QCOMPARE(r.locations[2].balloon->shapedAscent, false);
        QCOMPARE(r.locations[2].balloon->descentSd, 0.2);
        const sp::FlightProfile sod = r.profileFor(&r.locations[2]);
        QCOMPARE(sod.ascentRate, 4.6);
        QCOMPARE(sod.seaLevelDescentRate, 5.2);
        QCOMPARE(sod.descentExponent, 0.5);
        QCOMPARE(sod.descentModel, sp::DescentModel::DensityScaled);
        QCOMPARE(r.spreadFor(&r.locations[2]).burstAltitude, 2260.0);
        QCOMPARE(r.profileFor(&r.locations[0]).ascentRate, 5.7);
        QCOMPARE(r.profileFor(nullptr).burstAltitude, 31000.0);
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
        // A batch as Sonde Predict 5.0 wrote it: one site, hours and one minute.
        f.write("[schedule]\nsite=Jokioinen\ndays=0, 1\nhours=\"0,12\"\nminute=30\n");
        f.close();
        const sp::AppSettings s = sp::AppSettings::load(path);
        QVERIFY(s.schedule);
        QCOMPARE(s.schedule->days, (QList<int>{0, 1}));
        QCOMPARE(s.schedule->sites.size(), 1);
        QCOMPARE(s.schedule->sites[0].siteName, QStringLiteral("Jokioinen"));
        QCOMPARE(s.schedule->sites[0].times, (QList<QTime>{QTime(0, 30), QTime(12, 30)}));
    }

    void parsesLaunchTimes()
    {
        QCOMPARE(sp::parseLaunchTimes(QStringLiteral("23:30, 5:30;11:30 0530")),
                 std::optional(QList<QTime>{QTime(5, 30), QTime(11, 30), QTime(23, 30)}));
        QCOMPARE(sp::parseLaunchTimes(QString()), std::optional(QList<QTime>{}));
        QVERIFY(!sp::parseLaunchTimes(QStringLiteral("11:30, noon")));
        QVERIFY(!sp::parseLaunchTimes(QStringLiteral("25:00")));
        QCOMPARE(sp::formatLaunchTimes({QTime(23, 30), QTime(5, 0)}), QStringLiteral("05:00, 23:30"));
    }

    void readsOldGeneralSection()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("old.ini"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[%General]\noverwrite_predictions=false\nuse_utc=false\n");
        f.close();
        const sp::AppSettings s = sp::AppSettings::load(path);
        QCOMPARE(s.general.overwritePredictions, false);
        QCOMPARE(s.general.useUtc, false);

        QVERIFY(s.save(path));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray saved = f.readAll();
        QVERIFY(saved.contains("[options]"));
        QVERIFY(!saved.contains("%General"));
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
