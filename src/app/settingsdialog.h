#pragma once

#include "settings.h"
#include "sitestats.h"
#include "sondehub.h"

#include <QDialog>
#include <QNetworkAccessManager>

#include <optional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QWidget;

// Edits a copy of the settings; read it back with settings() after exec() == Accepted.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const sp::AppSettings &settings, QWidget *parent = nullptr);

    const sp::AppSettings &settings() const { return m_settings; }

    void accept() override;

private:
    QWidget *buildLocationsTab();
    QWidget *buildBalloonTab();
    QWidget *buildWindTab();
    QWidget *buildGeneralTab();
    QWidget *buildBatchTab();

    // Batch
    // Refills the batch drop-downs after locations change, keeping the chosen names.
    void refreshBatchLocations(std::optional<QString> home = std::nullopt);
    // Sites whose launch times cannot be read are listed in `invalid`.
    std::optional<sp::ScheduleRule> scheduleFromForm(QStringList *invalid = nullptr) const;
    void updateBatchSummary();
    void addBatchSiteRow(const QString &siteName, const QList<QTime> &times);
    void fillSiteCombo(QComboBox *combo, const QString &name);
    QComboBox *batchSiteCombo(int row) const;
    QLineEdit *batchTimesEdit(int row) const;
    QList<QTime> typicalLaunchTimes(const QString &siteName) const;

    // Locations
    void refreshLocationList(const QString &select = {});
    void loadLocationForm(int row);
    bool saveLocation();
    bool locationFormChanged() const;
    void removeLocation();
    void newLocation();
    void loadLocationBalloon(const std::optional<sp::SiteBalloon> &balloon);
    void showLocationBalloon(const sp::SiteBalloon &balloon);
    std::optional<sp::SiteBalloon> locationBalloonFromForm() const;
    void fillLocationBalloonFromSondeHub();

    // Balloon
    void loadProfile(const sp::FlightProfile &profile);
    void useSitePreset();
    void loadSpread(const sp::FlightSpread &spread);
    void restoreDefaults();
    sp::FlightProfile profileFromForm() const;
    void updateProfilePreview();

    sp::AppSettings m_settings;

    QListWidget *m_locationList = nullptr;
    QLineEdit *m_locName = nullptr;
    QDoubleSpinBox *m_locLat = nullptr;
    QDoubleSpinBox *m_locLon = nullptr;
    QSpinBox *m_locAlt = nullptr;
    QSpinBox *m_locWarning = nullptr;
    QPushButton *m_locSave = nullptr;
    QPushButton *m_locRemove = nullptr;
    QCheckBox *m_locOwnBalloon = nullptr;
    QWidget *m_locBalloonBox = nullptr;
    QDoubleSpinBox *m_locAscent = nullptr;
    QCheckBox *m_locShaped = nullptr;
    QSpinBox *m_locBurst = nullptr;
    QDoubleSpinBox *m_locDescent = nullptr;
    QDoubleSpinBox *m_locK = nullptr;
    QDoubleSpinBox *m_locAscentSd = nullptr;
    QSpinBox *m_locBurstSd = nullptr;
    QSpinBox *m_locDescentSd = nullptr;
    QLabel *m_locBalloonStatus = nullptr;
    int m_locRequest = 0;

    QDoubleSpinBox *m_ascent = nullptr;
    QSpinBox *m_burst = nullptr;
    QRadioButton *m_densityModel = nullptr;
    QRadioButton *m_legacyModel = nullptr;
    QDoubleSpinBox *m_descentRate = nullptr;
    QDoubleSpinBox *m_descentExponent = nullptr;
    QDoubleSpinBox *m_legacyRate = nullptr;
    QDoubleSpinBox *m_legacyGain = nullptr;
    QCheckBox *m_areaEnabled = nullptr;
    QCheckBox *m_areaWind = nullptr;
    QDoubleSpinBox *m_areaAscent = nullptr;
    QSpinBox *m_areaBurst = nullptr;
    QSpinBox *m_areaDescent = nullptr;
    QLabel *m_profilePreview = nullptr;

    QLineEdit *m_apiUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QCheckBox *m_useMeps = nullptr;
    QCheckBox *m_shapedAscent = nullptr;
    QComboBox *m_sitePreset = nullptr;
    QLabel *m_presetStatus = nullptr;
    QNetworkAccessManager m_network;
    sp::SondeHubClient *m_sondeHub = nullptr;
    int m_presetRequest = 0;
    std::vector<sp::LaunchSiteStats> m_siteStats;
    QCheckBox *m_averageMeps = nullptr;
    QComboBox *m_model = nullptr;
    QComboBox *m_grid = nullptr;
    QButtonGroup *m_proxyMode = nullptr;
    QLineEdit *m_proxyUrl = nullptr;

    QCheckBox *m_overwrite = nullptr;
    QCheckBox *m_useUtc = nullptr;
    QCheckBox *m_checkForUpdates = nullptr;

    QTabWidget *m_tabs = nullptr;
    QWidget *m_batchPage = nullptr;
    QTableWidget *m_batchSites = nullptr; // launch location | launch times
    QComboBox *m_batchHome = nullptr;
    QListWidget *m_batchDays = nullptr;
    QSpinBox *m_batchInterval = nullptr;
    QLabel *m_batchSummary = nullptr;
};
