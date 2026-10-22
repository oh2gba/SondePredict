// Sonde Predict desktop application.

#include "mainwindow.h"
#include "predictionstore.h"
#include "settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QPushButton>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SondePredict"));
    QApplication::setApplicationDisplayName(QStringLiteral("Sonde Predict"));
    QApplication::setApplicationVersion(QStringLiteral(SP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Radiosonde flight path and landing prediction."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {QStringLiteral("config"), QStringLiteral("Settings file."), QStringLiteral("file"), sp::AppSettings::defaultPath()},
        {QStringLiteral("db"), QStringLiteral("Prediction database."), QStringLiteral("file"), sp::PredictionStore::defaultPath()},
    });
    parser.process(app);

    MainWindow window(parser.value(QStringLiteral("config")), parser.value(QStringLiteral("db")));
    window.show();

    // Development aids: SP_AUTORUN=1 presses Start once the window is up; SP_SCREENSHOT=file.png
    // saves the window after SP_SCREENSHOT_DELAY_MS (default 5000) and quits.
    if (qEnvironmentVariableIsSet("SP_AUTORUN")) {
        QTimer::singleShot(500, &window, [&window]() {
            if (auto *run = window.findChild<QPushButton *>(QStringLiteral("runButton")))
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
