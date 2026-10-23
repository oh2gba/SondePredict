#include "updatecheck.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace sp {

UpdateCheck::UpdateCheck(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

int UpdateCheck::compareVersions(const QString &a, const QString &b)
{
    static const QRegularExpression separator(QStringLiteral("[.\\-+]"));
    const QStringList pa = a.trimmed().split(separator, Qt::SkipEmptyParts);
    const QStringList pb = b.trimmed().split(separator, Qt::SkipEmptyParts);
    const qsizetype n = std::max(pa.size(), pb.size());
    for (qsizetype i = 0; i < n; ++i) {
        // A missing part counts as 0, so "5.0" equals "5.0.0".
        bool okA = true, okB = true;
        const int va = pa.value(i).isEmpty() ? 0 : pa.value(i).toInt(&okA);
        const int vb = pb.value(i).isEmpty() ? 0 : pb.value(i).toInt(&okB);
        if (okA && okB) {
            if (va != vb)
                return va < vb ? -1 : 1;
            continue;
        }
        // "5.1.0-rc1" sorts before "5.1.0"; two suffixes compare as text.
        if (okA != okB)
            return okA ? 1 : -1;
        const int c = pa.value(i).compare(pb.value(i), Qt::CaseInsensitive);
        if (c != 0)
            return c < 0 ? -1 : 1;
    }
    return 0;
}

QString UpdateCheck::platformName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID"))
        return QStringLiteral("linux-flatpak");
    if (!qEnvironmentVariableIsEmpty("APPIMAGE"))
        return QStringLiteral("linux-appimage");
    return QStringLiteral("linux");
#endif
}

QUrl UpdateCheck::requestUrl(const QUrl &endpoint, const QString &version, const QString &platform)
{
    QUrl url = endpoint;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("v"), version);
    query.addQueryItem(QStringLiteral("os"), platform);
    url.setQuery(query);
    return url;
}

QString UpdateCheck::userAgent(const QString &version, const QString &platform)
{
    return QStringLiteral("SondePredict/%1 (%2)").arg(version, platform);
}

UpdateCheck::Result UpdateCheck::parseResponse(const QByteArray &json, const QString &currentVersion)
{
    Result r;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return r;
    const QJsonObject o = doc.object();
    r.latest = o.value(QStringLiteral("version")).toString().trimmed();
    r.url = o.value(QStringLiteral("url")).toString().trimmed();
    r.message = o.value(QStringLiteral("message")).toString().trimmed().left(200);
    static const QRegularExpression versionPattern(QStringLiteral("^\\d+(\\.\\d+){0,3}([-+][0-9A-Za-z.]+)?$"));
    if (!versionPattern.match(r.latest).hasMatch())
        return r;
    if (!r.url.isEmpty() && !r.url.startsWith(QLatin1String("https://")))
        r.url.clear();
    r.valid = true;
    r.newer = compareVersions(r.latest, currentVersion) > 0;
    return r;
}

void UpdateCheck::run(const QUrl &endpoint)
{
    if (m_busy || !endpoint.isValid())
        return;

    const QString version = QCoreApplication::applicationVersion();
    const QString platform = platformName();
    QNetworkRequest request(requestUrl(endpoint, version, platform));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent(version, platform));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // Generous: on hosts whose IPv6 route is broken Qt only falls back to IPv4 after the first
    // attempt gives up, which takes a while.
    request.setTransferTimeout(90000);

    m_busy = true;
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, version]() {
        reply->deleteLater();
        m_busy = false;
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(Result()); // no network or no server: never mind
            return;
        }
        emit finished(parseResponse(reply->readAll(), version));
    });
}

} // namespace sp
