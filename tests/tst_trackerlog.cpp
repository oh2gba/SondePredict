#include "trackerlog.h"

#include <QDateTime>
#include <QTest>
#include <QTimeZone>

namespace {

const QString kFlight2019 = QStringLiteral(SP_RECOVERED_DATA "/RS41_P4740092_2019-06-27/Radiosonde_P4740092_2019-06-27-15.csv");

} // namespace

class TestTrackerLog : public QObject {
    Q_OBJECT
private slots:
    void readsRs41TrackerCsv()
    {
        const sp::TrackerLog log = sp::readTrackerCsv(kFlight2019);
        QVERIFY2(log.error.isEmpty(), qPrintable(log.error));
        QCOMPARE(log.points.size(), std::size_t(4480));

        const sp::LoggedPoint *top = log.highest();
        QVERIFY(top);
        QCOMPARE(top->altitude, 27685.7);
        QCOMPARE(QDateTime::fromSecsSinceEpoch(top->time, QTimeZone::UTC).toString(Qt::ISODate),
                 QStringLiteral("2019-06-27T12:50:02Z"));
        QCOMPARE(log.points.back().altitude, 898.5);
    }

    void reportsMissingFile()
    {
        QVERIFY(!sp::readTrackerCsv(QStringLiteral("/nonexistent/flight.csv")).error.isEmpty());
    }

    void comparesAtLastLoggedAltitude()
    {
        const sp::TrackerLog log = sp::readTrackerCsv(kFlight2019);
        const sp::LoggedPoint &last = log.points.back();
        const sp::LoggedPoint *top = log.highest();

        // A made-up prediction that descends through 898.5 m exactly 1 km north of the
        // last logged point, 60 s late, with burst at the logged burst position.
        const sp::GeoPoint north = sp::offset(last.position, 1000.0, 0.0);
        sp::SimulationResult prediction;
        sp::TrackPoint burst{double(top->time), top->position, top->altitude + 300.0, 0, 0, sp::FlightPhase::Descent};
        prediction.track.push_back(burst);
        prediction.track.push_back({double(last.time) + 50.0, north, 1000.0, 0, 0, sp::FlightPhase::Descent});
        prediction.track.push_back({double(last.time) + 70.0, north, 797.0, 0, 0, sp::FlightPhase::Descent});
        prediction.burst = burst;

        const auto c = sp::compare(prediction, log);
        QVERIFY(c);
        QVERIFY(qAbs(c->horizontalError - 1000.0) < 0.5);
        QVERIFY(qAbs(c->timeError - 60.0) < 0.1);
        QVERIFY(qAbs(*c->burstDistance) < 0.01);
        QVERIFY(qAbs(*c->burstAltitudeError - 300.0) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TestTrackerLog)
#include "tst_trackerlog.moc"
