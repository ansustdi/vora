#pragma once

#include <QImage>
#include <QList>
#include <QPair>
#include <QString>

// EXIF access via exiv2 (compiled in when HAVE_EXIV2); harmless no-ops otherwise.
namespace Exif {

using Fields = QList<QPair<QString, QString>>;

struct Info
{
    bool ok = false;
    Fields summary; // camera, lens, exposure, GPS...
    Fields all;     // every tag, human readable
    int orientation = 1;
};

bool available();
Info read(const QString &path);
bool isRaw(const QString &path);
QImage rawPreview(const QString &path);

// Lossless rotation for JPEG: rewrite only the Orientation tag.
// `clockwiseDegrees` is added to the file's current orientation; fails for mirrored orientations.
bool rotateLossless(const QString &path, int clockwiseDegrees, QString *error);

// After re-encoding `dstPath`, copy metadata from the original file bytes (orientation reset to 1).
bool copyMetadata(const QByteArray &srcBytes, const QString &dstPath);

} // namespace Exif
