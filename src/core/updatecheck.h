#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace sp {

// The update check: is there a newer version? The answer is a small JSON document
// {version, url, message}; a failed request simply yields no result.
class UpdateCheck : public QObject {
    Q_OBJECT
public:
    struct Result {
        QString latest;  // version on the server
        QString url;     // where to get it (https only)
        QString message; // optional short note from the project
        bool newer = false;
        bool valid = false;
    };

    static constexpr const char *defaultUrl = "https://oh2gba.eu/sondepredict/version.php";

    explicit UpdateCheck(QNetworkAccessManager *network, QObject *parent = nullptr);

    // "5.0.10" vs "5.0.9" → 1; equal → 0; a missing part counts as 0, "5.1.0-rc1" < "5.1.0".
    static int compareVersions(const QString &a, const QString &b);
    // "linux", "linux-flatpak", "linux-appimage", "windows", "macos"
    static QString platformName();
    // The endpoint with ?v=<version>&os=<platform> added.
    static QUrl requestUrl(const QUrl &endpoint, const QString &version, const QString &platform);
    static QString userAgent(const QString &version, const QString &platform);
    static Result parseResponse(const QByteArray &json, const QString &currentVersion);

    bool isBusy() const { return m_busy; }

    // Starts a request unless one is running; finished() follows with the answer, or with an
    // invalid Result when the server could not be reached or understood.
    void run(const QUrl &endpoint);

signals:
    void finished(const sp::UpdateCheck::Result &result);

private:
    QNetworkAccessManager *m_network;
    bool m_busy = false;
};

} // namespace sp
