#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

namespace {

sp::AppSettings sample()
{
    sp::AppSettings s;
    s.locations << sp::Location{QStringLiteral("Jokioinen"), {60.814, 23.499}, 104.0, 30.0}
                << sp::Location{QStringLiteral("Home"), {60.2055, 24.6559}, 25.0, 40.0};
    s.manualRun = {QStringLiteral("Jokioinen"), QStringLiteral("Home"), 11, 15};
    s.schedule = sp::ScheduleRule{{{QStringLiteral("Jokioinen"), {QTime(11, 15), QTime(23, 15)}}}, QStringLiteral("Home"), {0, 1}, 6, false};
    return s;
}

template <typename T>
T *child(QWidget &parent, const char *name)
{
    T *w = parent.findChild<T *>(QString::fromLatin1(name));
    if (!w)
        qFatal("no child %s", name);
    return w;
}

} // namespace

class TestSettingsDialog : public QObject {
    Q_OBJECT
private slots:
    void unchangedSettingsRoundTrip()
    {
        const sp::AppSettings in = sample();
        SettingsDialog dialog(in);
        QCOMPARE(child<QListWidget>(dialog, "locationList")->count(), 2);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const sp::AppSettings &out = dialog.settings();
        QCOMPARE(out.locations.size(), 2);
        QCOMPARE(out.locations[0].position.lat, 60.814);
        QCOMPARE(out.profile.ascentRate, in.profile.ascentRate);
        QCOMPARE(out.profile.descentExponent, in.profile.descentExponent);
        QCOMPARE(out.wind.gridStep, in.wind.gridStep);
        QCOMPARE(out.wind.useMeps, in.wind.useMeps);
        QCOMPARE(out.wind.model, in.wind.model);
        QCOMPARE(out.network.proxyMode, in.network.proxyMode);
    }

    void usesALaunchSitesTypicalValues()
    {
        SettingsDialog dialog(sample());
        auto *preset = child<QComboBox>(dialog, "sitePreset");
        QVERIFY(preset->count() > 200);
        // Nearest to the sample's locations (Jokioinen) first.
        QCOMPARE(preset->currentData().toString(), QStringLiteral("02963"));
        child<QPushButton>(dialog, "useSitePreset")->click();
        dialog.accept();
        QCOMPARE(dialog.settings().profile.ascentRate, 5.1);
        QCOMPARE(dialog.settings().profile.burstAltitude, 29700.0);
        QCOMPARE(dialog.settings().profile.seaLevelDescentRate, 11.7); // fitted to Jokioinen's descents
        QCOMPARE(dialog.settings().profile.descentExponent, 0.33);
        QCOMPARE(dialog.settings().spread.burstAltitude, 1850.0);
    }

    void restoresAllDefaults()
    {
        sp::AppSettings in = sample();
        in.profile.ascentRate = 5.5;
        in.profile.burstAltitude = 28000;
        in.profile.shapedAscent = false;
        in.spread.burstAltitude = 4000;
        in.wind.model = QStringLiteral("gfs_global");
        in.wind.useMeps = false;
        in.general.useUtc = false;
        SettingsDialog dialog(in);
        // Answer "Yes" to the confirmation.
        QTimer::singleShot(0, []() {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        child<QPushButton>(dialog, "restoreAll")->click();
        dialog.accept();
        const sp::AppSettings defaults;
        const sp::AppSettings &out = dialog.settings();
        QCOMPARE(out.profile.ascentRate, defaults.profile.ascentRate);
        QCOMPARE(out.profile.burstAltitude, defaults.profile.burstAltitude);
        QCOMPARE(out.profile.shapedAscent, true);
        QCOMPARE(out.spread.burstAltitude, defaults.spread.burstAltitude);
        QCOMPARE(out.wind.model, defaults.wind.model);
        QVERIFY(out.wind.useMeps);
        QVERIFY(out.general.useUtc);
        QCOMPARE(out.locations.size(), in.locations.size()); // kept
    }

    void turnsMepsOff()
    {
        SettingsDialog dialog(sample());
        QVERIFY(child<QCheckBox>(dialog, "useMeps")->isChecked());
        child<QCheckBox>(dialog, "useMeps")->setChecked(false);
        dialog.accept();
        QVERIFY(!dialog.settings().wind.useMeps);
    }

    void turnsVersionCheckOff()
    {
        sp::AppSettings in = sample();
        in.general.updateUrl = QStringLiteral("http://localhost:8765/version.php");
        SettingsDialog dialog(in);
        QVERIFY(child<QCheckBox>(dialog, "checkForUpdates")->isChecked());
        child<QCheckBox>(dialog, "checkForUpdates")->setChecked(false);
        dialog.accept();
        QVERIFY(!dialog.settings().general.checkForUpdates);
        QCOMPARE(dialog.settings().general.updateUrl, in.general.updateUrl); // not in the dialog, kept
    }

    void addsLocation()
    {
        SettingsDialog dialog(sample());
        child<QPushButton>(dialog, "locNew")->click();
        child<QLineEdit>(dialog, "locName")->setText(QStringLiteral("Sodankylä"));
        child<QDoubleSpinBox>(dialog, "locLat")->setValue(67.367);
        child<QDoubleSpinBox>(dialog, "locLon")->setValue(26.63);
        child<QSpinBox>(dialog, "locAlt")->setValue(179);
        child<QPushButton>(dialog, "locSave")->click();
        QCOMPARE(child<QListWidget>(dialog, "locationList")->count(), 3);
        dialog.accept();
        QCOMPARE(dialog.settings().locations.size(), 3);
        QCOMPARE(dialog.settings().locations[2].name, QStringLiteral("Sodankylä"));
        QCOMPARE(dialog.settings().locations[2].altitude, 179.0);
    }

    void renameKeepsReferences()
    {
        SettingsDialog dialog(sample());
        child<QListWidget>(dialog, "locationList")->setCurrentRow(1);
        child<QLineEdit>(dialog, "locName")->setText(QStringLiteral("Home, Espoo"));
        child<QPushButton>(dialog, "locSave")->click();
        dialog.accept();
        const sp::AppSettings &s = dialog.settings();
        QCOMPARE(s.locations[1].name, QStringLiteral("Home, Espoo"));
        QCOMPARE(s.manualRun.homeName, QStringLiteral("Home, Espoo"));
        QCOMPARE(s.schedule->homeName, QStringLiteral("Home, Espoo"));
    }

    void renamingABatchSite()
    {
        SettingsDialog dialog(sample());
        child<QListWidget>(dialog, "locationList")->setCurrentRow(0);
        child<QLineEdit>(dialog, "locName")->setText(QStringLiteral("Jokioinen Ilmala"));
        child<QPushButton>(dialog, "locSave")->click();
        QCOMPARE(child<QComboBox>(dialog, "batchSite0")->currentText(), QStringLiteral("Jokioinen Ilmala"));
        dialog.accept();
        QCOMPARE(dialog.settings().schedule->sites[0].siteName, QStringLiteral("Jokioinen Ilmala"));
        QCOMPARE(dialog.settings().schedule->sites[0].times, (QList<QTime>{QTime(11, 15), QTime(23, 15)}));
    }

    void rejectsDuplicateName()
    {
        SettingsDialog dialog(sample());
        child<QPushButton>(dialog, "locNew")->click();
        child<QLineEdit>(dialog, "locName")->setText(QStringLiteral("jokioinen"));
        closeNextMessageBox();
        child<QPushButton>(dialog, "locSave")->click();
        QCOMPARE(child<QListWidget>(dialog, "locationList")->count(), 2);
    }

    void unsavedLocationIsKeptOnOk()
    {
        SettingsDialog dialog(sample());
        child<QPushButton>(dialog, "locNew")->click();
        child<QLineEdit>(dialog, "locName")->setText(QStringLiteral("Cabin"));
        child<QDoubleSpinBox>(dialog, "locLat")->setValue(61.5);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.settings().locations.size(), 3);
        QCOMPARE(dialog.settings().locations[2].name, QStringLiteral("Cabin"));
    }

    void removesLocation()
    {
        SettingsDialog dialog(sample());
        child<QListWidget>(dialog, "locationList")->setCurrentRow(0);
        child<QPushButton>(dialog, "locRemove")->click();
        dialog.accept();
        QCOMPARE(dialog.settings().locations.size(), 1);
        QCOMPARE(dialog.settings().locations[0].name, QStringLiteral("Home"));
    }

    void balloonSettings()
    {
        SettingsDialog dialog(sample());
        child<QDoubleSpinBox>(dialog, "ascentRate")->setValue(5.7);
        child<QSpinBox>(dialog, "burstAltitude")->setValue(31000);
        child<QRadioButton>(dialog, "legacyModel")->setChecked(true);
        dialog.accept();
        QCOMPARE(dialog.settings().profile.ascentRate, 5.7);
        QCOMPARE(dialog.settings().profile.burstAltitude, 31000.0);
        QCOMPARE(dialog.settings().profile.descentModel, sp::DescentModel::Legacy);
    }

    void batchTab()
    {
        sp::AppSettings in = sample();
        in.schedule->autoRun = true;
        SettingsDialog dialog(in);
        QCOMPARE(child<QComboBox>(dialog, "batchSite0")->currentText(), QStringLiteral("Jokioinen"));
        QCOMPARE(child<QLineEdit>(dialog, "batchTimes0")->text(), QStringLiteral("11:15, 23:15"));
        QCOMPARE(child<QComboBox>(dialog, "batchHome")->currentText(), QStringLiteral("Home"));
        auto *days = child<QListWidget>(dialog, "batchDays");
        QCOMPARE(days->item(1)->checkState(), Qt::Checked);
        days->item(2)->setCheckState(Qt::Checked);
        child<QLineEdit>(dialog, "batchTimes0")->setText(QStringLiteral("23:30, 5:30 11:30"));
        // A second site: the next location, with launch times filled in.
        child<QPushButton>(dialog, "batchAddSite")->click();
        QCOMPARE(child<QComboBox>(dialog, "batchSite1")->currentText(), QStringLiteral("Home"));
        QVERIFY(!child<QLineEdit>(dialog, "batchTimes1")->text().isEmpty());
        QVERIFY(child<QLabel>(dialog, "batchSummary")->text().contains(QStringLiteral("Jokioinen at 05:30, 11:30, 23:30")));
        dialog.accept();
        const auto &rule = dialog.settings().schedule;
        QVERIFY(rule);
        QCOMPARE(rule->days, (QList<int>{0, 1, 2}));
        QCOMPARE(rule->sites.size(), 2);
        QCOMPARE(rule->sites[0].times, (QList<QTime>{QTime(5, 30), QTime(11, 30), QTime(23, 30)}));
        QCOMPARE(rule->sites[1].siteName, QStringLiteral("Home"));
        QCOMPARE(rule->intervalHours, 6);
        QVERIFY(rule->autoRun); // kept: switched on and off in the main window
    }

    void batchRefusesUnreadableTimes()
    {
        SettingsDialog dialog(sample());
        child<QLineEdit>(dialog, "batchTimes0")->setText(QStringLiteral("11:15, noon"));
        closeNextMessageBox();
        dialog.accept();
        QVERIFY(dialog.result() != int(QDialog::Accepted));
        QCOMPARE(child<QTabWidget>(dialog, "tabs")->currentIndex(), 2); // shows the Batch tab
    }

    void removingABatchSite()
    {
        SettingsDialog dialog(sample());
        child<QPushButton>(dialog, "batchRemoveSite")->click();
        dialog.accept();
        QVERIFY(!dialog.settings().schedule); // no sites, no batch
    }

    void batchKeepsMissingLocation()
    {
        SettingsDialog dialog(sample());
        // Remove the batch's launch site: the batch must not silently move to "Home".
        child<QListWidget>(dialog, "locationList")->setCurrentRow(0);
        child<QPushButton>(dialog, "locRemove")->click();
        QCOMPARE(child<QComboBox>(dialog, "batchSite0")->currentText(), QStringLiteral("Jokioinen"));
        dialog.accept();
        QCOMPARE(dialog.settings().schedule->sites[0].siteName, QStringLiteral("Jokioinen"));
    }

    void locationWithItsOwnBalloon()
    {
        SettingsDialog dialog(sample());
        child<QListWidget>(dialog, "locationList")->setCurrentRow(0);
        auto *own = child<QCheckBox>(dialog, "locOwnBalloon");
        QVERIFY(!own->isChecked());
        own->setChecked(true);
        child<QDoubleSpinBox>(dialog, "locAscent")->setValue(4.6);
        child<QSpinBox>(dialog, "locBurst")->setValue(28700);
        child<QDoubleSpinBox>(dialog, "locDescent")->setValue(5.0);
        child<QPushButton>(dialog, "locSave")->click();
        dialog.accept();
        const sp::Location &jokioinen = dialog.settings().locations[0];
        QVERIFY(jokioinen.balloon);
        QCOMPARE(jokioinen.balloon->ascentRate, 4.6);
        QCOMPARE(jokioinen.balloon->burstAltitude, 28700.0);
        QCOMPARE(dialog.settings().profileFor(&jokioinen).seaLevelDescentRate, 5.0);
        QVERIFY(!dialog.settings().locations[1].balloon);
    }

    void emptyBatchIsNotStored()
    {
        sp::AppSettings in = sample();
        in.schedule.reset();
        SettingsDialog dialog(in);
        dialog.accept();
        QVERIFY(!dialog.settings().schedule);
    }

    void manualProxyNeedsAddress()
    {
        SettingsDialog dialog(sample());
        child<QRadioButton>(dialog, "manualProxy")->setChecked(true);
        child<QLineEdit>(dialog, "proxyUrl")->clear();
        closeNextMessageBox();
        dialog.accept();
        QVERIFY(dialog.result() != int(QDialog::Accepted));

        child<QLineEdit>(dialog, "proxyUrl")->setText(QStringLiteral("proxy.example.com:8080"));
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.settings().network.proxyMode, sp::ProxyMode::Manual);
    }

    // Saves a picture of every tab when SP_TEST_SCREENSHOTS names a directory.
    void screenshots()
    {
        const QString dir = qEnvironmentVariable("SP_TEST_SCREENSHOTS");
        if (dir.isEmpty())
            QSKIP("set SP_TEST_SCREENSHOTS to a directory to save screenshots");
        // Two stations, one with its own balloon, both in the batch.
        sp::AppSettings s = sample();
        s.locations.insert(1, sp::Location{QStringLiteral("Sodankylä"), {67.3667, 26.6289}, 179.0, 30.0,
                                           sp::SiteBalloon{4.6, true, 28700, 13.4, 0.33, 0.45, 2250, 0.17}});
        s.schedule = sp::ScheduleRule{{{QStringLiteral("Jokioinen"), {QTime(5, 30), QTime(11, 30), QTime(23, 30)}},
                                       {QStringLiteral("Sodankylä"), {QTime(11, 15), QTime(23, 15)}}},
                                      QStringLiteral("Home"), {0, 1}, 6, false};
        SettingsDialog dialog(s);
        dialog.show();
        child<QListWidget>(dialog, "locationList")->setCurrentRow(1);
        auto *tabs = child<QTabWidget>(dialog, "tabs");
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(50);
            QVERIFY(dialog.grab().save(QDir(dir).filePath(QStringLiteral("settings-%1.png").arg(i))));
        }
    }

private:
    // Message boxes from the dialog are modal; close the next one as soon as it opens.
    void closeNextMessageBox()
    {
        QTimer::singleShot(0, []() {
            if (QWidget *box = QApplication::activeModalWidget())
                box->close();
        });
    }
};

QTEST_MAIN(TestSettingsDialog)
#include "tst_settingsdialog.moc"
