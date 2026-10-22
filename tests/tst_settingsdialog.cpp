#include "settingsdialog.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
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
    s.schedule = sp::ScheduleRule{QStringLiteral("Jokioinen"), QStringLiteral("Home"), {0, 1}, {11, 23}, 15, 6, false};
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
        QCOMPARE(out.network.proxyMode, in.network.proxyMode);
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
        QCOMPARE(child<QComboBox>(dialog, "batchSite")->currentText(), QStringLiteral("Jokioinen"));
        QCOMPARE(child<QComboBox>(dialog, "batchHome")->currentText(), QStringLiteral("Home"));
        auto *days = child<QListWidget>(dialog, "batchDays");
        QCOMPARE(days->item(1)->checkState(), Qt::Checked);
        days->item(2)->setCheckState(Qt::Checked);
        child<QListWidget>(dialog, "batchHours")->item(23)->setCheckState(Qt::Unchecked);
        child<QSpinBox>(dialog, "batchMinute")->setValue(30);
        dialog.accept();
        const auto &rule = dialog.settings().schedule;
        QVERIFY(rule);
        QCOMPARE(rule->days, (QList<int>{0, 1, 2}));
        QCOMPARE(rule->hours, (QList<int>{11}));
        QCOMPARE(rule->minute, 30);
        QCOMPARE(rule->intervalHours, 6);
        QVERIFY(rule->autoRun); // kept: switched on and off in the main window
    }

    void batchKeepsMissingLocation()
    {
        SettingsDialog dialog(sample());
        // Remove the batch's launch site: the batch must not silently move to "Home".
        child<QListWidget>(dialog, "locationList")->setCurrentRow(0);
        child<QPushButton>(dialog, "locRemove")->click();
        QCOMPARE(child<QComboBox>(dialog, "batchSite")->currentText(), QStringLiteral("Jokioinen"));
        dialog.accept();
        QCOMPARE(dialog.settings().schedule->siteName, QStringLiteral("Jokioinen"));
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
        SettingsDialog dialog(sample());
        dialog.show();
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
