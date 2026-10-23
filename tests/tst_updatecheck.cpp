#include "updatecheck.h"

#include <QCoreApplication>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

class TestUpdateCheck : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setApplicationVersion(QStringLiteral("5.0.2"));
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }

    void versionOrder()
    {
        using sp::UpdateCheck;
        QCOMPARE(UpdateCheck::compareVersions("5.0.2", "5.0.2"), 0);
        QCOMPARE(UpdateCheck::compareVersions("5.0.10", "5.0.9"), 1);
        QCOMPARE(UpdateCheck::compareVersions("5.1", "5.0.9"), 1);
        QCOMPARE(UpdateCheck::compareVersions("5.0", "5.0.0"), 0);
        QCOMPARE(UpdateCheck::compareVersions("5.0.0", "10.0.0"), -1);
        QCOMPARE(UpdateCheck::compareVersions("5.1.0-rc1", "5.1.0"), -1);
        QCOMPARE(UpdateCheck::compareVersions("5.1.0", "5.1.0-rc1"), 1);
        QCOMPARE(UpdateCheck::compareVersions(" 5.0.2 ", "5.0.2"), 0);
    }

    void parsing()
    {
        using sp::UpdateCheck;
        auto r = UpdateCheck::parseResponse(
            "{\"version\":\"5.1.0\",\"url\":\"https://github.com/oh2gba/SondePredict/releases/latest\",\"message\":\"MEPS fixed\"}",
            "5.0.2");
        QVERIFY(r.valid);
        QVERIFY(r.newer);
        QCOMPARE(r.latest, QStringLiteral("5.1.0"));
        QCOMPARE(r.url, QStringLiteral("https://github.com/oh2gba/SondePredict/releases/latest"));
        QCOMPARE(r.message, QStringLiteral("MEPS fixed"));

        r = UpdateCheck::parseResponse("{\"version\":\"5.0.2\",\"url\":\"\",\"message\":\"\"}", "5.0.2");
        QVERIFY(r.valid);
        QVERIFY(!r.newer);
        r = UpdateCheck::parseResponse("{\"version\":\"5.0.1\"}", "5.0.2");
        QVERIFY(r.valid);
        QVERIFY(!r.newer); // an older answer (a test build) is not offered

        // Only https links are accepted, odd versions are rejected.
        r = UpdateCheck::parseResponse("{\"version\":\"5.0.3\",\"url\":\"http://evil.example/x\"}", "5.0.2");
        QVERIFY(r.valid);
        QVERIFY(r.url.isEmpty());
        QVERIFY(!UpdateCheck::parseResponse("{\"version\":\"<b>5</b>\"}", "5.0.2").valid);
        QVERIFY(!UpdateCheck::parseResponse("{\"url\":\"https://example.org/\"}", "5.0.2").valid);
        QVERIFY(!UpdateCheck::parseResponse("not json", "5.0.2").valid);
        QVERIFY(!UpdateCheck::parseResponse(QByteArray(), "5.0.2").valid);
    }

    void request()
    {
        using sp::UpdateCheck;
        const QUrl url = UpdateCheck::requestUrl(QUrl(QString::fromLatin1(UpdateCheck::defaultUrl)), "5.0.2", "linux-appimage");
        QCOMPARE(url.toString(), QStringLiteral("https://oh2gba.eu/sondepredict/version.php?v=5.0.2&os=linux-appimage"));
        QCOMPARE(QUrlQuery(url).queryItems().size(), 2);
        QCOMPARE(UpdateCheck::userAgent("5.0.2", "windows"), QStringLiteral("SondePredict/5.0.2 (windows)"));

        const QString p = UpdateCheck::platformName();
        QVERIFY(p == "linux" || p == "linux-flatpak" || p == "linux-appimage" || p == "windows" || p == "macos");
    }

    // A whole check, against a local file instead of the server.
    void runsAgainstLocalAnswer()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("version.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"version\":\"5.1.0\",\"url\":\"https://github.com/oh2gba/SondePredict/releases/latest\",\"message\":\"\"}");
        f.close();

        QNetworkAccessManager network;
        sp::UpdateCheck check(&network);
        QSignalSpy spy(&check, &sp::UpdateCheck::finished);
        check.run(QUrl::fromLocalFile(path));
        QVERIFY(check.isBusy());
        check.run(QUrl::fromLocalFile(path)); // ignored while the first one runs
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
        const auto r = spy.at(0).at(0).value<sp::UpdateCheck::Result>();
        QVERIFY(r.valid);
        QVERIFY(r.newer);
        QCOMPARE(r.latest, QStringLiteral("5.1.0"));
        QVERIFY(!check.isBusy());
    }

    void failedRequestGivesNoResult()
    {
        QNetworkAccessManager network;
        sp::UpdateCheck check(&network);
        QSignalSpy spy(&check, &sp::UpdateCheck::finished);
        check.run(QUrl::fromLocalFile(QStringLiteral("/nonexistent/sondepredict/version.json")));
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.at(0).at(0).value<sp::UpdateCheck::Result>().valid);
    }
};

QTEST_GUILESS_MAIN(TestUpdateCheck)
#include "tst_updatecheck.moc"
