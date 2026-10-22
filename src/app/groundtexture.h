#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>

#include <map>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// A geographic rectangle in degrees.
struct GeoBox {
    double south = 0, west = 0, north = 0, east = 0;
    bool isValid() const { return north > south && east > west; }
    bool operator==(const GeoBox &) const = default;
};

// Builds a ground image for the 3D view from OpenStreetMap tiles: downloads (and caches) the
// tiles covering a box, stitches them and reprojects them so that the image is linear in
// latitude and longitude, matching the flat ground plane of the scene.
class GroundTexture : public QObject {
    Q_OBJECT
public:
    explicit GroundTexture(QObject *parent = nullptr);
    ~GroundTexture() override;

    // Starts building the image for `box`; ready() follows (also when tiles fail: grey parts).
    void request(const GeoBox &box);

    // Tile zoom level that gives an image about `targetPixels` wide for `box`.
    static int zoomFor(const GeoBox &box, int targetPixels = 2048);
    // Web Mercator tile coordinates (fractional) of a point at zoom `z`.
    static double tileX(double lon, int z);
    static double tileY(double lat, int z);
    // Reprojects a Mercator mosaic whose top-left tile is (x0, y0) at zoom z onto `box`.
    static QImage reproject(const QImage &mosaic, int x0, int y0, int z, const GeoBox &box, QSize size);

signals:
    void ready(const QUrl &image, const GeoBox &box);

private:
    struct Job;
    void finishTile(const std::shared_ptr<Job> &job, int tx, int ty, QNetworkReply *reply);
    void complete(const std::shared_ptr<Job> &job);

    QNetworkAccessManager *m_network;
    std::shared_ptr<Job> m_current;
};
