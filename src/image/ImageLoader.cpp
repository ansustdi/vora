#include "ImageLoader.h"

#include "Exif.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QtConcurrent>

static int costOf(const QImage &img)
{
    return qMax<qsizetype>(1, img.sizeInBytes() / (1024 * 1024));
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
    m_cache.setMaxCost(768); // MiB of decoded pixels
    QImageReader::setAllocationLimit(2048);
}

LoadedImage ImageLoader::loadSync(const QString &path)
{
    LoadedImage r;
    r.path = path;
    {
        QImageReader probe(path);
        r.format = QString::fromLatin1(probe.format()).toUpper();
        r.animated = probe.supportsAnimation() && probe.imageCount() > 1;
        if (!r.animated)
            r.pages = qMax(1, probe.imageCount());
    }

    QImageReader reader(path);
    reader.setAutoTransform(true); // honour EXIF orientation
    r.originalSize = reader.size();
    if (r.originalSize.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90))
        r.originalSize.transpose();
    QImage img;
    // RAW files are TIFF-like: Qt would decode their tiny first-IFD thumbnail. The embedded
    // full-size JPEG preview is what the user wants to see.
    if (Exif::isRaw(path)) {
        img = Exif::rawPreview(path);
        if (!img.isNull()) {
            r.format = QFileInfo(path).suffix().toUpper() + " (preview)";
            r.originalSize = img.size();
            r.pages = 1;
        }
    }
    if (img.isNull() && !reader.read(&img)) {
        r.error = reader.errorString();
        return r;
    }
    if (!r.originalSize.isValid())
        r.originalSize = img.size();
    switch (img.format()) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_Grayscale16:
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        r.highDepth = true;
        break;
    default:
        break;
    }

    // Normalise to sRGB and a fast-to-paint format.
    if (img.colorSpace().isValid() && img.colorSpace() != QColorSpace::SRgb)
        img.convertToColorSpace(QColorSpace::SRgb);
    img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    r.image = img;
    return r;
}

void ImageLoader::request(const QString &path)
{
    m_wanted = path;
    if (LoadedImage *cached = m_cache.object(path)) {
        emit ready(*cached);
        return;
    }
    if (!m_pending.contains(path))
        start(path);
}

void ImageLoader::prefetch(const QStringList &paths)
{
    for (const QString &p : paths)
        if (!p.isEmpty() && !m_cache.contains(p) && !m_pending.contains(p))
            start(p);
}

void ImageLoader::invalidate(const QString &path)
{
    m_cache.remove(path);
}

void ImageLoader::start(const QString &path)
{
    m_pending.insert(path);
    auto *watcher = new QFutureWatcher<LoadedImage>(this);
    connect(watcher, &QFutureWatcher<LoadedImage>::finished, this, [this, watcher] {
        const LoadedImage r = watcher->result();
        watcher->deleteLater();
        m_pending.remove(r.path);
        if (r.error.isEmpty())
            m_cache.insert(r.path, new LoadedImage(r), costOf(r.image));
        if (r.path == m_wanted)
            emit ready(r);
    });
    watcher->setFuture(QtConcurrent::run(&ImageLoader::loadSync, path));
}
