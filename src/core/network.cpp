#include "network.h"

#include <QNetworkProxyFactory>

namespace sp {

QNetworkProxy proxyFromUrl(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return QNetworkProxy(QNetworkProxy::NoProxy);
    const bool socks = url.scheme().startsWith(QStringLiteral("socks"));
    return QNetworkProxy(socks ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy, url.host(),
                         static_cast<quint16>(url.port(socks ? 1080 : 8080)), url.userName(), url.password());
}

QString applyProxy(const NetworkSettings &settings)
{
    QNetworkProxyFactory::setUseSystemConfiguration(false);

    switch (settings.proxyMode) {
    case ProxyMode::None:
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        return {};
    case ProxyMode::Manual: {
        // v3 stored addresses like "proxy.example.com:8080"; assume http:// when no scheme is given.
        QString text = settings.proxyUrl.trimmed();
        if (!text.contains(QStringLiteral("://")))
            text.prepend(QStringLiteral("http://"));
        const QNetworkProxy proxy = proxyFromUrl(QUrl(text));
        if (proxy.type() == QNetworkProxy::NoProxy) {
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
            return QStringLiteral("invalid proxy address \"%1\"; connecting directly").arg(settings.proxyUrl);
        }
        QNetworkProxy::setApplicationProxy(proxy);
        return {};
    }
    case ProxyMode::System:
        break;
    }

    for (const char *name : {"https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY", "http_proxy", "HTTP_PROXY"}) {
        const QNetworkProxy proxy = proxyFromUrl(QUrl(qEnvironmentVariable(name)));
        if (proxy.type() != QNetworkProxy::NoProxy) {
            QNetworkProxy::setApplicationProxy(proxy);
            return {};
        }
    }
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    return {};
}

} // namespace sp
