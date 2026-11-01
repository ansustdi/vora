#include "Exif.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <cmath>

#ifdef HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace Exif {

static const QSet<QString> kRawExt = {"cr2", "cr3", "nef", "arw", "dng", "orf", "rw2", "raf", "pef", "srw"};

bool isRaw(const QString &path)
{
    return kRawExt.contains(QFileInfo(path).suffix().toLower());
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

#ifdef HAVE_EXIV2

bool available() { return true; }

static long long exivInt(const Exiv2::Exifdatum &d)
{
#if EXIV2_TEST_VERSION(0, 28, 0)
    return d.toInt64();
#else
    return d.toLong();
#endif
}

// Files are read through Qt so non-ASCII Windows paths work; exiv2 only sees memory.
static auto openMemory(const QByteArray &bytes)
{
    return Exiv2::ImageFactory::open(reinterpret_cast<const Exiv2::byte *>(bytes.constData()), bytes.size());
}

static QByteArray ioBytes(Exiv2::BasicIo &io)
{
    io.open();
    io.seek(0, Exiv2::BasicIo::beg);
    Exiv2::DataBuf buf = io.read(io.size());
    io.close();
#if EXIV2_TEST_VERSION(0, 28, 0)
    return QByteArray(reinterpret_cast<const char *>(buf.c_data()), int(buf.size()));
#else
    return QByteArray(reinterpret_cast<const char *>(buf.pData_), int(buf.size_));
#endif
}

static QString printed(const Exiv2::ExifData &exif, const char *key)
{
    const auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end())
        return {};
    return QString::fromStdString(it->print(&exif)).trimmed();
}

static double gpsDegrees(const Exiv2::ExifData &exif, const char *key, const char *refKey)
{
    const auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end() || it->count() < 3)
        return NAN;
    double d = it->toFloat(0) + it->toFloat(1) / 60.0 + it->toFloat(2) / 3600.0;
    const QString ref = printed(exif, refKey);
    if (ref.startsWith('S', Qt::CaseInsensitive) || ref.startsWith('W', Qt::CaseInsensitive))
        d = -d;
    return d;
}

Info read(const QString &path)
{
    Info info;
    try {
        const QByteArray bytes = readFile(path);
        if (bytes.isEmpty())
            return info;
        auto image = openMemory(bytes);
        if (!image)
            return info;
        image->readMetadata();
        const Exiv2::ExifData &exif = image->exifData();
        if (exif.empty())
            return info;
        info.ok = true;

        const auto orient = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (orient != exif.end())
            info.orientation = int(exivInt(*orient));

        auto add = [&](const QString &label, const QString &value) {
            if (!value.isEmpty())
                info.summary << qMakePair(label, value);
        };
        const QString make = printed(exif, "Exif.Image.Make");
        QString model = printed(exif, "Exif.Image.Model");
        if (!make.isEmpty() && !model.startsWith(make, Qt::CaseInsensitive))
            model = make + " " + model;
        add(QCoreApplication::translate("Exif", "Camera"), model);
        add(QCoreApplication::translate("Exif", "Lens"), printed(exif, "Exif.Photo.LensModel"));
        add(QCoreApplication::translate("Exif", "Aperture"), printed(exif, "Exif.Photo.FNumber"));
        add(QCoreApplication::translate("Exif", "Exposure"), printed(exif, "Exif.Photo.ExposureTime"));
        add(QCoreApplication::translate("Exif", "ISO"), printed(exif, "Exif.Photo.ISOSpeedRatings"));
        add(QCoreApplication::translate("Exif", "Focal length"), printed(exif, "Exif.Photo.FocalLength"));
        add(QCoreApplication::translate("Exif", "Flash"), printed(exif, "Exif.Photo.Flash"));
        add(QCoreApplication::translate("Exif", "Taken"), printed(exif, "Exif.Photo.DateTimeOriginal"));
        add(QCoreApplication::translate("Exif", "Software"), printed(exif, "Exif.Image.Software"));
        const double lat = gpsDegrees(exif, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef");
        const double lon = gpsDegrees(exif, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef");
        if (!std::isnan(lat) && !std::isnan(lon))
            add(QCoreApplication::translate("Exif", "GPS"), QString("%1, %2").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));

        for (const auto &d : exif) {
            if (d.size() > 256)
                continue; // maker notes, thumbnails
            QString v = QString::fromStdString(d.print(&exif)).trimmed();
            if (v.size() > 120)
                v = v.left(117) + "...";
            info.all << qMakePair(QString::fromStdString(d.groupName() + "." + d.tagName()), v);
        }
    } catch (const std::exception &) {
    }
    return info;
}

QImage rawPreview(const QString &path)
{
    try {
        const QByteArray bytes = readFile(path);
        auto image = openMemory(bytes);
        if (!image)
            return {};
        image->readMetadata();
        Exiv2::PreviewManager pm(*image);
        const Exiv2::PreviewPropertiesList list = pm.getPreviewProperties();
        if (list.empty())
            return {};
        const Exiv2::PreviewImage preview = pm.getPreviewImage(list.back()); // largest
        QImage img = QImage::fromData(reinterpret_cast<const uchar *>(preview.pData()), int(preview.size()));
        // Embedded previews ignore the RAW's orientation tag.
        const auto &exif = image->exifData();
        const auto orient = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (orient != exif.end()) {
            switch (exivInt(*orient)) {
            case 3: img = img.transformed(QTransform().rotate(180)); break;
            case 6: img = img.transformed(QTransform().rotate(90)); break;
            case 8: img = img.transformed(QTransform().rotate(270)); break;
            default: break;
            }
        }
        return img;
    } catch (const std::exception &) {
        return {};
    }
}

static int orientationToDegrees(int o)
{
    switch (o) {
    case 1: return 0;
    case 6: return 90;
    case 3: return 180;
    case 8: return 270;
    default: return -1; // mirrored
    }
}

static int degreesToOrientation(int deg)
{
    switch (((deg % 360) + 360) % 360) {
    case 90: return 6;
    case 180: return 3;
    case 270: return 8;
    default: return 1;
    }
}

bool rotateLossless(const QString &path, int clockwiseDegrees, QString *error)
{
    try {
        const QByteArray bytes = readFile(path);
        auto image = openMemory(bytes);
        if (!image || image->mimeType() != "image/jpeg") {
            if (error)
                *error = QCoreApplication::translate("Exif", "Not a JPEG file");
            return false;
        }
        image->readMetadata();
        Exiv2::ExifData &exif = image->exifData();
        int current = 1;
        const auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (it != exif.end())
            current = int(exivInt(*it));
        const int deg = orientationToDegrees(current);
        if (deg < 0) {
            if (error)
                *error = QCoreApplication::translate("Exif", "Mirrored orientation");
            return false;
        }
        const uint16_t orientation = uint16_t(degreesToOrientation(deg + clockwiseDegrees));
        exif["Exif.Image.Orientation"] = orientation;
        // Viewers that read XMP first (Lightroom, some web galleries) must agree with EXIF.
        Exiv2::XmpData &xmp = image->xmpData();
        if (xmp.findKey(Exiv2::XmpKey("Xmp.tiff.Orientation")) != xmp.end())
            xmp["Xmp.tiff.Orientation"] = std::to_string(orientation);
        image->writeMetadata();
        return writeFile(path, ioBytes(image->io()));
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromLocal8Bit(e.what());
        return false;
    }
}

bool copyMetadata(const QByteArray &srcBytes, const QString &dstPath)
{
    try {
        auto src = openMemory(srcBytes);
        if (!src)
            return false;
        src->readMetadata();
        const QByteArray dstBytes = readFile(dstPath);
        auto dst = openMemory(dstBytes);
        if (!dst)
            return false;
        dst->readMetadata();
        Exiv2::ExifData exif = src->exifData();
        if (exif.empty())
            return true;
        exif["Exif.Image.Orientation"] = uint16_t(1); // pixels are already upright
        // The old embedded thumbnail shows the unedited picture (Explorer uses it): drop it.
        Exiv2::ExifThumb(exif).erase();
        // Dimensions must describe the new pixels (crop / resize).
        if (dst->pixelWidth() > 0 && dst->pixelHeight() > 0) {
            exif["Exif.Photo.PixelXDimension"] = uint32_t(dst->pixelWidth());
            exif["Exif.Photo.PixelYDimension"] = uint32_t(dst->pixelHeight());
        }
        Exiv2::XmpData xmp = src->xmpData();
        if (xmp.findKey(Exiv2::XmpKey("Xmp.tiff.Orientation")) != xmp.end())
            xmp["Xmp.tiff.Orientation"] = "1";
        dst->setExifData(exif);
        dst->setIptcData(src->iptcData());
        dst->setXmpData(xmp);
        dst->writeMetadata();
        return writeFile(dstPath, ioBytes(dst->io()));
    } catch (const std::exception &) {
        return false;
    }
}

#else // !HAVE_EXIV2

bool available() { return false; }
Info read(const QString &) { return {}; }
QImage rawPreview(const QString &) { return {}; }

bool rotateLossless(const QString &, int, QString *error)
{
    if (error)
        *error = QCoreApplication::translate("Exif", "Built without exiv2");
    return false;
}

bool copyMetadata(const QByteArray &, const QString &) { return false; }

#endif

} // namespace Exif
