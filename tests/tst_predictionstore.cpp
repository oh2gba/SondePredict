#include "predictionstore.h"
#include "testfields.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

namespace {

constexpr qint64 kLaunch = 1792753200; // 2026-10-23 11:00 UTC

sp::PredictionRecord makeRecord(qint64 launchTime, const QString &site = QStringLiteral("Jokioinen"))
{
    sp::WindField field(0.25);
    sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, launchTime / 3600, launchTime / 3600 + 4, sptest::uniform(8.0, 2.0));

    sp::PredictionRecord rec;
    rec.siteName = site;
    rec.homeName = QStringLiteral("Home");
    rec.home = sp::GeoPoint{60.2055, 24.6559};
    rec.model = QStringLiteral("gfs_global");
    rec.prediction.params.site = {60.814, 23.499};
    rec.prediction.params.siteAltitude = 104.0;
    rec.prediction.params.launchTime = launchTime;
    rec.prediction.result = sp::simulate(rec.prediction.params, field);
    rec.prediction.passes = 2;
    rec.prediction.nodesFetched = 20;
    return rec;
}

} // namespace

class TestPredictionStore : public QObject {
    Q_OBJECT
private slots:
    void createsSchema()
    {
        QTemporaryDir dir;
        sp::PredictionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("new/predictions.sqlite"))), qPrintable(store.lastError()));
        QCOMPARE(store.schemaVersion(), sp::PredictionStore::kSchemaVersion);
        QVERIFY(store.list().empty());
    }

    void saveAndLoadRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("p.sqlite"));
        const sp::PredictionRecord original = makeRecord(kLaunch);
        QVERIFY(original.prediction.result.complete());

        qint64 id;
        {
            sp::PredictionStore store;
            QVERIFY(store.open(path));
            id = store.save(original, true);
            QVERIFY2(id > 0, qPrintable(store.lastError()));
        }

        sp::PredictionStore store; // reopen from disk
        QVERIFY(store.open(path));
        const auto loaded = store.load(id);
        QVERIFY2(loaded, qPrintable(store.lastError()));
        QCOMPARE(loaded->siteName, original.siteName);
        QCOMPARE(loaded->homeName, original.homeName);
        QCOMPARE(loaded->home->lat, original.home->lat);
        QCOMPARE(loaded->model, original.model);
        QVERIFY(loaded->createdAt > 0);
        QCOMPARE(loaded->prediction.params.launchTime, kLaunch);
        QCOMPARE(*loaded->prediction.params.siteAltitude, 104.0);
        QCOMPARE(loaded->prediction.params.profile.descentExponent, original.prediction.params.profile.descentExponent);
        QCOMPARE(loaded->prediction.passes, 2);

        const auto &a = original.prediction.result;
        const auto &b = loaded->prediction.result;
        QCOMPARE(b.track.size(), a.track.size());
        QCOMPARE(b.track[5].position.lat, a.track[5].position.lat);
        QCOMPARE(b.track[5].u, a.track[5].u);
        QCOMPARE(b.track.back().phase, sp::FlightPhase::Descent);
        QVERIFY(b.burst);
        QCOMPARE(b.burst->time, a.burst->time);
        QCOMPARE(b.burst->altitude, a.burst->altitude);
        QCOMPARE(b.landing->position.lon, a.landing->position.lon);

        const auto summaries = store.list();
        QCOMPARE(summaries.size(), std::size_t(1));
        const double expected = sp::inverse(*original.home, a.landing->position).distance;
        QCOMPARE(*summaries[0].distanceFromHome, expected);
        QCOMPARE(summaries[0].landing.lat, a.landing->position.lat);
    }

    void replaceExistingPrediction()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("p.sqlite"));
        sp::PredictionStore store;
        QVERIFY(store.open(path));
        const qint64 first = store.save(makeRecord(kLaunch), true);
        const qint64 other = store.save(makeRecord(kLaunch, QStringLiteral("Tikkakoski")), true);
        const qint64 second = store.save(makeRecord(kLaunch), true);
        QVERIFY(first && other && second);
        QVERIFY(!store.load(first)); // replaced
        QCOMPARE(store.list().size(), std::size_t(2));

        store.save(makeRecord(kLaunch), false); // keep both
        QCOMPARE(store.list().size(), std::size_t(3));

        // Track points of the replaced prediction are gone too (foreign key cascade).
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("check"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM track_points WHERE prediction_id NOT IN (SELECT id FROM predictions)")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(DISTINCT prediction_id) FROM track_points")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 3);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("check"));
    }

    void listsNewestLaunchFirstAndRemoves()
    {
        QTemporaryDir dir;
        sp::PredictionStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("p.sqlite"))));
        const qint64 a = store.save(makeRecord(kLaunch), true);
        const qint64 b = store.save(makeRecord(kLaunch + 12 * 3600), true);
        const qint64 c = store.save(makeRecord(kLaunch - 12 * 3600), true);

        auto list = store.list();
        QCOMPARE(list.size(), std::size_t(3));
        QCOMPARE(list[0].id, b);
        QCOMPARE(list[1].id, a);
        QCOMPARE(list[2].id, c);

        QVERIFY(store.remove({a, c}));
        list = store.list();
        QCOMPARE(list.size(), std::size_t(1));
        QCOMPARE(list[0].id, b);
        QVERIFY(!store.load(a));
    }

    void rejectsUnlandedPrediction()
    {
        QTemporaryDir dir;
        sp::PredictionStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("p.sqlite"))));
        sp::PredictionRecord rec = makeRecord(kLaunch);
        rec.prediction.result.landing.reset();
        QCOMPARE(store.save(rec, true), qint64(0));
        QVERIFY(!store.lastError().isEmpty());
    }

    void refusesNewerSchema()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("p.sqlite"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("future"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery(db).exec(QStringLiteral("PRAGMA user_version = 99"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("future"));

        sp::PredictionStore store;
        QVERIFY(!store.open(path));
        QVERIFY(store.lastError().contains(QStringLiteral("newer")));
        QVERIFY(!store.isOpen());
    }
};

QTEST_GUILESS_MAIN(TestPredictionStore)
#include "tst_predictionstore.moc"
