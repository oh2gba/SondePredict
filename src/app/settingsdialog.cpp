#include "settingsdialog.h"

#include "openmeteo.h"
#include "simulator.h"
#include "sitestats.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
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

// "1 prediction" / "3 predictions". The app ships no translation files, so tr()'s %n
// plurals would show a literal "(s)".
QString counted(int n, const QString &one, const QString &many)
{
    return (n == 1 ? one : many).arg(n);
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
    resize(700, 600);

    // SondeHub's launch sites, nearest to your locations first (Locations, Balloon and Batch use them).
    if (QFile file(QStringLiteral(":/data/launch-sites.json")); file.open(QIODevice::ReadOnly))
        m_siteStats = sp::parseLaunchSiteStats(file.readAll());
    std::vector<sp::GeoPoint> places;
    for (const sp::Location &l : m_settings.locations)
        places.push_back(l.position);
    sp::sortByDistance(m_siteStats, places);

    auto *tabs = new QTabWidget;
    m_tabs = tabs;
    tabs->setObjectName(QStringLiteral("tabs"));
    tabs->addTab(buildLocationsTab(), tr("Locations"));
    // The balloon page is tall; let it scroll on small screens instead of squeezing its fields.
    auto *balloonScroll = new QScrollArea;
    balloonScroll->setWidget(buildBalloonTab());
    balloonScroll->setWidgetResizable(true);
    balloonScroll->setFrameShape(QFrame::NoFrame);
    tabs->addTab(balloonScroll, tr("Balloon"));
    tabs->addTab(buildBatchTab(), tr("Batch"));
    tabs->addTab(buildWindTab(), tr("Wind data"));
    tabs->addTab(buildGeneralTab(), tr("General"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    QPushButton *restore = buttons->button(QDialogButtonBox::RestoreDefaults);
    restore->setObjectName(QStringLiteral("restoreAll"));
    restore->setToolTip(tr("Balloon, landing area, wind data and display settings back to their defaults. "
                           "Locations, the batch, the proxy and an API key stay."));
    connect(restore, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Restore defaults"),
                                  tr("Put the balloon, landing area, wind data and display settings back to their defaults?\n\n"
                                     "Your locations, the batch, the proxy and an API key stay. Nothing changes until you press OK."))
            == QMessageBox::Yes)
            restoreDefaults();
    });
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

    // Balloon values for launches from this location, when its station's balloons differ
    // from the general ones (Balloon tab).
    m_locOwnBalloon = new QCheckBox(tr("Own balloon values for launches from here"));
    m_locOwnBalloon->setObjectName(QStringLiteral("locOwnBalloon"));
    m_locAscent = doubleSpin(0.5, 20.0, 2, 0.1, tr(" m/s"));
    m_locAscent->setObjectName(QStringLiteral("locAscent"));
    m_locShaped = new QCheckBox(tr("shaped"));
    m_locShaped->setToolTip(tr("Slower around the tropopause, faster high up, as real balloons climb (see the Balloon tab)."));
    m_locBurst = intSpin(1000, 45000, 500, tr(" m"));
    m_locBurst->setObjectName(QStringLiteral("locBurst"));
    m_locDescent = doubleSpin(1.0, 100.0, 1, 0.5, tr(" m/s"));
    m_locDescent->setObjectName(QStringLiteral("locDescent"));
    m_locK = doubleSpin(0.0, 1.0, 2, 0.01, QString());
    m_locAscentSd = doubleSpin(0.0, 3.0, 2, 0.05, tr(" m/s"));
    m_locBurstSd = intSpin(0, 8000, 100, tr(" m"));
    m_locDescentSd = intSpin(0, 50, 1, tr(" %"));
    auto *fill = new QPushButton(tr("From &SondeHub"));
    fill->setObjectName(QStringLiteral("locFromSondeHub"));
    fill->setToolTip(tr("The values of the SondeHub launch site within 25 km: its flights of the last 30 days."));
    connect(fill, &QPushButton::clicked, this, &SettingsDialog::fillLocationBalloonFromSondeHub);
    m_locBalloonStatus = new QLabel;
    m_locBalloonStatus->setWordWrap(true);

    auto *ascentRow = new QHBoxLayout;
    ascentRow->addWidget(m_locAscent, 1);
    ascentRow->addWidget(m_locShaped);
    auto *descentRow = new QHBoxLayout;
    descentRow->addWidget(m_locDescent, 1);
    descentRow->addWidget(new QLabel(tr("k")));
    descentRow->addWidget(m_locK);
    auto *spreadRow = new QHBoxLayout;
    spreadRow->addWidget(m_locAscentSd);
    spreadRow->addWidget(m_locBurstSd);
    spreadRow->addWidget(m_locDescentSd);
    auto *balloonForm = new QFormLayout;
    balloonForm->addRow(tr("Ascent rate"), ascentRow);
    balloonForm->addRow(tr("Burst altitude"), m_locBurst);
    balloonForm->addRow(tr("Descent at sea level"), descentRow);
    balloonForm->addRow(tr("Variation ±"), spreadRow);
    m_locBalloonBox = new QWidget;
    auto *boxLayout = new QVBoxLayout(m_locBalloonBox);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->addLayout(balloonForm);
    connect(m_locOwnBalloon, &QCheckBox::toggled, m_locBalloonBox, &QWidget::setEnabled);

    auto *ownRow = new QHBoxLayout;
    ownRow->addWidget(m_locOwnBalloon, 1);
    ownRow->addWidget(fill);
    auto *balloon = new QGroupBox(tr("Balloon"));
    auto *balloonLayout = new QVBoxLayout(balloon);
    balloonLayout->addLayout(ownRow);
    balloonLayout->addWidget(m_locBalloonBox);
    balloonLayout->addWidget(m_locBalloonStatus);
    balloonLayout->addWidget(note(tr("Stations fly different balloons, some with a parachute. Without own values, launches "
                                     "from here use the Balloon tab's.")));

    auto *right = new QVBoxLayout;
    right->addLayout(form);
    right->addWidget(note(tr("Decimal degrees, WGS84 (as shown by GPS and most map sites). "
                             "Use a launch site's real altitude; your location's altitude is not used.")));
    right->addWidget(balloon);
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
    loadLocationBalloon(l.balloon);
}

void SettingsDialog::loadLocationBalloon(const std::optional<sp::SiteBalloon> &balloon)
{
    // Without own values the fields show the general ones, a starting point when ticked.
    sp::SiteBalloon b;
    if (balloon) {
        b = *balloon;
    } else {
        const sp::FlightProfile p = m_settings.profile;
        b.ascentRate = p.ascentRate;
        b.shapedAscent = p.shapedAscent;
        b.burstAltitude = p.burstAltitude;
        b.descentRate = p.seaLevelDescentRate;
        b.descentExponent = p.descentExponent;
        b.ascentSd = m_settings.spread.ascentRate;
        b.burstSd = m_settings.spread.burstAltitude;
        b.descentSd = m_settings.spread.descentRate;
    }
    m_locOwnBalloon->setChecked(balloon.has_value());
    m_locBalloonBox->setEnabled(balloon.has_value());
    showLocationBalloon(b);
    m_locBalloonStatus->clear();
}

void SettingsDialog::showLocationBalloon(const sp::SiteBalloon &b)
{
    m_locAscent->setValue(b.ascentRate);
    m_locShaped->setChecked(b.shapedAscent);
    m_locBurst->setValue(qRound(b.burstAltitude));
    m_locDescent->setValue(b.descentRate);
    m_locK->setValue(b.descentExponent);
    m_locAscentSd->setValue(b.ascentSd);
    m_locBurstSd->setValue(qRound(b.burstSd));
    m_locDescentSd->setValue(qRound(b.descentSd * 100.0));
}

std::optional<sp::SiteBalloon> SettingsDialog::locationBalloonFromForm() const
{
    if (!m_locOwnBalloon->isChecked())
        return std::nullopt;
    sp::SiteBalloon b;
    b.ascentRate = m_locAscent->value();
    b.shapedAscent = m_locShaped->isChecked();
    b.burstAltitude = m_locBurst->value();
    b.descentRate = m_locDescent->value();
    b.descentExponent = m_locK->value();
    b.ascentSd = m_locAscentSd->value();
    b.burstSd = m_locBurstSd->value();
    b.descentSd = m_locDescentSd->value() / 100.0;
    return b;
}

void SettingsDialog::fillLocationBalloonFromSondeHub()
{
    const sp::GeoPoint here{m_locLat->value(), m_locLon->value()};
    const sp::LaunchSiteStats *found = sp::nearestSite(m_siteStats, here);
    if (!found) {
        m_locBalloonStatus->setText(tr("SondeHub knows no launch site within 25 km of this location."));
        return;
    }
    const sp::LaunchSiteStats site = *found;
    sp::SiteBalloon base = locationBalloonFromForm().value_or(sp::SiteBalloon{});
    base.descentExponent = m_locK->value();
    m_locOwnBalloon->setChecked(true);
    showLocationBalloon(sp::siteBalloon(site, std::nullopt, base));
    m_locBalloonStatus->setText(tr("Looking up the last 30 days of flights from %1 on SondeHub…").arg(site.name));
    if (!m_sondeHub)
        m_sondeHub = new sp::SondeHubClient(&m_network, this);
    const int request = ++m_locRequest;
    m_sondeHub->recentFlights(site.id, site.position, 30, [this, request, site, base](const std::optional<sp::RecentFlightStats> &recent,
                                                                                     const QString &error) {
        if (request != m_locRequest)
            return;
        if (!recent) {
            m_locBalloonStatus->setText(tr("%1; using SondeHub's long-term averages for %2.").arg(error, site.name));
            return;
        }
        showLocationBalloon(sp::siteBalloon(site, recent, base));
        m_locBalloonStatus->setText(tr("%1, %2 flights from %3 to %4. Press Save to keep these values.")
                                        .arg(site.name).arg(recent->flights)
                                        .arg(QLocale().toString(recent->first, QLocale::ShortFormat),
                                             QLocale().toString(recent->last, QLocale::ShortFormat)));
    });
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
    loadLocationBalloon(std::nullopt);
    m_locRemove->setEnabled(false);
    m_locName->setFocus();
}

bool SettingsDialog::locationFormChanged() const
{
    const int row = m_locationList->currentRow();
    if (row < 0)
        return !m_locName->text().trimmed().isEmpty();
    const sp::Location &l = m_settings.locations[row];
    // The form shows rounded values; compare at that precision.
    const auto balloon = locationBalloonFromForm();
    bool balloonChanged = balloon.has_value() != l.balloon.has_value();
    if (balloon && l.balloon) {
        const sp::SiteBalloon &a = *balloon, &b = *l.balloon;
        balloonChanged = std::abs(a.ascentRate - b.ascentRate) > 0.005 || a.shapedAscent != b.shapedAscent
                      || std::abs(a.burstAltitude - b.burstAltitude) > 0.5 || std::abs(a.descentRate - b.descentRate) > 0.05
                      || std::abs(a.descentExponent - b.descentExponent) > 0.005 || std::abs(a.ascentSd - b.ascentSd) > 0.005
                      || std::abs(a.burstSd - b.burstSd) > 0.5 || std::abs(a.descentSd - b.descentSd) > 0.005;
    }
    return m_locName->text().trimmed() != l.name || m_locLat->value() != l.position.lat || m_locLon->value() != l.position.lon
        || m_locAlt->value() != qRound(l.altitude) || m_locWarning->value() != qRound(l.warningDistanceKm) || balloonChanged;
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

    const sp::Location location{name, {m_locLat->value(), m_locLon->value()}, double(m_locAlt->value()), double(m_locWarning->value()),
                                locationBalloonFromForm()};
    if (row >= 0) {
        // Keep references to a renamed location working.
        const QString oldName = m_settings.locations[row].name;
        for (QString *ref : {&m_settings.manualRun.siteName, &m_settings.manualRun.homeName}) {
            if (*ref == oldName)
                *ref = name;
        }
        if (m_settings.schedule) {
            if (m_settings.schedule->homeName == oldName)
                m_settings.schedule->homeName = name;
            for (sp::BatchSite &site : m_settings.schedule->sites) {
                if (site.siteName == oldName)
                    site.siteName = name;
            }
        }
        // The batch form shows the same names: rename them there too.
        QList<QComboBox *> combos{m_batchHome};
        for (int r = 0; m_batchSites && r < m_batchSites->rowCount(); ++r)
            combos << batchSiteCombo(r);
        for (QComboBox *combo : combos) {
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
    m_shapedAscent = new QCheckBox(tr("Slower around the tropopause, faster high up, as real balloons climb"));
    m_shapedAscent->setObjectName(QStringLiteral("shapedAscent"));
    m_shapedAscent->setToolTip(tr("Measured on Jokioinen flights: about 13 % slower than average at 9–18 km and up to 20 % "
                                  "faster above 25 km. The average, and so the burst time, stays as set."));

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
                                     "k = 0.33 and 11.7 m/s fit 30 RS41 descents from Jokioinen (no parachute); k = 0.5 is the textbook parachute.")));
    descentLayout->addWidget(densityBox);
    descentLayout->addWidget(m_legacyModel);
    descentLayout->addWidget(note(tr("Speed = rate + altitude in km × gain, as in Sonde Predict v3.")));
    descentLayout->addWidget(legacyBox);

    m_profilePreview = new QLabel;
    m_profilePreview->setWordWrap(true);
    auto *defaults = new QPushButton(tr("Balloon defaults"));
    defaults->setToolTip(tr("The default balloon and landing-area values (Jokioinen's flights)."));
    connect(defaults, &QPushButton::clicked, this, [this]() {
        loadProfile(sp::FlightProfile{});
        loadSpread(sp::FlightSpread{});
    });

    auto *top = new QFormLayout;
    top->addRow(tr("Ascent rate"), m_ascent);
    top->addRow(QString(), m_shapedAscent);
    top->addRow(tr("Burst altitude"), m_burst);

    // Typical values of a launch site, from SondeHub's statistics of the flights it tracked;
    // the sites nearest to your locations come first.
    m_sitePreset = new QComboBox;
    m_sitePreset->setObjectName(QStringLiteral("sitePreset"));
    m_sitePreset->setEditable(true);
    m_sitePreset->setInsertPolicy(QComboBox::NoInsert);
    m_sitePreset->completer()->setFilterMode(Qt::MatchContains);
    m_sitePreset->completer()->setCompletionMode(QCompleter::PopupCompletion);
    for (const sp::LaunchSiteStats &s : m_siteStats) {
        // The name only: Use takes the recent flights; the long-term averages are in the tooltip.
        m_sitePreset->addItem(s.name, s.id);
        m_sitePreset->setItemData(m_sitePreset->count() - 1,
                                  tr("Sonde Predict's copy of SondeHub's long-term averages: ascent %1 m/s, burst %2 km, descent %3 m/s%4")
                                      .arg(s.ascentRate, 0, 'f', 1).arg(s.burstAltitude / 1000.0, 0, 'f', 1).arg(s.descentRate, 0, 'f', 1)
                                      .arg(s.flights > 0 ? tr(" (%1 flights)").arg(s.flights) : QString()),
                                  Qt::ToolTipRole);
    }
    auto *useSite = new QPushButton(tr("&Use"));
    useSite->setObjectName(QStringLiteral("useSitePreset"));
    useSite->setEnabled(!m_siteStats.empty());
    connect(useSite, &QPushButton::clicked, this, &SettingsDialog::useSitePreset);
    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(m_sitePreset, 1);
    presetRow->addWidget(useSite);
    m_presetStatus = new QLabel;
    m_presetStatus->setObjectName(QStringLiteral("presetStatus"));
    m_presetStatus->setWordWrap(true);
    auto *presets = new QGroupBox(tr("Typical values for a launch site"));
    auto *presetLayout = new QVBoxLayout(presets);
    presetLayout->addLayout(presetRow);
    presetLayout->addWidget(m_presetStatus);
    presetLayout->addWidget(note(tr("Launch sites tracked by <a href=\"https://sondehub.org/\">SondeHub</a>'s receivers, "
                                    "nearest to your locations first; type to search. <b>Use</b> fills in the ascent and "
                                    "burst of the site's flights of the last 30 days (SondeHub's long-term averages when "
                                    "offline), its descent, and their variation for the landing area.")));

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_profilePreview, 1);
    bottom->addWidget(defaults, 0, Qt::AlignBottom);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(presets);
    layout->addLayout(top);
    layout->addWidget(descent);

    // Landing area: how much real flights vary from the settings above.
    m_areaEnabled = new QCheckBox(tr("Estimate the landing area"));
    m_areaEnabled->setObjectName(QStringLiteral("areaEnabled"));
    m_areaEnabled->setChecked(m_settings.spread.enabled);
    m_areaAscent = doubleSpin(0.0, 3.0, 2, 0.05, tr(" m/s"));
    m_areaAscent->setValue(m_settings.spread.ascentRate);
    m_areaBurst = intSpin(0, 8000, 100, tr(" m"));
    m_areaBurst->setValue(qRound(m_settings.spread.burstAltitude));
    m_areaDescent = intSpin(0, 50, 1, tr(" %"));
    m_areaDescent->setValue(qRound(m_settings.spread.descentRate * 100.0));
    auto *spreadRow = new QHBoxLayout;
    for (const auto &[label, spin] : {std::pair<QString, QWidget *>{tr("ascent ±"), m_areaAscent},
                                      std::pair<QString, QWidget *>{tr("burst ±"), m_areaBurst},
                                      std::pair<QString, QWidget *>{tr("descent ±"), m_areaDescent}}) {
        spreadRow->addWidget(new QLabel(label));
        spreadRow->addWidget(spin);
    }
    spreadRow->addStretch();
    auto *spreadBox = new QWidget;
    spreadBox->setLayout(spreadRow);
    spreadBox->setEnabled(m_areaEnabled->isChecked());
    connect(m_areaEnabled, &QCheckBox::toggled, spreadBox, &QWidget::setEnabled);
    m_areaWind = new QCheckBox(tr("Add forecast uncertainty (more the further ahead the launch is)"));
    m_areaWind->setObjectName(QStringLiteral("areaWind"));
    m_areaWind->setChecked(m_settings.spread.windUncertainty);
    m_areaWind->setEnabled(m_areaEnabled->isChecked());
    connect(m_areaEnabled, &QCheckBox::toggled, m_areaWind, &QWidget::setEnabled);
    auto *area = new QGroupBox(tr("Landing area"));
    auto *areaLayout = new QVBoxLayout(area);
    areaLayout->addWidget(m_areaEnabled);
    areaLayout->addWidget(spreadBox);
    areaLayout->addWidget(m_areaWind);
    areaLayout->addWidget(note(tr("Re-flies each prediction with this much variation (one standard deviation), and with "
                                  "small wind errors as large as the forecasts' real errors, and "
                                  "shows where 95% of those flights land. Uses no extra wind data quota.")));
    layout->addWidget(area);
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

void SettingsDialog::useSitePreset()
{
    const int index = m_sitePreset->currentIndex();
    if (index < 0 || std::size_t(index) >= m_siteStats.size())
        return;
    const sp::LaunchSiteStats site = m_siteStats[std::size_t(index)];
    // The long-term averages at once; the last 30 days when SondeHub answers (balloons and
    // filling change over the years: Jokioinen bursts moved from 29.7 to about 34 km).
    loadProfile(site.applyTo(profileFromForm()));
    sp::FlightSpread spread;
    spread.ascentRate = m_areaAscent->value();
    spread.burstAltitude = m_areaBurst->value();
    spread.descentRate = m_areaDescent->value() / 100.0;
    spread = site.applyTo(spread);
    m_areaAscent->setValue(spread.ascentRate);
    m_areaBurst->setValue(qRound(spread.burstAltitude));
    m_areaDescent->setValue(qRound(spread.descentRate * 100.0));
    m_presetStatus->setText(tr("Looking up the last 30 days of flights from %1 on SondeHub…").arg(site.name));

    if (!m_sondeHub)
        m_sondeHub = new sp::SondeHubClient(&m_network, this);
    const int request = ++m_presetRequest;
    m_sondeHub->recentFlights(site.id, site.position, 30, [this, request, site](const std::optional<sp::RecentFlightStats> &recent,
                                                                                const QString &error) {
        if (request != m_presetRequest)
            return; // another site was chosen meanwhile
        if (!recent) {
            m_presetStatus->setText(tr("%1; using SondeHub's long-term averages for %2.").arg(error, site.name));
            return;
        }
        m_ascent->setValue(recent->ascentRate);
        m_burst->setValue(int(std::lround(recent->burstAltitude / 100.0)) * 100);
        m_areaAscent->setValue(recent->ascentSd);
        m_areaBurst->setValue(int(std::lround(recent->burstSd / 50.0)) * 50);
        m_presetStatus->setText(tr("%1, %2 flights from %3 to %4: ascent %5 ± %6 m/s, burst %7 ± %8 km.")
                                    .arg(site.name).arg(recent->flights)
                                    .arg(QLocale().toString(recent->first, QLocale::ShortFormat), QLocale().toString(recent->last, QLocale::ShortFormat))
                                    .arg(recent->ascentRate, 0, 'f', 1).arg(recent->ascentSd, 0, 'f', 1)
                                    .arg(recent->burstAltitude / 1000.0, 0, 'f', 1).arg(recent->burstSd / 1000.0, 0, 'f', 1));
    });
}

void SettingsDialog::loadSpread(const sp::FlightSpread &s)
{
    m_areaEnabled->setChecked(s.enabled);
    m_areaWind->setChecked(s.windUncertainty);
    m_areaAscent->setValue(s.ascentRate);
    m_areaBurst->setValue(qRound(s.burstAltitude));
    m_areaDescent->setValue(qRound(s.descentRate * 100.0));
}

void SettingsDialog::restoreDefaults()
{
    const sp::AppSettings defaults;
    loadProfile(defaults.profile);
    loadSpread(defaults.spread);
    m_useMeps->setChecked(defaults.wind.useMeps);
    m_averageMeps->setChecked(defaults.wind.averageMeps);
    m_model->setCurrentIndex(std::max(0, m_model->findData(defaults.wind.model)));
    m_grid->setCurrentIndex(std::max(0, m_grid->findData(defaults.wind.gridStep)));
    m_apiUrl->clear();
    m_overwrite->setChecked(defaults.general.overwritePredictions);
    m_useUtc->setChecked(defaults.general.useUtc);
    m_checkForUpdates->setChecked(defaults.general.checkForUpdates);
    if (m_presetStatus)
        m_presetStatus->setText(tr("Defaults restored; press OK to keep them."));
}

void SettingsDialog::loadProfile(const sp::FlightProfile &p)
{
    m_ascent->setValue(p.ascentRate);
    m_shapedAscent->setChecked(p.shapedAscent);
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
    p.shapedAscent = m_shapedAscent->isChecked();
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

// ---------------------------------------------------------------- Wind data

QWidget *SettingsDialog::buildWindTab()
{
    m_apiUrl = new QLineEdit(m_settings.wind.apiUrl);
    m_apiUrl->setPlaceholderText(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    m_apiKey = new QLineEdit(m_settings.wind.apiKey);
    m_apiKey->setPlaceholderText(tr("only for a paid subscription"));
    m_apiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_model = new QComboBox;
    m_model->setObjectName(QStringLiteral("model"));
    for (const sp::openmeteo::ModelInfo &m : sp::openmeteo::models())
        m_model->addItem(m.name, m.id);
    if (m_model->findData(m_settings.wind.model) < 0) // e.g. typed into an older version
        m_model->addItem(m_settings.wind.model, m_settings.wind.model);
    m_model->setCurrentIndex(m_model->findData(m_settings.wind.model));
    m_grid = new QComboBox;
    for (double step : {0.25, 0.5, 1.0})
        m_grid->addItem(QStringLiteral("%1°").arg(step), step);
    m_grid->setCurrentIndex(std::max(0, m_grid->findData(m_settings.wind.gridStep)));

    m_useMeps = new QCheckBox(tr("Use MET Norway's MEPS model inside its area"));
    m_useMeps->setObjectName(QStringLiteral("useMeps"));
    m_useMeps->setChecked(m_settings.wind.useMeps);
    m_averageMeps = new QCheckBox(tr("Average it with the global model (recommended)"));
    m_averageMeps->setObjectName(QStringLiteral("averageMeps"));
    m_averageMeps->setChecked(m_settings.wind.averageMeps);
    m_averageMeps->setEnabled(m_useMeps->isChecked());
    connect(m_useMeps, &QCheckBox::toggled, m_averageMeps, &QWidget::setEnabled);
    auto *meps = new QGroupBox(tr("Nordic area (MEPS)"));
    auto *mepsLayout = new QVBoxLayout(meps);
    mepsLayout->addWidget(m_useMeps);
    auto *averageRow = new QHBoxLayout;
    averageRow->addSpacing(20);
    averageRow->addWidget(m_averageMeps, 1);
    mepsLayout->addLayout(averageRow);
    mepsLayout->addWidget(note(tr("MEPS covers the Nordic countries and the Baltic on a 2.5 km grid with 65 model levels, "
                                  "up to 66 hours ahead, with a new run every 3 hours. It is free and has no quota. "
                                  "Averaged with the global model it matched real soundings best; that uses the "
                                  "Open-Meteo quota like the global model alone (MEPS alone is used when the quota is "
                                  "used up). Elsewhere, further ahead, or if MET Norway's server cannot be reached, "
                                  "the global model below is used.")));

    auto *wind = new QGroupBox(tr("Global model (Open-Meteo)"));
    auto *windForm = new QFormLayout(wind);
    windForm->addRow(tr("Server"), m_apiUrl);
    windForm->addRow(tr("API key"), m_apiKey);
    windForm->addRow(tr("Model"), m_model);
    windForm->addRow(tr("Grid spacing"), m_grid);
    windForm->addRow(note(tr("The free <a href=\"https://open-meteo.com/\">Open-Meteo</a> API is for non-commercial use, "
                             "about 40–60 new predictions a day. A coarser grid uses less of that quota.")));


    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(meps);
    layout->addWidget(wind);
    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------- Batch

QWidget *SettingsDialog::buildBatchTab()
{
    m_batchSites = new QTableWidget(0, 2);
    m_batchSites->setObjectName(QStringLiteral("batchSites"));
    m_batchSites->setHorizontalHeaderLabels({tr("Launch location"), tr("Launch times (UTC)")});
    m_batchSites->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_batchSites->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_batchSites->verticalHeader()->hide();
    m_batchSites->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_batchSites->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *addSite = new QPushButton(tr("&Add site"));
    addSite->setObjectName(QStringLiteral("batchAddSite"));
    auto *removeSite = new QPushButton(tr("Re&move site"));
    removeSite->setObjectName(QStringLiteral("batchRemoveSite"));
    connect(addSite, &QPushButton::clicked, this, [this]() {
        // The first location not in the batch yet, with its station's usual launch times.
        QString name;
        for (const sp::Location &l : m_settings.locations) {
            bool used = false;
            for (int r = 0; r < m_batchSites->rowCount(); ++r)
                used = used || batchSiteCombo(r)->currentText() == l.name;
            if (!used) {
                name = l.name;
                break;
            }
        }
        if (name.isEmpty() && !m_settings.locations.isEmpty())
            name = m_settings.locations.front().name;
        addBatchSiteRow(name, typicalLaunchTimes(name));
        updateBatchSummary();
    });
    connect(removeSite, &QPushButton::clicked, this, [this]() {
        const int row = m_batchSites->currentRow();
        m_batchSites->removeRow(row >= 0 ? row : m_batchSites->rowCount() - 1);
        updateBatchSummary();
    });

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
    m_batchInterval = intSpin(0, 168, 1, tr(" h"));
    m_batchInterval->setObjectName(QStringLiteral("batchInterval"));
    m_batchInterval->setSpecialValueText(tr("never"));
    m_batchSummary = new QLabel;
    m_batchSummary->setObjectName(QStringLiteral("batchSummary"));
    m_batchSummary->setWordWrap(true);

    if (const auto &rule = m_settings.schedule) {
        for (const sp::BatchSite &site : rule->sites)
            addBatchSiteRow(site.siteName, site.times);
        for (int i = 0; i < m_batchDays->count(); ++i)
            m_batchDays->item(i)->setCheckState(rule->days.contains(i) ? Qt::Checked : Qt::Unchecked);
        m_batchInterval->setValue(rule->intervalHours);
    }
    refreshBatchLocations(m_settings.schedule ? std::optional(m_settings.schedule->homeName) : std::nullopt);

    connect(m_batchHome, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateBatchSummary);
    connect(m_batchDays, &QListWidget::itemChanged, this, &SettingsDialog::updateBatchSummary);
    connect(m_batchInterval, &QSpinBox::valueChanged, this, &SettingsDialog::updateBatchSummary);

    auto *siteButtons = new QHBoxLayout;
    siteButtons->addWidget(addSite);
    siteButtons->addWidget(removeSite);
    siteButtons->addStretch();
    auto *sites = new QGroupBox(tr("Launch sites"));
    auto *sitesLayout = new QVBoxLayout(sites);
    sitesLayout->addWidget(m_batchSites, 1);
    sitesLayout->addLayout(siteButtons);
    sitesLayout->addWidget(note(tr("Launch times in UTC, separated by commas, e.g. 05:30, 11:30, 23:30. A new site gets "
                                   "its station's usual times (30 min before each observation time) when SondeHub knows it. "
                                   "Each site uses its own balloon values if it has them (Locations tab).")));

    auto *days = new QVBoxLayout;
    days->addWidget(new QLabel(tr("Days")));
    days->addWidget(m_batchDays);
    auto *form = new QFormLayout;
    form->addRow(tr("Your location"), m_batchHome);
    form->addRow(tr("Repeat every"), m_batchInterval);
    auto *right = new QVBoxLayout;
    right->addLayout(form);
    right->addWidget(m_batchSummary);
    right->addStretch();
    right->addWidget(note(tr("Start a batch from the main window, or tick \"Run scheduled batch tasks\" there to repeat it "
                             "while Sonde Predict is open. Launches close in time share downloaded wind data.")));
    auto *bottom = new QHBoxLayout;
    bottom->addLayout(days, 1);
    bottom->addLayout(right, 2);

    auto *page = new QWidget;
    m_batchPage = page;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(sites, 3);
    layout->addLayout(bottom, 4);
    updateBatchSummary();
    return page;
}

QComboBox *SettingsDialog::batchSiteCombo(int row) const
{
    return qobject_cast<QComboBox *>(m_batchSites->cellWidget(row, 0));
}

QLineEdit *SettingsDialog::batchTimesEdit(int row) const
{
    return qobject_cast<QLineEdit *>(m_batchSites->cellWidget(row, 1));
}

QList<QTime> SettingsDialog::typicalLaunchTimes(const QString &siteName) const
{
    if (const sp::Location *l = m_settings.location(siteName)) {
        if (const sp::LaunchSiteStats *s = sp::nearestSite(m_siteStats, l->position); s && !s->synopticTimes.isEmpty())
            return s->launchTimes();
    }
    return {QTime(11, 30), QTime(23, 30)};
}

void SettingsDialog::addBatchSiteRow(const QString &siteName, const QList<QTime> &times)
{
    const int row = m_batchSites->rowCount();
    m_batchSites->insertRow(row);
    auto *site = new QComboBox;
    site->setObjectName(QStringLiteral("batchSite%1").arg(row));
    auto *edit = new QLineEdit(sp::formatLaunchTimes(times));
    edit->setObjectName(QStringLiteral("batchTimes%1").arg(row));
    edit->setProperty("typical", sp::formatLaunchTimes(typicalLaunchTimes(siteName)));
    edit->setPlaceholderText(QStringLiteral("05:30, 11:30, 23:30"));
    m_batchSites->setCellWidget(row, 0, site);
    m_batchSites->setCellWidget(row, 1, edit);
    fillSiteCombo(site, siteName);
    connect(site, &QComboBox::currentIndexChanged, this, [this, site, edit]() {
        // Another site: offer its usual launch times, unless the times were typed in.
        const QString typical = sp::formatLaunchTimes(typicalLaunchTimes(site->currentText()));
        if (edit->text().trimmed().isEmpty() || edit->text() == edit->property("typical").toString())
            edit->setText(typical);
        edit->setProperty("typical", typical);
        updateBatchSummary();
    });
    connect(edit, &QLineEdit::textChanged, this, &SettingsDialog::updateBatchSummary);
}

void SettingsDialog::fillSiteCombo(QComboBox *combo, const QString &name)
{
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const sp::Location &l : m_settings.locations)
        combo->addItem(l.name);
    // A name that is no longer a location stays selected, marked, so that pressing OK does not
    // quietly move the batch to another place.
    if (name.isEmpty()) {
        combo->setCurrentIndex(combo->count() ? 0 : -1);
        return;
    }
    int i = combo->findText(name);
    if (i < 0) {
        combo->addItem(name);
        i = combo->count() - 1;
        combo->setItemData(i, QColor(Qt::red), Qt::ForegroundRole);
        combo->setItemData(i, tr("Not one of your locations any more"), Qt::ToolTipRole);
    }
    combo->setCurrentIndex(i);
}

void SettingsDialog::refreshBatchLocations(std::optional<QString> wantedHome)
{
    if (!m_batchHome)
        return;
    for (int r = 0; r < m_batchSites->rowCount(); ++r)
        fillSiteCombo(batchSiteCombo(r), batchSiteCombo(r)->currentText());
    const QString home = wantedHome.value_or(m_batchHome->currentIndex() > 0 ? m_batchHome->currentText() : QString());
    {
        const QSignalBlocker blocker(m_batchHome);
        fillSiteCombo(m_batchHome, home);
        m_batchHome->insertItem(0, tr("(none)"));
        if (home.isEmpty())
            m_batchHome->setCurrentIndex(0);
    }
    updateBatchSummary();
}

std::optional<sp::ScheduleRule> SettingsDialog::scheduleFromForm(QStringList *invalid) const
{
    sp::ScheduleRule rule;
    for (int r = 0; r < m_batchSites->rowCount(); ++r) {
        sp::BatchSite site;
        site.siteName = batchSiteCombo(r)->currentText();
        const auto times = sp::parseLaunchTimes(batchTimesEdit(r)->text());
        if (!times && invalid)
            *invalid << site.siteName;
        site.times = times.value_or(QList<QTime>{});
        if (!site.siteName.isEmpty())
            rule.sites << site;
    }
    if (rule.sites.isEmpty())
        return std::nullopt;
    rule.homeName = m_batchHome->currentIndex() > 0 ? m_batchHome->currentText() : QString();
    for (int i = 0; i < m_batchDays->count(); ++i) {
        if (m_batchDays->item(i)->checkState() == Qt::Checked)
            rule.days << m_batchDays->item(i)->data(Qt::UserRole).toInt();
    }
    rule.intervalHours = m_batchInterval->value();
    rule.autoRun = m_settings.schedule && m_settings.schedule->autoRun && rule.intervalHours > 0;
    return rule;
}

void SettingsDialog::updateBatchSummary()
{
    if (!m_batchSummary || !m_batchInterval)
        return;
    for (int r = 0; r < m_batchSites->rowCount(); ++r) {
        QLineEdit *edit = batchTimesEdit(r);
        edit->setStyleSheet(sp::parseLaunchTimes(edit->text()) ? QString() : QStringLiteral("color: red"));
    }
    QStringList invalid;
    const auto rule = scheduleFromForm(&invalid);
    if (m_settings.locations.isEmpty()) {
        m_batchSummary->setText(tr("Add a location first (Locations tab)."));
        return;
    }
    if (!rule) {
        m_batchSummary->setText(tr("Add a launch site to predict."));
        return;
    }
    if (!invalid.isEmpty()) {
        m_batchSummary->setText(tr("Check the launch times of %1: write them like 05:30, 11:30.").arg(invalid.join(QStringLiteral(", "))));
        return;
    }
    for (const sp::BatchSite &site : rule->sites) {
        if (!m_settings.location(site.siteName)) {
            m_batchSummary->setText(tr("“%1” is no longer one of your locations; choose another one.").arg(site.siteName));
            return;
        }
    }
    int perDay = 0;
    QStringList parts;
    for (const sp::BatchSite &site : rule->sites) {
        perDay += int(site.times.size());
        if (!site.times.isEmpty())
            parts << tr("%1 at %2").arg(site.siteName, sp::formatLaunchTimes(site.times));
    }
    if (rule->days.isEmpty() || perDay == 0) {
        m_batchSummary->setText(tr("Tick at least one day, and give each site its launch times."));
        return;
    }
    QString text = tr("%1 per batch: %2 UTC, on %3.")
                       .arg(counted(perDay * int(rule->days.size()), tr("%1 prediction"), tr("%1 predictions")),
                            parts.join(QStringLiteral("; ")), counted(int(rule->days.size()), tr("%1 day"), tr("%1 days")));
    if (rule->intervalHours > 0)
        text += u' ' + tr("Repeats every %1 when scheduled runs are on.").arg(counted(rule->intervalHours, tr("%1 hour"), tr("%1 hours")));
    m_batchSummary->setText(text);
}

// ---------------------------------------------------------------- General

QWidget *SettingsDialog::buildGeneralTab()
{
    m_overwrite = new QCheckBox(tr("Replace an older prediction for the same launch, site and location"));
    m_overwrite->setChecked(m_settings.general.overwritePredictions);
    m_useUtc = new QCheckBox(tr("Show and enter times in UTC"));
    m_useUtc->setChecked(m_settings.general.useUtc);
    m_checkForUpdates = new QCheckBox(tr("Check for a new version"));
    m_checkForUpdates->setObjectName(QStringLiteral("checkForUpdates"));
    m_checkForUpdates->setChecked(m_settings.general.checkForUpdates);

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
    networkLayout->addWidget(m_checkForUpdates);

    auto *predictions = new QGroupBox(tr("Predictions"));
    auto *predictionsLayout = new QVBoxLayout(predictions);
    predictionsLayout->addWidget(m_overwrite);
    predictionsLayout->addWidget(m_useUtc);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(predictions);
    layout->addWidget(network);
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
    QStringList invalidTimes;
    const auto rule = scheduleFromForm(&invalidTimes);
    if (!invalidTimes.isEmpty()) {
        m_tabs->setCurrentWidget(m_batchPage);
        QMessageBox::information(this, tr("Batch"), tr("Check the launch times of %1: write them like 05:30, 11:30, 23:30.")
                                                        .arg(invalidTimes.join(QStringLiteral(", "))));
        return;
    }
    const auto proxyMode = static_cast<sp::ProxyMode>(m_proxyMode->checkedId());
    if (proxyMode == sp::ProxyMode::Manual && m_proxyUrl->text().trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Network"), tr("Enter the proxy address, or choose another proxy option."));
        return;
    }

    m_settings.profile = profile;
    m_settings.spread.enabled = m_areaEnabled->isChecked();
    m_settings.spread.ascentRate = m_areaAscent->value();
    m_settings.spread.burstAltitude = m_areaBurst->value();
    m_settings.spread.descentRate = m_areaDescent->value() / 100.0;
    m_settings.spread.windUncertainty = m_areaWind->isChecked();
    m_settings.wind.useMeps = m_useMeps->isChecked();
    m_settings.wind.averageMeps = m_averageMeps->isChecked();
    m_settings.wind.apiUrl = m_apiUrl->text().trimmed();
    m_settings.wind.apiKey = m_apiKey->text().trimmed();
    m_settings.wind.model = m_model->currentData().toString();
    m_settings.wind.gridStep = m_grid->currentData().toDouble();
    m_settings.network.proxyMode = proxyMode;
    m_settings.network.proxyUrl = m_proxyUrl->text().trimmed();
    m_settings.general.overwritePredictions = m_overwrite->isChecked();
    m_settings.general.useUtc = m_useUtc->isChecked();
    m_settings.general.checkForUpdates = m_checkForUpdates->isChecked();
    // A batch without sites is no batch; one without days is kept so its sites are not lost.
    m_settings.schedule = rule;
    QDialog::accept();
}
