#include "groundtexture.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr int kTileSize = 256;
constexpr int kMaxTiles = 80; // OSM's tile policy asks for moderate use

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/ground3d");
}

} // namespace

struct GroundTexture::Job {
    GeoBox box;
    int z = 0;
    int x0 = 0, y0 = 0, nx = 0, ny = 0;
    QImage mosaic;
    int pending = 0;
    bool cancelled = false;
};

GroundTexture::GroundTexture(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    auto *cache = new QNetworkDiskCache(m_network);
    cache->setCacheDirectory(cacheDir() + QStringLiteral("/tiles"));
    cache->setMaximumCacheSize(200 * 1024 * 1024);
    m_network->setCache(cache);
}

GroundTexture::~GroundTexture()
{
    if (m_current)
        m_current->cancelled = true;
}

double GroundTexture::tileX(double lon, int z)
{
    return (lon + 180.0) / 360.0 * std::ldexp(1.0, z);
}

double GroundTexture::tileY(double lat, int z)
{
    const double phi = std::clamp(lat, -85.0, 85.0) * std::numbers::pi / 180.0;
    return (1.0 - std::log(std::tan(phi) + 1.0 / std::cos(phi)) / std::numbers::pi) / 2.0 * std::ldexp(1.0, z);
}

int GroundTexture::zoomFor(const GeoBox &box, int targetPixels)
{
    const double span = std::max(box.east - box.west, 0.01);
    int z = static_cast<int>(std::floor(std::log2(targetPixels / double(kTileSize) * 360.0 / span)));
    z = std::clamp(z, 3, 13);
    // Keep the number of tiles moderate (tall boxes at high latitude need many rows).
    while (z > 3) {
        const double nx = std::floor(tileX(box.east, z)) - std::floor(tileX(box.west, z)) + 1;
        const double ny = std::floor(tileY(box.south, z)) - std::floor(tileY(box.north, z)) + 1;
        if (nx * ny <= kMaxTiles)
            break;
        --z;
    }
    return z;
}

QImage GroundTexture::reproject(const QImage &mosaic, int x0, int y0, int z, const GeoBox &box, QSize size)
{
    QImage out(size, QImage::Format_RGB32);
    out.fill(QColor(0xdd, 0xdd, 0xdd));
    const QImage src = mosaic.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        const double lat = box.north - (y + 0.5) / size.height() * (box.north - box.south);
        const int sy = static_cast<int>((tileY(lat, z) - y0) * kTileSize);
        if (sy < 0 || sy >= src.height())
            continue;
        const auto *in = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const double lon = box.west + (x + 0.5) / size.width() * (box.east - box.west);
            const int sx = static_cast<int>((tileX(lon, z) - x0) * kTileSize);
            if (sx >= 0 && sx < src.width())
                line[x] = in[sx];
        }
    }
    return out;
}

void GroundTexture::request(const GeoBox &box)
{
    if (m_current)
        m_current->cancelled = true;
    auto job = std::make_shared<Job>();
    m_current = job;
    job->box = box;
    job->z = zoomFor(box);
    job->x0 = static_cast<int>(std::floor(tileX(box.west, job->z)));
    job->y0 = static_cast<int>(std::floor(tileY(box.north, job->z)));
    job->nx = static_cast<int>(std::floor(tileX(box.east, job->z))) - job->x0 + 1;
    job->ny = static_cast<int>(std::floor(tileY(box.south, job->z))) - job->y0 + 1;
    job->mosaic = QImage(job->nx * kTileSize, job->ny * kTileSize, QImage::Format_RGB32);
    job->mosaic.fill(QColor(0xdd, 0xdd, 0xdd));

    const QString agent = QStringLiteral("SondePredict/%1 (radiosonde landing predictor)").arg(QCoreApplication::applicationVersion());
    const int tiles = 1 << job->z;
    for (int ty = job->y0; ty < job->y0 + job->ny; ++ty) {
        for (int tx = job->x0; tx < job->x0 + job->nx; ++tx) {
            QNetworkRequest request(QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png")
                                             .arg(job->z).arg(((tx % tiles) + tiles) % tiles).arg(ty)));
            request.setHeader(QNetworkRequest::UserAgentHeader, agent);
            request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
            request.setTransferTimeout(20000);
            ++job->pending;
            QNetworkReply *reply = m_network->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, job, tx, ty, reply]() { finishTile(job, tx, ty, reply); });
        }
    }
}

void GroundTexture::finishTile(const std::shared_ptr<Job> &job, int tx, int ty, QNetworkReply *reply)
{
    reply->deleteLater();
    if (!job->cancelled && reply->error() == QNetworkReply::NoError) {
        QImage tile;
        if (tile.loadFromData(reply->readAll())) {
            QPainter painter(&job->mosaic);
            painter.drawImage((tx - job->x0) * kTileSize, (ty - job->y0) * kTileSize, tile);
        }
    }
    if (--job->pending == 0 && !job->cancelled)
        complete(job);
}

void GroundTexture::complete(const std::shared_ptr<Job> &job)
{
    // Same pixel size as the mosaic across, height from the box's real proportions.
    const GeoBox &b = job->box;
    const double widthPx = (tileX(b.east, job->z) - tileX(b.west, job->z)) * kTileSize;
    const double midLat = (b.north + b.south) / 2.0 * std::numbers::pi / 180.0;
    const double aspect = (b.north - b.south) / ((b.east - b.west) * std::cos(midLat));
    const QSize size(std::clamp(int(widthPx), 64, 4096), std::clamp(int(widthPx * aspect), 64, 4096));
    const QImage image = reproject(job->mosaic, job->x0, job->y0, job->z, b, size);

    QDir().mkpath(cacheDir());
    const QByteArray key = QCryptographicHash::hash(QStringLiteral("%1,%2,%3,%4,%5").arg(b.south).arg(b.west).arg(b.north).arg(b.east).arg(job->z).toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(16);
    const QString path = cacheDir() + QStringLiteral("/ground-") + QString::fromLatin1(key) + QStringLiteral(".jpg");
    image.save(path, "JPG", 90);
    if (m_current == job)
        m_current.reset();
    emit ready(QUrl::fromLocalFile(path), b);
}
