#pragma once

#include "settings.h"

#include <QDialog>

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

    // Locations
    void refreshLocationList(const QString &select = {});
    void loadLocationForm(int row);
    bool saveLocation();
    bool locationFormChanged() const;
    void removeLocation();
    void newLocation();

    // Balloon
    void loadProfile(const sp::FlightProfile &profile);
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

    QDoubleSpinBox *m_ascent = nullptr;
    QSpinBox *m_burst = nullptr;
    QRadioButton *m_densityModel = nullptr;
    QRadioButton *m_legacyModel = nullptr;
    QDoubleSpinBox *m_descentRate = nullptr;
    QDoubleSpinBox *m_descentExponent = nullptr;
    QDoubleSpinBox *m_legacyRate = nullptr;
    QDoubleSpinBox *m_legacyGain = nullptr;
    QLabel *m_profilePreview = nullptr;

    QLineEdit *m_apiUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QComboBox *m_model = nullptr;
    QComboBox *m_grid = nullptr;
    QButtonGroup *m_proxyMode = nullptr;
    QLineEdit *m_proxyUrl = nullptr;

    QLineEdit *m_googleEarth = nullptr;
    QCheckBox *m_overwrite = nullptr;
    QCheckBox *m_useUtc = nullptr;
};
