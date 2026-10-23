#pragma once

#include "batch.h"
#include "meps.h"
#include "openmeteo.h"
#include "predictionstore.h"
#include "predictor.h"
#include "settings.h"

#include <QDateTime>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QTimer>

#include <memory>
#include <optional>

class MapWidget;
class ProfileWidget;
class QTabWidget;
class Scene3DWidget;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QSplitter;
class QPlainTextEdit;
class QTextBrowser;
class QTimeEdit;
class QTreeWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QString configPath, QString dbPath, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void restoreWindowState();
    void saveWindowState() const;

    void applySettings(); // proxy, wind source, controls that depend on settings
    void saveSettings();
    void refreshLocationCombos();
    void rememberManualRunChoices();

    void refreshTree(qint64 selectId = 0);
    void onSelectionChanged();
    QList<qint64> selectedIds() const;
    void showDetails(const sp::PredictionRecord &record);
    void showOverview();
    void showInViews(const std::vector<sp::PredictionRecord> &records);
    void updateCurrentView();

    std::shared_ptr<sp::WindField> windField(); // the shared cache, renewed when stale
    std::vector<sp::WindBackend> windBackends(); // MEPS if enabled, then the global model
    void logActivity(const QString &line);      // to the Activity tab, log.txt and the terminal
    void runPrediction();
    void onPredictionFinished(const sp::Prediction &prediction);
    void onPredictionFailed(const QString &error);
    void startBatch(bool scheduled);
    void onBatchJobFinished(const sp::BatchJob &job, const sp::Prediction &prediction);
    void stopRunning();
    bool isBusy() const;
    void setBusy(bool busy, const QString &message = {});
    void updateBatchControls();
    void updateScheduleTimer();
    void runDueScheduledBatch();

    std::vector<sp::PredictionRecord> selectedRecords() const;
    void exportGpx();

    void deleteSelected();
    void openSettings();
    void importV3Settings();
    void showAbout();

    std::optional<QDateTime> launchDateTime() const;
    QString formatTime(double unixTime, bool withSeconds = false) const;
    QString formatDay(qint64 unixTime) const;
    std::optional<double> warningDistanceKm(const QString &siteName, const QString &homeName) const;

    QString m_configPath;
    QString m_dbPath;
    sp::AppSettings m_settings;
    sp::PredictionStore m_store;

    QNetworkAccessManager m_network;
    std::unique_ptr<sp::OpenMeteoClient> m_client;
    std::unique_ptr<sp::MepsClient> m_meps;
    std::unique_ptr<sp::Predictor> m_predictor;
    std::unique_ptr<sp::BatchRunner> m_batch;
    bool m_busy = false; // a prediction or batch is running
    bool m_batchScheduled = false;
    int m_batchSaved = 0;
    QStringList m_batchErrors;
    QString m_batchLimitMessage; // set when the batch stopped at Open-Meteo's limit
    QTimer m_scheduleTimer;
    QDateTime m_nextScheduledRun;
    bool m_scheduledBatchDue = false;
    std::shared_ptr<sp::WindField> m_field;     // global model (latitude/longitude grid)
    std::shared_ptr<sp::WindField> m_mepsField; // MEPS (its own grid)
    QDateTime m_fieldCreated;

    struct PendingRun {
        QString siteName;
        QString homeName;
        std::optional<sp::GeoPoint> home;
    } m_pending;

    QTreeWidget *m_tree = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QComboBox *m_siteCombo = nullptr;
    QComboBox *m_homeCombo = nullptr;
    QDateEdit *m_dateEdit = nullptr;
    QTimeEdit *m_timeEdit = nullptr;
    QLabel *m_timeZoneLabel = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_batchButton = nullptr;
    QCheckBox *m_schedulerCheck = nullptr;
    QTextBrowser *m_details = nullptr;
    QPlainTextEdit *m_activity = nullptr;   // what predictions and downloads are doing
    QTabWidget *m_infoTabs = nullptr;       // Details / Activity
    MapWidget *m_map = nullptr;
    QTabWidget *m_views = nullptr;          // Map / 3D / Profile
    Scene3DWidget *m_scene3d = nullptr;     // created when its tab is first opened
    ProfileWidget *m_profile = nullptr;
    std::vector<sp::PredictionRecord> m_shown; // what the views show
    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_rightSplitter = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_clockLabel = nullptr;
    QTimer m_clock;
    QList<QAction *> m_selectionActions; // enabled while predictions are selected
};
