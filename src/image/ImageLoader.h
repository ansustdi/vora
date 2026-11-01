#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>

struct LoadedImage
{
    QString path;
    QImage image;
    bool animated = false;
    QString format;
    QSize originalSize; // as displayed (EXIF rotation applied)
    int pages = 1;       // multi-page TIFF etc.: only the first page is shown / edited
    bool highDepth = false; // 16-bit / float pixels: saving over the file would lose precision
    QString error;
};

// Decodes images on the thread pool, keeps a small LRU cache and prefetches neighbours.
class ImageLoader : public QObject
{
    Q_OBJECT
public:
    explicit ImageLoader(QObject *parent = nullptr);

    void request(const QString &path);
    void prefetch(const QStringList &paths);
    void invalidate(const QString &path);

    static LoadedImage loadSync(const QString &path);

signals:
    void ready(const LoadedImage &image);

private:
    void start(const QString &path);

    QCache<QString, LoadedImage> m_cache; // cost = MiB
    QSet<QString> m_pending;
    QString m_wanted;
};
