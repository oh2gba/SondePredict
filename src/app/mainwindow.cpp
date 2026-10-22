#include "mainwindow.h"

#include "mapwidget.h"
#include "network.h"
#include "settingsdialog.h"
#include "trackexport.h"
#include "v3import.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDateEdit>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr qint64 kCacheMaxAgeSecs = 3600; // GFS updates every 6 h; don't reuse winds for long
constexpr int kMaxDaysAhead = 15;
constexpr int kMaxDaysBack = 7;

QString compass(double bearing)
{
    static const char *const names[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    const int index = static_cast<int>(std::lround(bearing / 22.5)) % 16;
    return QString::fromLatin1(names[index]);
}

QString km(double metres)
{
    return QStringLiteral("%1 km").arg(metres / 1000.0, 0, 'f', 1);
}

QString duration(double seconds)
{
    const int minutes = static_cast<int>(std::lround(seconds / 60.0));
    return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60, 2, 10, QChar(u'0'));
}

QString osmLink(sp::GeoPoint p)
{
    const QString lat = QString::number(p.lat, 'f', 5);
    const QString lon = QString::number(p.lon, 'f', 5);
    return QStringLiteral("<a href=\"https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=12/%1/%2\">%1, %2</a>").arg(lat, lon);
}

} // namespace

MainWindow::MainWindow(QString configPath, QString dbPath, QWidget *parent)
    : QMainWindow(parent)
    , m_configPath(std::move(configPath))
    , m_dbPath(std::move(dbPath))
{
    setWindowTitle(QStringLiteral("Sonde Predict"));
    setMinimumSize(900, 600);

    QString error;
    m_settings = sp::AppSettings::load(m_configPath, &error);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Settings"), tr("%1\nDefaults are used instead.").arg(error));

    buildUi();
    buildMenus();
    restoreWindowState();

    if (!m_store.open(m_dbPath)) {
        QMessageBox::critical(this, tr("Prediction database"),
                              tr("Could not open %1:\n%2\n\nPredictions will not be saved.").arg(m_dbPath, m_store.lastError()));
    }

    applySettings();
    refreshLocationCombos();
    refreshTree();

    connect(&m_clock, &QTimer::timeout, this, [this]() {
        m_clockLabel->setText(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'")));
    });
    m_clock.start(1000);

    m_scheduleTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_scheduleTimer, &QTimer::timeout, this, [this]() {
        updateScheduleTimer(); // schedules the next one
        startBatch(true);
    });
    updateBatchControls();
    updateScheduleTimer();
    m_clockLabel->setText(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'")));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    // Left: predictions tree and the "new prediction" controls (v3 layout).
    auto *predictionsBox = new QGroupBox(tr("Predictions"));
    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("predictionTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setUniformRowHeights(true);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);

    m_deleteButton = new QPushButton(tr("&Delete"));
    m_deleteButton->setEnabled(false);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    m_earthButton = new QPushButton(tr("&Google Earth"));
    m_earthButton->setEnabled(false);
    m_earthButton->setToolTip(tr("Show the selected predictions in Google Earth"));
    connect(m_earthButton, &QPushButton::clicked, this, &MainWindow::openInGoogleEarth);

    auto *treeButtons = new QHBoxLayout;
    treeButtons->addWidget(m_deleteButton);
    treeButtons->addStretch();
    treeButtons->addWidget(m_earthButton);
    auto *predictionsLayout = new QVBoxLayout(predictionsBox);
    predictionsLayout->addWidget(m_tree);
    predictionsLayout->addLayout(treeButtons);

    auto *runBox = new QGroupBox(tr("New prediction"));
    m_siteCombo = new QComboBox;
    m_homeCombo = new QComboBox;
    m_dateEdit = new QDateEdit;
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat(QStringLiteral("ddd d MMM yyyy"));
    m_timeEdit = new QTimeEdit;
    m_timeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_timeZoneLabel = new QLabel;
    auto *timeRow = new QHBoxLayout;
    timeRow->addWidget(m_timeEdit);
    timeRow->addWidget(m_timeZoneLabel);
    timeRow->addStretch();

    m_runButton = new QPushButton(tr("&Start"));
    m_runButton->setObjectName(QStringLiteral("runButton"));
    m_runButton->setDefault(true);
    connect(m_runButton, &QPushButton::clicked, this, [this]() { isBusy() ? stopRunning() : runPrediction(); });
    m_batchButton = new QPushButton(tr("Start a &batch"));
    m_batchButton->setObjectName(QStringLiteral("batchButton"));
    connect(m_batchButton, &QPushButton::clicked, this, [this]() { startBatch(false); });
    m_schedulerCheck = new QCheckBox(tr("Run scheduled batch tasks"));
    m_schedulerCheck->setObjectName(QStringLiteral("schedulerCheck"));
    connect(m_schedulerCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_settings.schedule || m_settings.schedule->autoRun == on)
            return;
        m_settings.schedule->autoRun = on;
        saveSettings();
        updateScheduleTimer();
    });

    auto *form = new QFormLayout;
    form->addRow(tr("Launch location"), m_siteCombo);
    form->addRow(tr("Your location"), m_homeCombo);
    form->addRow(tr("Date"), m_dateEdit);
    form->addRow(tr("Time"), timeRow);
    auto *runButtons = new QHBoxLayout;
    runButtons->addWidget(m_batchButton);
    runButtons->addStretch();
    runButtons->addWidget(m_runButton);
    auto *runLayout = new QVBoxLayout(runBox);
    runLayout->addLayout(form);
    runLayout->addWidget(m_schedulerCheck);
    runLayout->addLayout(runButtons);

    for (QComboBox *combo : {m_siteCombo, m_homeCombo}) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this]() {
            if (selectedIds().isEmpty())
                showOverview();
        });
    }

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(predictionsBox, 1);
    leftLayout->addWidget(runBox);

    // Right: map above the details.
    m_map = new MapWidget;
    auto *detailsBox = new QGroupBox(tr("Details"));
    m_details = new QTextBrowser;
    m_details->setOpenExternalLinks(true);
    auto *detailsLayout = new QVBoxLayout(detailsBox);
    detailsLayout->addWidget(m_details);

    m_rightSplitter = new QSplitter(Qt::Vertical);
    m_rightSplitter->addWidget(m_map);
    m_rightSplitter->addWidget(detailsBox);
    m_rightSplitter->setStretchFactor(0, 3);
    m_rightSplitter->setStretchFactor(1, 2);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->addWidget(left);
    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({330, 870});
    m_mainSplitter->setContentsMargins(6, 6, 6, 0);
    setCentralWidget(m_mainSplitter);

    m_statusLabel = new QLabel(tr("Sonde Predict"));
    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setMaximumWidth(160);
    m_progress->setVisible(false);
    m_clockLabel = new QLabel;
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->addPermanentWidget(m_clockLabel);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    m_selectionActions << file->addAction(tr("Export selected as &KML (Google Earth)..."), this, [this]() { exportSelected(false); });
    m_selectionActions << file->addAction(tr("Export selected as &GPX (GPS, phone)..."), this, [this]() { exportSelected(true); });
    file->addSeparator();
    file->addAction(tr("&Import Sonde Predict v3 settings..."), this, &MainWindow::importV3Settings);
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *settings = edit->addAction(tr("&Settings..."), this, &MainWindow::openSettings);
    settings->setShortcut(QKeySequence::Preferences);
    settings->setObjectName(QStringLiteral("settingsAction"));

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Open &log file"), this, [this]() {
        const QString log = QFileInfo(m_dbPath).dir().filePath(QStringLiteral("log.txt"));
        if (!QFileInfo::exists(log) || !QDesktopServices::openUrl(QUrl::fromLocalFile(log)))
            QMessageBox::information(this, tr("Log file"), tr("The log file is %1").arg(QDir::toNativeSeparators(log)));
    });
    help->addSeparator();
    help->addAction(tr("&About Sonde Predict"), this, &MainWindow::showAbout);
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::restoreWindowState()
{
    const QSettings state(QFileInfo(m_configPath).dir().filePath(QStringLiteral("window.ini")), QSettings::IniFormat);
    if (!restoreGeometry(state.value(QStringLiteral("geometry")).toByteArray()))
        resize(1200, 800);
    m_mainSplitter->restoreState(state.value(QStringLiteral("mainSplitter")).toByteArray());
    m_rightSplitter->restoreState(state.value(QStringLiteral("rightSplitter")).toByteArray());
}

void MainWindow::saveWindowState() const
{
    QSettings state(QFileInfo(m_configPath).dir().filePath(QStringLiteral("window.ini")), QSettings::IniFormat);
    state.setValue(QStringLiteral("geometry"), saveGeometry());
    state.setValue(QStringLiteral("mainSplitter"), m_mainSplitter->saveState());
    state.setValue(QStringLiteral("rightSplitter"), m_rightSplitter->saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_predictor)
        m_predictor->cancel();
    if (m_batch)
        m_batch->cancel();
    rememberManualRunChoices();
    saveSettings();
    saveWindowState();
    event->accept();
}

void MainWindow::applySettings()
{
    if (const QString error = sp::applyProxy(m_settings.network); !error.isEmpty())
        m_statusLabel->setText(error);

    sp::OpenMeteoOptions options;
    if (!m_settings.wind.apiUrl.isEmpty())
        options.baseUrl = m_settings.wind.apiUrl;
    options.apiKey = m_settings.wind.apiKey;
    options.model = m_settings.wind.model;
    m_predictor.reset(); // they refer to the old client
    m_batch.reset();
    m_client = std::make_unique<sp::OpenMeteoClient>(&m_network, options);
    connect(m_client.get(), &sp::OpenMeteoClient::rateLimited, this, [this](int seconds) {
        m_statusLabel->setText(tr("Open-Meteo rate limit reached; continuing in %1 s...").arg(seconds));
    });
    m_field.reset();

    const bool utc = m_settings.general.useUtc;
    m_timeZoneLabel->setText(utc ? tr("UTC") : QTimeZone::systemTimeZone().displayName(QDateTime::currentDateTime(), QTimeZone::ShortName));
    const QDate today = utc ? QDateTime::currentDateTimeUtc().date() : QDate::currentDate();
    m_dateEdit->setDateRange(today.addDays(-kMaxDaysBack), today.addDays(kMaxDaysAhead));
    if (m_dateEdit->date() < today)
        m_dateEdit->setDate(today);
    m_timeEdit->setTime(QTime(m_settings.manualRun.hour, m_settings.manualRun.minute));
}

void MainWindow::saveSettings()
{
    QString error;
    if (!m_settings.save(m_configPath, &error))
        QMessageBox::warning(this, tr("Settings"), error);
}

void MainWindow::refreshLocationCombos()
{
    const QSignalBlocker siteBlocker(m_siteCombo);
    const QSignalBlocker homeBlocker(m_homeCombo);
    m_siteCombo->clear();
    m_homeCombo->clear();
    m_homeCombo->addItem(tr("(none)"));
    for (const sp::Location &l : m_settings.locations) {
        m_siteCombo->addItem(l.name);
        m_homeCombo->addItem(l.name);
    }
    if (const int i = m_siteCombo->findText(m_settings.manualRun.siteName); i >= 0)
        m_siteCombo->setCurrentIndex(i);
    if (const int i = m_homeCombo->findText(m_settings.manualRun.homeName); i >= 0)
        m_homeCombo->setCurrentIndex(i);
    m_runButton->setEnabled(m_siteCombo->count() > 0 || isBusy());
    updateBatchControls();
}

void MainWindow::rememberManualRunChoices()
{
    m_settings.manualRun.siteName = m_siteCombo->currentText();
    m_settings.manualRun.homeName = m_homeCombo->currentIndex() > 0 ? m_homeCombo->currentText() : QString();
    m_settings.manualRun.hour = m_timeEdit->time().hour();
    m_settings.manualRun.minute = m_timeEdit->time().minute();
}

QString MainWindow::formatTime(double unixTime, bool withSeconds) const
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::lround(unixTime)), QTimeZone::UTC);
    const QString format = withSeconds ? QStringLiteral("yyyy-MM-dd HH:mm:ss") : QStringLiteral("yyyy-MM-dd HH:mm");
    if (m_settings.general.useUtc)
        return t.toString(format) + QStringLiteral(" UTC");
    const QDateTime local = t.toLocalTime();
    return local.toString(format) + u' ' + local.timeZoneAbbreviation();
}

QString MainWindow::formatDay(qint64 unixTime) const
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(unixTime, QTimeZone::UTC);
    return (m_settings.general.useUtc ? t : t.toLocalTime()).toString(QStringLiteral("ddd d MMM yyyy"));
}

std::optional<double> MainWindow::warningDistanceKm(const QString &siteName, const QString &homeName) const
{
    if (const sp::Location *home = m_settings.location(homeName))
        return home->warningDistanceKm;
    if (const sp::Location *site = m_settings.location(siteName))
        return site->warningDistanceKm; // where v3 took it from
    return std::nullopt;
}

void MainWindow::refreshTree(qint64 selectId)
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    QTreeWidgetItem *dayItem = nullptr;
    QTreeWidgetItem *toSelect = nullptr;
    QString currentDay;
    for (const sp::PredictionSummary &p : m_store.isOpen() ? m_store.list() : std::vector<sp::PredictionSummary>{}) {
        const QString day = formatDay(p.launchTime);
        if (!dayItem || day != currentDay) {
            dayItem = new QTreeWidgetItem(m_tree, {day});
            dayItem->setData(0, kIdRole, qint64(0));
            dayItem->setExpanded(true);
            currentDay = day;
        }
        QString label = QStringLiteral("%1 %2").arg(p.siteName, formatTime(double(p.launchTime)).section(u' ', 1, 1));
        if (p.distanceFromHome)
            label += QStringLiteral("  →  %1").arg(km(*p.distanceFromHome));
        auto *item = new QTreeWidgetItem(dayItem, {label});
        item->setData(0, kIdRole, p.id);
        item->setToolTip(0, tr("Lands at %1, %2 %3")
                                .arg(p.landing.lat, 0, 'f', 4).arg(p.landing.lon, 0, 'f', 4)
                                .arg(p.manual ? QString() : tr("(batch)")));

        // Bold when the landing is close to "your location" (v3 behaviour).
        const auto warning = warningDistanceKm(p.siteName, p.homeName);
        if (p.distanceFromHome && warning && *p.distanceFromHome <= *warning * 1000.0) {
            QFont bold = item->font(0);
            bold.setBold(true);
            item->setFont(0, bold);
            dayItem->setFont(0, bold);
        }
        if (p.id == selectId || (!selectId && !toSelect))
            toSelect = item;
    }

    if (toSelect) {
        m_tree->setCurrentItem(toSelect);
        toSelect->setSelected(true);
    }
    onSelectionChanged();
}

QList<qint64> MainWindow::selectedIds() const
{
    QList<qint64> ids;
    const auto add = [&ids](QTreeWidgetItem *item) {
        const qint64 id = item->data(0, kIdRole).toLongLong();
        if (id && !ids.contains(id))
            ids << id;
    };
    // The current item first: it is the one shown in the details.
    if (QTreeWidgetItem *current = m_tree->currentItem(); current && current->isSelected())
        add(current->childCount() ? current->child(0) : current);
    for (QTreeWidgetItem *item : m_tree->selectedItems()) {
        add(item);
        for (int i = 0; i < item->childCount(); ++i)
            add(item->child(i));
    }
    return ids;
}

void MainWindow::onSelectionChanged()
{
    const QList<qint64> ids = selectedIds();
    m_deleteButton->setEnabled(!ids.isEmpty());
    m_earthButton->setEnabled(!ids.isEmpty());
    for (QAction *action : m_selectionActions)
        action->setEnabled(!ids.isEmpty());
    if (ids.isEmpty()) {
        showOverview();
        return;
    }

    std::vector<sp::PredictionRecord> records;
    for (qint64 id : ids) {
        if (auto record = m_store.load(id))
            records.push_back(std::move(*record));
    }
    if (records.empty()) {
        showOverview();
        return;
    }
    m_map->model()->setPredictions(records);
    showDetails(records.front());
    if (records.size() > 1)
        m_statusLabel->setText(tr("%1 predictions shown on the map").arg(records.size()));
}

void MainWindow::showOverview()
{
    m_map->model()->setLocations(m_settings.location(m_siteCombo->currentText()),
                                 m_homeCombo->currentIndex() > 0 ? m_settings.location(m_homeCombo->currentText()) : nullptr);
    if (m_settings.locations.isEmpty()) {
        m_details->setHtml(tr("<h3>Welcome to Sonde Predict</h3>"
                              "<p>Add your launch site and your own location under <b>Edit → Settings → Locations</b>, "
                              "or bring them over from Sonde Predict v3 with <b>File → Import Sonde Predict v3 settings</b>.</p>"
                              "<p>Then pick a date and time and press <b>Start</b>.</p>"));
    } else {
        m_details->setHtml(tr("<p>Select a prediction on the left, or start a new one.</p>"));
    }
}

void MainWindow::showDetails(const sp::PredictionRecord &rec)
{
    const sp::LaunchParams &p = rec.prediction.params;
    const sp::SimulationResult &r = rec.prediction.result;
    const sp::TrackPoint &start = r.track.front();
    const sp::TrackPoint &land = *r.landing;

    QString html = QStringLiteral("<table cellspacing=\"2\" cellpadding=\"1\">");
    const auto row = [&html](const QString &name, const QString &value) {
        html += QStringLiteral("<tr><td style=\"padding-right:16px; color:palette(mid)\">%1</td><td>%2</td></tr>").arg(name, value);
    };
    const auto section = [&html](const QString &title) {
        html += QStringLiteral("<tr><td colspan=\"2\" style=\"padding-top:8px\"><b>%1</b></td></tr>").arg(title);
    };

    section(tr("Landing"));
    row(tr("Position"), osmLink(land.position));
    row(tr("Time"), tr("%1  (flight %2)").arg(formatTime(land.time), duration(land.time - start.time)));
    row(tr("Ground altitude"), tr("%1 m").arg(qRound(land.altitude)));
    if (rec.home) {
        const sp::Geodesic g = sp::inverse(*rec.home, land.position);
        row(tr("From %1").arg(rec.homeName.toHtmlEscaped()),
            tr("<b>%1</b>, bearing %2° (%3)").arg(km(g.distance)).arg(qRound(g.initialBearing)).arg(compass(g.initialBearing)));
    }
    const sp::Geodesic fromSite = sp::inverse(start.position, land.position);
    row(tr("From launch site"), tr("%1, bearing %2° (%3)").arg(km(fromSite.distance)).arg(qRound(fromSite.initialBearing)).arg(compass(fromSite.initialBearing)));

    section(tr("Launch"));
    row(tr("Site"), tr("%1 (%2 m)").arg(rec.siteName.toHtmlEscaped()).arg(qRound(start.altitude)));
    row(tr("Time"), formatTime(start.time));
    if (r.burst)
        row(tr("Burst"), tr("%1 m at %2, %3").arg(qRound(r.burst->altitude)).arg(formatTime(r.burst->time), osmLink(r.burst->position)));

    section(tr("Settings used"));
    row(tr("Ascent rate"), tr("%1 m/s").arg(p.profile.ascentRate));
    row(tr("Burst altitude"), tr("%1 m").arg(qRound(p.profile.burstAltitude)));
    if (p.profile.descentModel == sp::DescentModel::Legacy) {
        row(tr("Descent"), tr("v3 linear: %1 m/s + %2 per km").arg(p.profile.legacyDescentRate).arg(p.profile.legacyDescentGain));
    } else {
        row(tr("Descent"), tr("%1 m/s at sea level, density exponent %2").arg(p.profile.seaLevelDescentRate).arg(p.profile.descentExponent));
    }
    row(tr("Wind data"), tr("GFS (%1) via Open-Meteo, %2 grid points").arg(rec.model.toHtmlEscaped()).arg(rec.prediction.nodesFetched));
    row(tr("Calculated"), formatTime(double(rec.createdAt)) + (rec.manual ? QString() : tr(" (batch)")));
    html += QStringLiteral("</table>");

    if (r.aboveTopLevel)
        html += tr("<p><i>The flight went above the highest forecast level (10 hPa, about 31 km); the wind there was held constant.</i></p>");
    html += tr("<p style=\"color:palette(mid)\"><small>Weather data by <a href=\"https://open-meteo.com/\">Open-Meteo.com</a> (CC BY 4.0).</small></p>");
    m_details->setHtml(html);
}

std::optional<QDateTime> MainWindow::launchDateTime() const
{
    const QDate date = m_dateEdit->date();
    const QTime time = m_timeEdit->time();
    const QDateTime t = m_settings.general.useUtc ? QDateTime(date, time, QTimeZone::UTC) : QDateTime(date, time);
    return t.isValid() ? std::optional(t) : std::nullopt;
}

std::shared_ptr<sp::WindField> MainWindow::windField()
{
    // Reuse downloaded winds for a while; start fresh when stale or the grid changed.
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!m_field || m_field->gridStep() != m_settings.wind.gridStep || m_fieldCreated.secsTo(nowUtc) > kCacheMaxAgeSecs) {
        m_field = std::make_shared<sp::WindField>(m_settings.wind.gridStep);
        m_fieldCreated = nowUtc;
        m_predictor.reset();
    }
    return m_field;
}

void MainWindow::runPrediction()
{
    const sp::Location *site = m_settings.location(m_siteCombo->currentText());
    if (!site) {
        QMessageBox::information(this, tr("New prediction"), tr("Add a launch location in Settings first."));
        return;
    }
    const auto launch = launchDateTime();
    if (!launch)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (launch->toSecsSinceEpoch() > now + kMaxDaysAhead * 86400LL) {
        QMessageBox::information(this, tr("New prediction"), tr("GFS forecasts reach about %1 days ahead.").arg(kMaxDaysAhead));
        return;
    }

    const sp::Location *home = m_homeCombo->currentIndex() > 0 ? m_settings.location(m_homeCombo->currentText()) : nullptr;
    m_pending = {site->name, home ? home->name : QString(), home ? std::optional(home->position) : std::nullopt};
    rememberManualRunChoices();

    sp::LaunchParams params;
    params.site = site->position;
    params.siteAltitude = site->altitude;
    params.launchTime = launch->toSecsSinceEpoch();
    params.profile = m_settings.profile;

    windField();
    if (!m_predictor) {
        m_predictor = std::make_unique<sp::Predictor>(m_client.get(), m_field);
        connect(m_predictor.get(), &sp::Predictor::progress, this, [this](const QString &message) { m_statusLabel->setText(message + u'…'); });
        connect(m_predictor.get(), &sp::Predictor::finished, this, &MainWindow::onPredictionFinished);
        connect(m_predictor.get(), &sp::Predictor::failed, this, &MainWindow::onPredictionFailed);
    }

    setBusy(true, tr("Starting prediction for %1…").arg(site->name));
    m_predictor->start(params);
}

void MainWindow::onPredictionFinished(const sp::Prediction &prediction)
{
    setBusy(false);
    sp::PredictionRecord record;
    record.siteName = m_pending.siteName;
    record.homeName = m_pending.homeName;
    record.home = m_pending.home;
    record.manual = true;
    record.model = m_settings.wind.model;
    record.prediction = prediction;

    const qint64 id = m_store.isOpen() ? m_store.save(record, m_settings.general.overwritePredictions) : 0;
    if (!id) {
        m_statusLabel->setText(tr("Prediction done, but it could not be saved: %1").arg(m_store.lastError()));
        m_map->model()->setPredictions({record});
        showDetails(record);
        return;
    }
    refreshTree(id);
    const sp::TrackPoint &land = *prediction.result.landing;
    m_statusLabel->setText(record.home ? tr("Landing %1 from %2").arg(km(sp::inverse(*record.home, land.position).distance), record.homeName)
                                       : tr("Prediction done"));
}

void MainWindow::onPredictionFailed(const QString &error)
{
    qWarning().noquote() << "Prediction failed:" << error;
    setBusy(false, tr("Prediction failed"));
    QMessageBox::warning(this, tr("Prediction failed"), error);
}

bool MainWindow::isBusy() const
{
    return m_busy;
}

void MainWindow::setBusy(bool busy, const QString &message)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    m_runButton->setText(busy ? tr("&Stop") : tr("&Start"));
    m_runButton->setEnabled(busy || m_siteCombo->count() > 0);
    if (QAction *settings = findChild<QAction *>(QStringLiteral("settingsAction")))
        settings->setEnabled(!busy);
    updateBatchControls();
    if (!message.isEmpty())
        m_statusLabel->setText(message);
}

void MainWindow::stopRunning()
{
    if (m_predictor)
        m_predictor->cancel();
    if (m_batch && m_batch->isRunning()) {
        m_batch->cancel();
        refreshTree(selectedIds().value(0)); // show what the batch saved so far
    }
    setBusy(false, tr("Stopped"));
}

void MainWindow::updateBatchControls()
{
    const bool haveRule = m_settings.schedule.has_value();
    const bool busy = isBusy();
    m_batchButton->setEnabled(haveRule && !busy);
    m_batchButton->setToolTip(haveRule ? tr("Run the batch set up in Settings → Batch")
                                       : tr("Set up a batch in Settings → Batch first"));
    const bool canSchedule = haveRule && m_settings.schedule->intervalHours > 0;
    const QSignalBlocker blocker(m_schedulerCheck);
    m_schedulerCheck->setEnabled(canSchedule);
    m_schedulerCheck->setChecked(canSchedule && m_settings.schedule->autoRun);
    if (!canSchedule)
        m_schedulerCheck->setToolTip(tr("Set a repeat interval in Settings → Batch first"));
}

void MainWindow::updateScheduleTimer()
{
    const bool on = m_settings.schedule && m_settings.schedule->autoRun && m_settings.schedule->intervalHours > 0;
    if (!on) {
        m_scheduleTimer.stop();
        m_nextScheduledRun = {};
        if (m_schedulerCheck->isEnabled())
            m_schedulerCheck->setToolTip(tr("Repeat the batch every %n hour(s) while Sonde Predict is open", nullptr,
                                            m_settings.schedule ? m_settings.schedule->intervalHours : 0));
        return;
    }
    const int ms = m_settings.schedule->intervalHours * 3600 * 1000;
    m_scheduleTimer.start(ms);
    m_nextScheduledRun = QDateTime::currentDateTimeUtc().addMSecs(ms);
    m_schedulerCheck->setToolTip(tr("Next batch at %1").arg(formatTime(double(m_nextScheduledRun.toSecsSinceEpoch()))));
}

void MainWindow::startBatch(bool scheduled)
{
    if (isBusy() || !m_settings.schedule) {
        if (scheduled)
            qInfo() << "Scheduled batch skipped: busy or no batch set up";
        return;
    }
    const sp::BatchPlan plan = sp::planBatch(*m_settings.schedule, m_settings, QDateTime::currentSecsSinceEpoch(), kMaxDaysAhead);
    if (!plan.error.isEmpty() || plan.jobs.empty()) {
        const QString error = plan.error.isEmpty() ? tr("all launches are beyond the forecast range") : plan.error;
        qWarning().noquote() << "Batch not started:" << error;
        if (scheduled)
            m_statusLabel->setText(tr("Scheduled batch not started: %1").arg(error));
        else
            QMessageBox::information(this, tr("Batch"), tr("The batch cannot start: %1.").arg(error));
        return;
    }

    m_batch = std::make_unique<sp::BatchRunner>(m_client.get(), windField());
    m_batchScheduled = scheduled;
    m_batchSaved = 0;
    m_batchErrors.clear();
    connect(m_batch.get(), &sp::BatchRunner::jobStarted, this, [this](int index, int total, const sp::BatchJob &job) {
        m_statusLabel->setText(tr("Batch %1/%2: %3 %4…").arg(index + 1).arg(total).arg(job.siteName, formatTime(double(job.params.launchTime))));
    });
    connect(m_batch.get(), &sp::BatchRunner::jobFinished, this,
            [this](int, const sp::BatchJob &job, const sp::Prediction &prediction) { onBatchJobFinished(job, prediction); });
    connect(m_batch.get(), &sp::BatchRunner::jobFailed, this, [this](int, const sp::BatchJob &job, const QString &error) {
        m_batchErrors << QStringLiteral("%1: %2").arg(formatTime(double(job.params.launchTime)), error);
        qWarning().noquote() << "Batch prediction failed:" << m_batchErrors.last();
    });
    connect(m_batch.get(), &sp::BatchRunner::finished, this, [this](int succeeded, int failed) {
        setBusy(false, tr("Batch done: %n prediction(s) saved", nullptr, m_batchSaved)
                           + (failed ? tr(", %n failed", nullptr, failed) : QString()));
        qInfo() << "Batch done:" << succeeded << "succeeded," << failed << "failed";
        refreshTree();
        if (failed && !m_batchScheduled) {
            QMessageBox::warning(this, tr("Batch"), tr("%n prediction(s) failed:", nullptr, failed) + u'\n'
                                     + m_batchErrors.mid(0, 5).join(u'\n'));
        }
    });

    qInfo() << "Batch started:" << plan.jobs.size() << "predictions" << (scheduled ? "(scheduled)" : "");
    setBusy(true, tr("Starting a batch of %n prediction(s)…", nullptr, int(plan.jobs.size())));
    m_batch->start(plan.jobs);
}

void MainWindow::onBatchJobFinished(const sp::BatchJob &job, const sp::Prediction &prediction)
{
    sp::PredictionRecord record;
    record.siteName = job.siteName;
    record.homeName = job.homeName;
    record.home = job.home;
    record.manual = false;
    record.model = m_settings.wind.model;
    record.prediction = prediction;
    if (m_store.isOpen() && m_store.save(record, m_settings.general.overwritePredictions)) {
        ++m_batchSaved;
        refreshTree(selectedIds().value(0)); // show results as they come in
    } else {
        m_batchErrors << tr("could not save: %1").arg(m_store.lastError());
    }
}

std::vector<sp::PredictionRecord> MainWindow::selectedRecords() const
{
    std::vector<sp::PredictionRecord> records;
    for (qint64 id : selectedIds()) {
        if (auto record = m_store.load(id))
            records.push_back(std::move(*record));
    }
    return records;
}

void MainWindow::openInGoogleEarth()
{
    const auto records = selectedRecords();
    if (records.empty())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/kml");
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("sondepredict-%1.kml")
                                                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (const QString error = sp::writeFile(path, sp::toKml(records)); !error.isEmpty()) {
        QMessageBox::warning(this, tr("Google Earth"), error);
        return;
    }

    const QString earth = m_settings.general.googleEarthPath;
    const bool started = !earth.isEmpty() && QFileInfo(earth).isExecutable()
        ? QProcess::startDetached(earth, {QDir::toNativeSeparators(path)})
        : QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    if (!started) {
        QMessageBox::information(this, tr("Google Earth"),
                                 tr("No program opened the KML file. Install Google Earth Pro, or set its path in "
                                    "Settings → General.\n\nThe file was saved as %1").arg(QDir::toNativeSeparators(path)));
    } else {
        m_statusLabel->setText(tr("Opened %n prediction(s) in Google Earth", nullptr, int(records.size())));
    }
}

void MainWindow::exportSelected(bool gpx)
{
    const auto records = selectedRecords();
    if (records.empty())
        return;
    const sp::PredictionRecord &first = records.front();
    QString name = QStringLiteral("%1-%2").arg(first.siteName, QDateTime::fromSecsSinceEpoch(first.prediction.params.launchTime, QTimeZone::UTC)
                                                                   .toString(QStringLiteral("yyyy-MM-dd-HHmm")));
    name.replace(QRegularExpression(QStringLiteral("[^\\w.-]+")), QStringLiteral("_"));
    const QString suffix = gpx ? QStringLiteral("gpx") : QStringLiteral("kml");
    const QString path = QFileDialog::getSaveFileName(this, gpx ? tr("Export GPX") : tr("Export KML"),
                                                      QDir(QDir::homePath()).filePath(name + u'.' + suffix),
                                                      gpx ? tr("GPX files (*.gpx)") : tr("KML files (*.kml)"));
    if (path.isEmpty())
        return;
    if (const QString error = sp::writeFile(path, gpx ? sp::toGpx(records) : sp::toKml(records)); !error.isEmpty())
        QMessageBox::warning(this, tr("Export"), error);
    else
        m_statusLabel->setText(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::deleteSelected()
{
    const QList<qint64> ids = selectedIds();
    if (ids.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete predictions"), tr("Delete %n prediction(s)?", nullptr, int(ids.size())))
        != QMessageBox::Yes) {
        return;
    }
    if (!m_store.remove(ids))
        QMessageBox::warning(this, tr("Delete predictions"), m_store.lastError());
    refreshTree();
}

void MainWindow::openSettings()
{
    rememberManualRunChoices();
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_settings = dialog.settings();
    saveSettings();
    applySettings();
    refreshLocationCombos();
    refreshTree(selectedIds().value(0));
    updateScheduleTimer();
}

void MainWindow::importV3Settings()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Sonde Predict v3 settings"), QDir::homePath(),
                                                      tr("Sonde Predict v3 settings (config.ini);;INI files (*.ini);;All files (*)"));
    if (path.isEmpty())
        return;
    rememberManualRunChoices();
    sp::AppSettings updated = m_settings;
    const sp::V3ImportResult result = sp::importV3Config(path, updated);
    if (!result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Import"), result.error);
        return;
    }
    m_settings = updated;
    saveSettings();
    applySettings();
    refreshLocationCombos();
    refreshTree(selectedIds().value(0));
    QMessageBox::information(this, tr("Import"), result.messages.join(u'\n'));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Sonde Predict"),
                       tr("<h3>Sonde Predict %1</h3>"
                          "<p>Predicts radiosonde flight paths and landing points from GFS upper-air winds.</p>"
                          "<p>A rewrite of Sonde Predict v3 (2016–2022).</p>"
                          "<p>Weather data by <a href=\"https://open-meteo.com/\">Open-Meteo.com</a> (CC BY 4.0), "
                          "from NOAA GFS. Map data © <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap</a> contributors.</p>")
                           .arg(QApplication::applicationVersion()));
}
