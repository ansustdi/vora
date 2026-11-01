#include "OsHash.h"

#include <QFile>
#include <QtEndian>

static constexpr qint64 kChunk = 65536;

static quint64 sumChunk(const QByteArray &data)
{
    quint64 sum = 0;
    const int words = data.size() / 8;
    for (int i = 0; i < words; ++i)
        sum += qFromLittleEndian<quint64>(data.constData() + i * 8);
    return sum;
}

QString openSubtitlesHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = f.size();
    if (size < kChunk * 2)
        return {};

    quint64 hash = quint64(size);
    hash += sumChunk(f.read(kChunk));
    if (!f.seek(size - kChunk))
        return {};
    hash += sumChunk(f.read(kChunk));
    return QString("%1").arg(hash, 16, 16, QChar('0'));
}
