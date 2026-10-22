// Sonde Predict desktop application.

#include "mainwindow.h"
#include "predictionstore.h"
#include "settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMutex>
#include <QTextStream>
#include <QPushButton>
#include <QTimer>

#include <algorithm>

namespace {

QString s_logPath;
QtMessageHandler s_defaultHandler = nullptr;

// Everything Qt logs also goes to log.txt next to the prediction database (v3 had log.txt too).
void logToFile(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static QMutex mutex;
    if (type != QtDebugMsg && !s_logPath.isEmpty()) {
        const QMutexLocker lock(&mutex);
        QFile file(s_logPath);
        if (file.size() > 1024 * 1024) {
            QFile::remove(s_logPath + QStringLiteral(".old"));
            QFile::rename(s_logPath, s_logPath + QStringLiteral(".old"));
        }
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            static const char *const levels[] = {"debug", "warning", "critical", "fatal", "info"};
            QTextStream(&file) << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << ' '
                               << levels[std::clamp(int(type), 0, 4)] << ": " << message << '\n';
        }
    }
    if (s_defaultHandler)
        s_defaultHandler(type, context, message);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SondePredict"));
    QApplication::setApplicationDisplayName(QStringLiteral("Sonde Predict"));
    QApplication::setApplicationVersion(QStringLiteral(SP_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("sondepredict"));
    QIcon icon(QStringLiteral(":/icons/sondepredict.svg"));
    for (int size : {16, 32, 48, 256})
        icon.addFile(QStringLiteral(":/icons/sondepredict-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Radiosonde flight path and landing prediction."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {QStringLiteral("config"), QStringLiteral("Settings file."), QStringLiteral("file"), sp::AppSettings::defaultPath()},
        {QStringLiteral("db"), QStringLiteral("Prediction database."), QStringLiteral("file"), sp::PredictionStore::defaultPath()},
    });
    parser.process(app);

    const QString dbPath = parser.value(QStringLiteral("db"));
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    s_logPath = QFileInfo(dbPath).dir().filePath(QStringLiteral("log.txt"));
    s_defaultHandler = qInstallMessageHandler(logToFile);
    qInfo().noquote() << "Sonde Predict" << QApplication::applicationVersion() << "started";

    MainWindow window(parser.value(QStringLiteral("config")), dbPath);
    window.show();

    // Development aids: SP_AUTORUN=1 (or =batch) presses Start (or Start a batch) once the
    // window is up; SP_SCREENSHOT=file.png saves the window after SP_SCREENSHOT_DELAY_MS
    // (default 5000) and quits.
    if (qEnvironmentVariableIsSet("SP_AUTORUN")) {
        const QString button = qEnvironmentVariable("SP_AUTORUN") == QLatin1String("batch") ? QStringLiteral("batchButton")
                                                                                            : QStringLiteral("runButton");
        QTimer::singleShot(500, &window, [&window, button]() {
            if (auto *run = window.findChild<QPushButton *>(button))
                run->click();
        });
    }
    if (const QString shot = qEnvironmentVariable("SP_SCREENSHOT"); !shot.isEmpty()) {
        QTimer::singleShot(qEnvironmentVariableIntValue("SP_SCREENSHOT_DELAY_MS") > 0 ? qEnvironmentVariableIntValue("SP_SCREENSHOT_DELAY_MS") : 5000,
                           &window, [&window, shot]() {
                               window.grab().save(shot);
                               QApplication::quit();
                           });
    }
    return app.exec();
}
