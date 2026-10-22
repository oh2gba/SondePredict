#pragma once

#include "settings.h"

#include <QNetworkProxy>
#include <QString>
#include <QUrl>

namespace sp {

// A QNetworkProxy from a URL such as http://user:pass@host:8080 or socks5://host:1080.
// Returns NoProxy if the URL has no host.
QNetworkProxy proxyFromUrl(const QUrl &url);

// Sets the proxy for all Qt network access in this process.
// System: https_proxy / all_proxy / http_proxy if set, otherwise the operating system's settings.
// Returns an error message for an unusable manual proxy URL (the setting is then ignored).
QString applyProxy(const NetworkSettings &settings);

} // namespace sp
