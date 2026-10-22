#include "settingsdialog.h"

#include "simulator.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QDoubleSpinBox *doubleSpin(double min, double max, int decimals, double step, const QString &suffix)
{
    auto *spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setSuffix(suffix);
    return spin;
}

QSpinBox *intSpin(int min, int max, int step, const QString &suffix)
{
    auto *spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setSuffix(suffix);
    return spin;
}

QString minutes(double seconds)
{
    const int m = static_cast<int>(std::lround(seconds / 60.0));
    return m >= 60 ? QObject::tr("%1 h %2 min").arg(m / 60).arg(m % 60) : QObject::tr("%1 min").arg(m);
}

QLabel *note(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);
    label->setStyleSheet(QStringLiteral("color: palette(mid);"));
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(const sp::AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Settings"));
    resize(640, 480);

    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("tabs"));
    tabs->addTab(buildLocationsTab(), tr("Locations"));
    tabs->addTab(buildBalloonTab(), tr("Balloon"));
    tabs->addTab(buildBatchTab(), tr("Batch"));
    tabs->addTab(buildWindTab(), tr("Wind data && network"));
    tabs->addTab(buildGeneralTab(), tr("General"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

// ---------------------------------------------------------------- Locations

QWidget *SettingsDialog::buildLocationsTab()
{
    m_locationList = new QListWidget;
    m_locationList->setObjectName(QStringLiteral("locationList"));
    connect(m_locationList, &QListWidget::currentRowChanged, this, &SettingsDialog::loadLocationForm);

    m_locName = new QLineEdit;
    m_locName->setObjectName(QStringLiteral("locName"));
    m_locLat = doubleSpin(-90.0, 90.0, 5, 0.01, QStringLiteral("°"));
    m_locLat->setObjectName(QStringLiteral("locLat"));
    m_locLon = doubleSpin(-180.0, 180.0, 5, 0.01, QStringLiteral("°"));
    m_locLon->setObjectName(QStringLiteral("locLon"));
    m_locAlt = intSpin(-500, 9000, 10, tr(" m"));
    m_locAlt->setObjectName(QStringLiteral("locAlt"));
    m_locWarning = intSpin(0, 1000, 5, tr(" km"));
    m_locWarning->setObjectName(QStringLiteral("locWarning"));
    m_locWarning->setToolTip(tr("Predictions landing closer than this to your location are shown in bold."));

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_locName);
    form->addRow(tr("Latitude"), m_locLat);
    form->addRow(tr("Longitude"), m_locLon);
    form->addRow(tr("Altitude"), m_locAlt);
    form->addRow(tr("Highlight within"), m_locWarning);

    auto *newButton = new QPushButton(tr("&New"));
    newButton->setObjectName(QStringLiteral("locNew"));
    m_locSave = new QPushButton(tr("&Save"));
    m_locSave->setObjectName(QStringLiteral("locSave"));
    m_locRemove = new QPushButton(tr("&Remove"));
    m_locRemove->setObjectName(QStringLiteral("locRemove"));
    connect(newButton, &QPushButton::clicked, this, &SettingsDialog::newLocation);
    connect(m_locSave, &QPushButton::clicked, this, [this]() { saveLocation(); });
    connect(m_locRemove, &QPushButton::clicked, this, &SettingsDialog::removeLocation);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(m_locRemove);
    buttons->addStretch();
    buttons->addWidget(m_locSave);

    auto *right = new QVBoxLayout;
    right->addLayout(form);
    right->addWidget(note(tr("Decimal degrees, WGS84 (as shown by GPS and most map sites). "
                             "Use a launch site's real altitude; your location's altitude is not used.")));
    right->addStretch();
    right->addLayout(buttons);

    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->addWidget(m_locationList, 2);
    layout->addLayout(right, 3);

    refreshLocationList();
    if (m_locationList->count() == 0)
        newLocation();
    return page;
}

void SettingsDialog::refreshLocationList(const QString &select)
{
    const QSignalBlocker blocker(m_locationList);
    m_locationList->clear();
    int row = m_settings.locations.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_settings.locations.size(); ++i) {
        m_locationList->addItem(m_settings.locations[i].name);
        if (m_settings.locations[i].name == select)
            row = i;
    }
    m_locationList->setCurrentRow(row);
    loadLocationForm(row);
    refreshBatchLocations();
}

void SettingsDialog::loadLocationForm(int row)
{
    m_locRemove->setEnabled(row >= 0);
    if (row < 0 || row >= m_settings.locations.size())
        return;
    const sp::Location &l = m_settings.locations[row];
    m_locName->setText(l.name);
    m_locLat->setValue(l.position.lat);
    m_locLon->setValue(l.position.lon);
    m_locAlt->setValue(qRound(l.altitude));
    m_locWarning->setValue(qRound(l.warningDistanceKm));
}

void SettingsDialog::newLocation()
{
    const QSignalBlocker blocker(m_locationList);
    m_locationList->setCurrentRow(-1);
    m_locationList->clearSelection();
    m_locName->clear();
    m_locLat->setValue(0.0);
    m_locLon->setValue(0.0);
    m_locAlt->setValue(0);
    m_locWarning->setValue(30);
    m_locRemove->setEnabled(false);
    m_locName->setFocus();
}

bool SettingsDialog::locationFormChanged() const
{
    const int row = m_locationList->currentRow();
    if (row < 0)
        return !m_locName->text().trimmed().isEmpty();
    const sp::Location &l = m_settings.locations[row];
    return m_locName->text().trimmed() != l.name || m_locLat->value() != l.position.lat || m_locLon->value() != l.position.lon
        || m_locAlt->value() != qRound(l.altitude) || m_locWarning->value() != qRound(l.warningDistanceKm);
}

bool SettingsDialog::saveLocation()
{
    const QString name = m_locName->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("Locations"), tr("Give the location a name."));
        return false;
    }
    const int row = m_locationList->currentRow();
    for (int i = 0; i < m_settings.locations.size(); ++i) {
        if (i != row && m_settings.locations[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, tr("Locations"), tr("There is already a location called \"%1\".").arg(name));
            return false;
        }
    }

    const sp::Location location{name, {m_locLat->value(), m_locLon->value()}, double(m_locAlt->value()), double(m_locWarning->value())};
    if (row >= 0) {
        // Keep references to a renamed location working.
        const QString oldName = m_settings.locations[row].name;
        for (QString *ref : {&m_settings.manualRun.siteName, &m_settings.manualRun.homeName}) {
            if (*ref == oldName)
                *ref = name;
        }
        if (m_settings.schedule) {
            for (QString *ref : {&m_settings.schedule->siteName, &m_settings.schedule->homeName}) {
                if (*ref == oldName)
                    *ref = name;
            }
        }
        for (QComboBox *combo : {m_batchSite, m_batchHome}) {
            if (const int i = combo->findText(oldName); i >= 0)
                combo->setItemText(i, name);
        }
        m_settings.locations[row] = location;
    } else {
        m_settings.locations << location;
    }
    refreshLocationList(name);
    return true;
}

void SettingsDialog::removeLocation()
{
    const int row = m_locationList->currentRow();
    if (row < 0)
        return;
    m_settings.locations.removeAt(row);
    refreshLocationList();
    if (m_settings.locations.isEmpty())
        newLocation();
}

// ---------------------------------------------------------------- Balloon

QWidget *SettingsDialog::buildBalloonTab()
{
    m_ascent = doubleSpin(0.5, 20.0, 1, 0.1, tr(" m/s"));
    m_ascent->setObjectName(QStringLiteral("ascentRate"));
    m_burst = intSpin(1000, 45000, 500, tr(" m"));
    m_burst->setObjectName(QStringLiteral("burstAltitude"));

    m_densityModel = new QRadioButton(tr("Density-scaled (recommended)"));
    m_legacyModel = new QRadioButton(tr("Sonde Predict v3 (linear)"));
    m_legacyModel->setObjectName(QStringLiteral("legacyModel"));
    m_descentRate = doubleSpin(1.0, 100.0, 1, 0.5, tr(" m/s"));
    m_descentExponent = doubleSpin(0.0, 1.0, 2, 0.01, QString());
    m_legacyRate = doubleSpin(1.0, 100.0, 1, 0.5, tr(" m/s"));
    m_legacyGain = doubleSpin(0.0, 10.0, 2, 0.05, QString());

    auto *density = new QFormLayout;
    density->addRow(tr("Descent rate at sea level"), m_descentRate);
    density->addRow(tr("Density exponent k"), m_descentExponent);
    auto *densityBox = new QWidget;
    densityBox->setLayout(density);
    auto *legacy = new QFormLayout;
    legacy->addRow(tr("Descent rate"), m_legacyRate);
    legacy->addRow(tr("Gain per km"), m_legacyGain);
    auto *legacyBox = new QWidget;
    legacyBox->setLayout(legacy);

    connect(m_densityModel, &QRadioButton::toggled, densityBox, &QWidget::setEnabled);
    connect(m_legacyModel, &QRadioButton::toggled, legacyBox, &QWidget::setEnabled);

    auto *descent = new QGroupBox(tr("Descent"));
    auto *descentLayout = new QVBoxLayout(descent);
    descentLayout->addWidget(m_densityModel);
    descentLayout->addWidget(note(tr("Speed = rate × (sea-level air density / air density)<sup>k</sup>. "
                                     "k = 0.24 and 12 m/s fit an RS41 flight from Jokioinen; k = 0.5 is the textbook parachute.")));
    descentLayout->addWidget(densityBox);
    descentLayout->addWidget(m_legacyModel);
    descentLayout->addWidget(note(tr("Speed = rate + altitude in km × gain, as in Sonde Predict v3.")));
    descentLayout->addWidget(legacyBox);

    m_profilePreview = new QLabel;
    m_profilePreview->setWordWrap(true);
    auto *defaults = new QPushButton(tr("Restore defaults"));
    connect(defaults, &QPushButton::clicked, this, [this]() { loadProfile(sp::FlightProfile{}); });

    auto *top = new QFormLayout;
    top->addRow(tr("Ascent rate"), m_ascent);
    top->addRow(tr("Burst altitude"), m_burst);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_profilePreview, 1);
    bottom->addWidget(defaults, 0, Qt::AlignBottom);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addLayout(top);
    layout->addWidget(descent);
    layout->addStretch();
    layout->addLayout(bottom);

    for (QDoubleSpinBox *spin : {m_ascent, m_descentRate, m_descentExponent, m_legacyRate, m_legacyGain})
        connect(spin, &QDoubleSpinBox::valueChanged, this, &SettingsDialog::updateProfilePreview);
    connect(m_burst, &QSpinBox::valueChanged, this, &SettingsDialog::updateProfilePreview);
    connect(m_densityModel, &QRadioButton::toggled, this, &SettingsDialog::updateProfilePreview);

    loadProfile(m_settings.profile);
    densityBox->setEnabled(m_densityModel->isChecked());
    legacyBox->setEnabled(m_legacyModel->isChecked());
    return page;
}

void SettingsDialog::loadProfile(const sp::FlightProfile &p)
{
    m_ascent->setValue(p.ascentRate);
    m_burst->setValue(qRound(p.burstAltitude));
    m_densityModel->setChecked(p.descentModel == sp::DescentModel::DensityScaled);
    m_legacyModel->setChecked(p.descentModel == sp::DescentModel::Legacy);
    m_descentRate->setValue(p.seaLevelDescentRate);
    m_descentExponent->setValue(p.descentExponent);
    m_legacyRate->setValue(p.legacyDescentRate);
    m_legacyGain->setValue(p.legacyDescentGain);
    updateProfilePreview();
}

sp::FlightProfile SettingsDialog::profileFromForm() const
{
    sp::FlightProfile p;
    p.ascentRate = m_ascent->value();
    p.burstAltitude = m_burst->value();
    p.descentModel = m_legacyModel->isChecked() ? sp::DescentModel::Legacy : sp::DescentModel::DensityScaled;
    p.seaLevelDescentRate = m_descentRate->value();
    p.descentExponent = m_descentExponent->value();
    p.legacyDescentRate = m_legacyRate->value();
    p.legacyDescentGain = m_legacyGain->value();
    return p;
}

void SettingsDialog::updateProfilePreview()
{
    if (!m_profilePreview)
        return;
    const sp::FlightProfile p = profileFromForm();
    if (const char *error = p.validationError(); *error) {
        m_profilePreview->setText(QString::fromUtf8(error));
        return;
    }
    constexpr double kLaunch = 100.0;
    const double total = sp::estimatedFlightTime(p, kLaunch);
    const double ascent = (p.burstAltitude - kLaunch) / p.ascentRate;
    m_profilePreview->setText(tr("Flight from 100 m: ascent %1, descent %2, total %3.\nDescent speed %4 m/s near the burst, %5 m/s near the ground.")
                                  .arg(minutes(ascent), minutes(total - ascent), minutes(total))
                                  .arg(p.descentRate(p.burstAltitude), 0, 'f', 1)
                                  .arg(p.descentRate(kLaunch), 0, 'f', 1));
}

// ---------------------------------------------------------------- Wind data & network

QWidget *SettingsDialog::buildWindTab()
{
    m_apiUrl = new QLineEdit(m_settings.wind.apiUrl);
    m_apiUrl->setPlaceholderText(QStringLiteral("https://api.open-meteo.com/v1/gfs"));
    m_apiKey = new QLineEdit(m_settings.wind.apiKey);
    m_apiKey->setPlaceholderText(tr("only for a paid subscription"));
    m_apiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_model = new QComboBox;
    m_model->setEditable(true);
    m_model->addItems({QStringLiteral("gfs_global"), QStringLiteral("gfs_seamless")});
    m_model->setCurrentText(m_settings.wind.model);
    m_grid = new QComboBox;
    for (double step : {0.25, 0.5, 1.0})
        m_grid->addItem(QStringLiteral("%1°").arg(step), step);
    m_grid->setCurrentIndex(std::max(0, m_grid->findData(m_settings.wind.gridStep)));

    auto *wind = new QGroupBox(tr("Wind data (Open-Meteo)"));
    auto *windForm = new QFormLayout(wind);
    windForm->addRow(tr("Server"), m_apiUrl);
    windForm->addRow(tr("API key"), m_apiKey);
    windForm->addRow(tr("Model"), m_model);
    windForm->addRow(tr("Grid spacing"), m_grid);
    windForm->addRow(note(tr("The free <a href=\"https://open-meteo.com/\">Open-Meteo</a> API is for non-commercial use, "
                             "about 40–60 new predictions a day. A coarser grid uses less of that quota.")));

    auto *system = new QRadioButton(tr("Use the system proxy settings"));
    auto *none = new QRadioButton(tr("No proxy"));
    auto *manual = new QRadioButton(tr("Use this proxy:"));
    manual->setObjectName(QStringLiteral("manualProxy"));
    m_proxyMode = new QButtonGroup(this);
    m_proxyMode->addButton(system, int(sp::ProxyMode::System));
    m_proxyMode->addButton(none, int(sp::ProxyMode::None));
    m_proxyMode->addButton(manual, int(sp::ProxyMode::Manual));
    m_proxyMode->button(int(m_settings.network.proxyMode))->setChecked(true);
    m_proxyUrl = new QLineEdit(m_settings.network.proxyUrl);
    m_proxyUrl->setObjectName(QStringLiteral("proxyUrl"));
    m_proxyUrl->setPlaceholderText(QStringLiteral("http://user:password@proxy.example.com:8080"));
    m_proxyUrl->setEnabled(manual->isChecked());
    connect(manual, &QRadioButton::toggled, m_proxyUrl, &QWidget::setEnabled);

    auto *network = new QGroupBox(tr("Network"));
    auto *networkLayout = new QVBoxLayout(network);
    networkLayout->addWidget(system);
    networkLayout->addWidget(none);
    auto *manualRow = new QHBoxLayout;
    manualRow->addWidget(manual);
    manualRow->addWidget(m_proxyUrl, 1);
    networkLayout->addLayout(manualRow);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(wind);
    layout->addWidget(network);
    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------- Batch

QWidget *SettingsDialog::buildBatchTab()
{
    m_batchSite = new QComboBox;
    m_batchSite->setObjectName(QStringLiteral("batchSite"));
    m_batchHome = new QComboBox;
    m_batchHome->setObjectName(QStringLiteral("batchHome"));

    m_batchDays = new QListWidget;
    m_batchDays->setObjectName(QStringLiteral("batchDays"));
    for (int day = 0; day <= 15; ++day) {
        const QString label = day == 0 ? tr("Today") : day == 1 ? tr("Tomorrow") : tr("In %1 days").arg(day);
        auto *item = new QListWidgetItem(label, m_batchDays);
        item->setData(Qt::UserRole, day);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    m_batchHours = new QListWidget;
    m_batchHours->setObjectName(QStringLiteral("batchHours"));
    for (int hour = 0; hour < 24; ++hour) {
        auto *item = new QListWidgetItem(QStringLiteral("%1").arg(hour, 2, 10, QChar(u'0')), m_batchHours);
        item->setData(Qt::UserRole, hour);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    m_batchMinute = intSpin(0, 59, 5, tr(" min past the hour"));
    m_batchMinute->setObjectName(QStringLiteral("batchMinute"));
    m_batchInterval = intSpin(0, 168, 1, tr(" h"));
    m_batchInterval->setObjectName(QStringLiteral("batchInterval"));
    m_batchInterval->setSpecialValueText(tr("only when started by hand"));
    m_batchSummary = new QLabel;
    m_batchSummary->setWordWrap(true);

    refreshBatchLocations();
    if (const auto &rule = m_settings.schedule) {
        m_batchSite->setCurrentText(rule->siteName);
        m_batchHome->setCurrentIndex(std::max(0, m_batchHome->findText(rule->homeName)));
        for (int i = 0; i < m_batchDays->count(); ++i)
            m_batchDays->item(i)->setCheckState(rule->days.contains(i) ? Qt::Checked : Qt::Unchecked);
        for (int i = 0; i < m_batchHours->count(); ++i)
            m_batchHours->item(i)->setCheckState(rule->hours.contains(i) ? Qt::Checked : Qt::Unchecked);
        m_batchMinute->setValue(rule->minute);
        m_batchInterval->setValue(rule->intervalHours);
    } else {
        m_batchMinute->setValue(m_settings.manualRun.minute);
    }

    for (QComboBox *combo : {m_batchSite, m_batchHome})
        connect(combo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateBatchSummary);
    for (QListWidget *list : {m_batchDays, m_batchHours})
        connect(list, &QListWidget::itemChanged, this, &SettingsDialog::updateBatchSummary);
    for (QSpinBox *spin : {m_batchMinute, m_batchInterval})
        connect(spin, &QSpinBox::valueChanged, this, &SettingsDialog::updateBatchSummary);

    auto *top = new QFormLayout;
    top->addRow(tr("Launch location"), m_batchSite);
    top->addRow(tr("Your location"), m_batchHome);

    auto *lists = new QHBoxLayout;
    for (const auto &[label, list] : {std::pair{tr("Days"), m_batchDays}, std::pair{tr("Launch hours (UTC)"), m_batchHours}}) {
        auto *column = new QVBoxLayout;
        column->addWidget(new QLabel(label));
        column->addWidget(list);
        lists->addLayout(column);
    }

    auto *bottom = new QFormLayout;
    bottom->addRow(tr("Launch time"), m_batchMinute);
    bottom->addRow(tr("Repeat every"), m_batchInterval);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addLayout(top);
    layout->addLayout(lists, 1);
    layout->addLayout(bottom);
    layout->addWidget(m_batchSummary);
    layout->addWidget(note(tr("Start a batch from the main window, or tick \"Run scheduled batch tasks\" there to repeat it "
                              "while Sonde Predict is open. Launches on the same day share downloaded wind data.")));
    updateBatchSummary();
    return page;
}

void SettingsDialog::refreshBatchLocations()
{
    if (!m_batchSite)
        return;
    const QString site = m_batchSite->currentText();
    const QString home = m_batchHome->currentText();
    const QSignalBlocker siteBlocker(m_batchSite);
    const QSignalBlocker homeBlocker(m_batchHome);
    m_batchSite->clear();
    m_batchHome->clear();
    m_batchHome->addItem(tr("(none)"));
    for (const sp::Location &l : m_settings.locations) {
        m_batchSite->addItem(l.name);
        m_batchHome->addItem(l.name);
    }
    m_batchSite->setCurrentIndex(std::max(0, m_batchSite->findText(site)));
    m_batchHome->setCurrentIndex(std::max(0, m_batchHome->findText(home)));
    updateBatchSummary();
}

std::optional<sp::ScheduleRule> SettingsDialog::scheduleFromForm() const
{
    if (m_batchSite->currentText().isEmpty())
        return std::nullopt;
    sp::ScheduleRule rule;
    rule.siteName = m_batchSite->currentText();
    rule.homeName = m_batchHome->currentIndex() > 0 ? m_batchHome->currentText() : QString();
    for (int i = 0; i < m_batchDays->count(); ++i) {
        if (m_batchDays->item(i)->checkState() == Qt::Checked)
            rule.days << m_batchDays->item(i)->data(Qt::UserRole).toInt();
    }
    for (int i = 0; i < m_batchHours->count(); ++i) {
        if (m_batchHours->item(i)->checkState() == Qt::Checked)
            rule.hours << m_batchHours->item(i)->data(Qt::UserRole).toInt();
    }
    rule.minute = m_batchMinute->value();
    rule.intervalHours = m_batchInterval->value();
    rule.autoRun = m_settings.schedule && m_settings.schedule->autoRun && rule.intervalHours > 0;
    return rule;
}

void SettingsDialog::updateBatchSummary()
{
    if (!m_batchSummary || !m_batchInterval)
        return;
    const auto rule = scheduleFromForm();
    if (!rule) {
        m_batchSummary->setText(tr("Add a location first."));
        return;
    }
    const int count = int(rule->days.size() * rule->hours.size());
    if (count == 0) {
        m_batchSummary->setText(tr("Tick at least one day and one launch hour."));
        return;
    }
    QStringList times;
    for (int h : rule->hours)
        times << QStringLiteral("%1:%2").arg(h, 2, 10, QChar(u'0')).arg(rule->minute, 2, 10, QChar(u'0'));
    QString text = tr("%n prediction(s) per batch from %1, launches at %2 UTC.", nullptr, count)
                       .arg(rule->siteName, times.join(QStringLiteral(", ")));
    if (rule->intervalHours > 0)
        text += u' ' + tr("Repeats every %n hour(s) when scheduled runs are on.", nullptr, rule->intervalHours);
    m_batchSummary->setText(text);
}

// ---------------------------------------------------------------- General

QWidget *SettingsDialog::buildGeneralTab()
{
    m_googleEarth = new QLineEdit(m_settings.general.googleEarthPath);
    auto *browse = new QPushButton(tr("Browse..."));
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Google Earth program"), m_googleEarth->text());
        if (!path.isEmpty())
            m_googleEarth->setText(path);
    });
    auto *earthRow = new QHBoxLayout;
    earthRow->addWidget(m_googleEarth, 1);
    earthRow->addWidget(browse);

    m_overwrite = new QCheckBox(tr("Replace an older prediction for the same launch, site and location"));
    m_overwrite->setChecked(m_settings.general.overwritePredictions);
    m_useUtc = new QCheckBox(tr("Show and enter times in UTC"));
    m_useUtc->setChecked(m_settings.general.useUtc);

    auto *apps = new QGroupBox(tr("Other programs"));
    auto *appsForm = new QFormLayout(apps);
    appsForm->addRow(tr("Google Earth"), earthRow);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(apps);
    layout->addWidget(m_overwrite);
    layout->addWidget(m_useUtc);
    layout->addStretch();
    return page;
}

void SettingsDialog::accept()
{
    // A location typed in but not saved yet is almost certainly meant to be kept.
    if (locationFormChanged() && !saveLocation())
        return;
    const sp::FlightProfile profile = profileFromForm();
    if (const char *error = profile.validationError(); *error) {
        QMessageBox::information(this, tr("Balloon"), QString::fromUtf8(error));
        return;
    }
    const auto proxyMode = static_cast<sp::ProxyMode>(m_proxyMode->checkedId());
    if (proxyMode == sp::ProxyMode::Manual && m_proxyUrl->text().trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Network"), tr("Enter the proxy address, or choose another proxy option."));
        return;
    }

    m_settings.profile = profile;
    m_settings.wind.apiUrl = m_apiUrl->text().trimmed();
    m_settings.wind.apiKey = m_apiKey->text().trimmed();
    m_settings.wind.model = m_model->currentText().trimmed().isEmpty() ? QStringLiteral("gfs_global") : m_model->currentText().trimmed();
    m_settings.wind.gridStep = m_grid->currentData().toDouble();
    m_settings.network.proxyMode = proxyMode;
    m_settings.network.proxyUrl = m_proxyUrl->text().trimmed();
    m_settings.general.googleEarthPath = m_googleEarth->text().trimmed();
    m_settings.general.overwritePredictions = m_overwrite->isChecked();
    m_settings.general.useUtc = m_useUtc->isChecked();
    m_settings.schedule = scheduleFromForm();
    QDialog::accept();
}
