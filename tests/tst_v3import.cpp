#include "v3import.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kFixture = QStringLiteral(SP_TEST_DATA "/v3_config.ini");

QString writeFile(const QTemporaryDir &dir, const char *name, const QByteArray &content)
{
    const QString path = dir.filePath(QString::fromLatin1(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write %s", qPrintable(path));
    f.write(content);
    return path;
}

} // namespace

class TestV3Import : public QObject {
    Q_OBJECT
private slots:
    void parsesConfigObjSyntax()
    {
        QString error;
        const auto root = sp::configobj::parse(QStringLiteral(
            "# comment\n"
            "top = 1\n"
            "[a]\n"
            "  x = \"quoted # not a comment\"   # comment\n"
            "  list = 1, 2, 3\n"
            "  [[b]]\n"
            "  y = 'single'\n"
            "  [[c]]\n"
            "[d]\n"
            "z = 'one, two', three\n"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(root.value(QStringLiteral("top")), QStringLiteral("1"));
        const auto *a = root.section(QStringLiteral("a"));
        QVERIFY(a);
        QCOMPARE(a->value(QStringLiteral("x")), QStringLiteral("quoted # not a comment"));
        QCOMPARE(sp::configobj::toList(a->value(QStringLiteral("list"))), (QStringList{"1", "2", "3"}));
        QCOMPARE(a->sections.size(), std::size_t(2));
        QCOMPARE(a->section(QStringLiteral("b"))->value(QStringLiteral("y")), QStringLiteral("single"));
        QVERIFY(!a->section(QStringLiteral("d")));
        QCOMPARE(sp::configobj::toList(root.section(QStringLiteral("d"))->value(QStringLiteral("z"))),
                 (QStringList{"one, two", "three"}));
    }

    void listEdgeCases()
    {
        QCOMPARE(sp::configobj::toList(QStringLiteral("11,")), QStringList{"11"});
        QCOMPARE(sp::configobj::toList(QStringLiteral(",")), QStringList());
        QCOMPARE(sp::configobj::toList(QString()), QStringList());
    }

    void rejectsBadStructure()
    {
        QString error;
        sp::configobj::parse(QStringLiteral("[[orphan]]\nx = 1\n"), &error);
        QVERIFY(!error.isEmpty());
        error.clear();
        sp::configobj::parse(QStringLiteral("[a]\njust words\n"), &error);
        QVERIFY(error.contains(QStringLiteral("line 2")));
    }

    void importsV3Config()
    {
        sp::AppSettings s;
        const sp::V3ImportResult r = sp::importV3Config(kFixture, s);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));

        // Three valid [locations] plus "Cabin", which only exists in the schedule.
        QCOMPARE(r.locationsAdded, 4);
        QCOMPARE(s.locations.size(), 4);
        QCOMPARE(s.locations[0].name, QStringLiteral("Jokioinen"));
        QCOMPARE(s.locations[0].position.lat, 60.814);
        QCOMPARE(s.locations[0].altitude, 104.0);
        QCOMPARE(s.locations[1].name, QStringLiteral("Home, Espoo"));
        QCOMPARE(s.locations[1].warningDistanceKm, 40.0);
        QCOMPARE(s.locations[2].name, QStringLiteral("Sodankylä"));
        QVERIFY(s.location(QStringLiteral("Cabin")));
        QVERIFY(!s.location(QStringLiteral("Broken")));
        QVERIFY(r.messages.join(u'\n').contains(QStringLiteral("[[3]]")));

        QCOMPARE(s.profile.ascentRate, 5.7);
        QCOMPARE(s.profile.burstAltitude, 28000.0);
        QCOMPARE(s.profile.descentModel, sp::DescentModel::Legacy); // tuned values
        QCOMPARE(s.profile.legacyDescentRate, 10.0);
        QCOMPARE(s.profile.legacyDescentGain, 0.6);
        QCOMPARE(s.wind.gridStep, 0.5);

        QCOMPARE(s.network.proxyMode, sp::ProxyMode::Manual);
        QCOMPARE(s.network.proxyUrl, QStringLiteral("http://proxy.example.com:8080/"));
        QCOMPARE(s.general.googleEarthPath, QStringLiteral("C:\\Program Files\\Google\\Google Earth\\client\\googleearth.exe"));
        QCOMPARE(s.general.useUtc, false);

        QVERIFY(r.scheduleImported);
        QCOMPARE(s.schedule->siteName, QStringLiteral("Jokioinen"));
        QCOMPARE(s.schedule->homeName, QStringLiteral("Cabin"));
        QCOMPARE(s.schedule->days, (QList<int>{0, 1, 2}));
        QCOMPARE(s.schedule->hours, (QList<int>{11, 23}));
        QCOMPARE(s.schedule->intervalHours, 6);

        QCOMPARE(s.manualRun.siteName, QStringLiteral("Jokioinen"));
        QCOMPARE(s.manualRun.homeName, QStringLiteral("Home, Espoo"));
        QCOMPARE(s.manualRun.hour, 23);
    }

    void keepsExistingLocations()
    {
        sp::AppSettings s;
        s.locations << sp::Location{QStringLiteral("Jokioinen"), {60.8, 23.5}, 110.0, 25.0};
        const sp::V3ImportResult r = sp::importV3Config(kFixture, s);
        QCOMPARE(r.locationsSkipped, 1);
        QCOMPARE(s.location(QStringLiteral("Jokioinen"))->altitude, 110.0);
    }

    void defaultDescentKeepsDensityModel()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "config.ini",
            "[track_settings]\nballoon_asc_rate = 5\nballoon_desc_rate = 40\nballoon_desc_gain = 1\n"
            "balloon_burst_alt = 24000\ngrid_size = 0p25\n[locations]\n");
        sp::AppSettings s;
        const sp::V3ImportResult r = sp::importV3Config(path, s);
        QVERIFY(r.error.isEmpty());
        QCOMPARE(s.profile.descentModel, sp::DescentModel::DensityScaled);
        QCOMPARE(s.profile.legacyDescentRate, 40.0);
        QCOMPARE(s.profile.burstAltitude, 24000.0);
        QVERIFY(!r.scheduleImported);
    }

    void rejectsOtherFiles()
    {
        QTemporaryDir dir;
        sp::AppSettings s;
        QVERIFY(!sp::importV3Config(writeFile(dir, "other.ini", "[something]\nx = 1\n"), s).error.isEmpty());
        QVERIFY(!sp::importV3Config(dir.filePath(QStringLiteral("missing.ini")), s).error.isEmpty());
        QVERIFY(s.locations.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestV3Import)
#include "tst_v3import.moc"
