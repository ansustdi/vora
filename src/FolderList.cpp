#include "FolderList.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

static const QSet<QString> kVideoExt = {
    "mkv", "mp4", "m4v", "avi", "mov", "wmv", "flv", "webm", "mpg", "mpeg", "ts", "m2ts", "mts",
    "3gp", "ogv", "vob", "rm", "rmvb", "divx", "f4v", "asf",
    "mp3", "flac", "m4a", "aac", "ogg", "opus", "wav", "wma", "ape",
};
static const QSet<QString> kImageExt = {
    "jpg", "jpeg", "jpe", "jfif", "png", "apng", "gif", "webp", "bmp", "dib", "tif", "tiff", "svg", "svgz",
    "ico", "icns", "tga", "pbm", "pgm", "ppm", "xbm", "xpm",
    "avif", "heic", "heif", "jxl", "jp2", "exr", "hdr", "psd",
    "cr2", "cr3", "nef", "arw", "dng", "orf", "rw2", "raf", "pef", "srw",
};
static const QSet<QString> kAudioExt = {"mp3", "flac", "m4a", "aac", "ogg", "opus", "wav", "wma", "ape"};
static const QSet<QString> kSubExt = {"srt", "ass", "ssa", "vtt", "sub", "sup", "idx"};

MediaType mediaTypeOf(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (kVideoExt.contains(ext))
        return MediaType::Video;
    if (kImageExt.contains(ext))
        return MediaType::Image;
    if (kSubExt.contains(ext))
        return MediaType::Subtitle;
    return MediaType::None;
}

bool isAudioFile(const QString &path)
{
    return kAudioExt.contains(QFileInfo(path).suffix().toLower());
}

QStringList mediaNameFilters(MediaType type)
{
    const QSet<QString> &set = type == MediaType::Video ? kVideoExt
                             : type == MediaType::Image ? kImageExt
                                                        : kSubExt;
    QStringList out;
    for (const QString &e : set)
        out << "*." + e;
    out.sort();
    return out;
}

void FolderList::setCurrent(const QString &path)
{
    const QFileInfo fi(path);
    m_current = fi.absoluteFilePath();
    if (fi.absolutePath() != m_dir || !m_files.contains(m_current)) {
        m_dir = fi.absolutePath();
        rescan();
    }
}

void FolderList::rescan()
{
    m_files.clear();
    QDir dir(m_dir);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo &fi : entries) {
        const MediaType t = mediaTypeOf(fi.fileName());
        if (t == MediaType::Video || t == MediaType::Image)
            m_files << fi.absoluteFilePath();
    }
    applySort();
}

void FolderList::setSort(Sort sort)
{
    m_sort = sort;
    applySort();
}

void FolderList::applySort()
{
    if (m_sort == ByName) {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(m_files.begin(), m_files.end(), [&](const QString &a, const QString &b) {
            return c.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });
    } else if (m_sort == ByDate) {
        std::sort(m_files.begin(), m_files.end(), [](const QString &a, const QString &b) {
            return QFileInfo(a).lastModified() > QFileInfo(b).lastModified();
        });
    } else {
        std::sort(m_files.begin(), m_files.end(), [](const QString &a, const QString &b) {
            return QFileInfo(a).size() > QFileInfo(b).size();
        });
    }
}

QStringList FolderList::files(MediaType type) const
{
    QStringList out;
    for (const QString &f : m_files)
        if (mediaTypeOf(f) == type)
            out << f;
    return out;
}

int FolderList::indexOf(const QString &path, MediaType type) const
{
    return files(type).indexOf(QFileInfo(path).absoluteFilePath());
}

QString FolderList::step(const QString &from, int delta, MediaType type, bool wrap) const
{
    const QStringList list = files(type);
    if (list.isEmpty())
        return {};
    int i = list.indexOf(QFileInfo(from).absoluteFilePath());
    if (i < 0)
        return list.first();
    i += delta;
    if (wrap)
        i = ((i % list.size()) + list.size()) % list.size();
    if (i < 0 || i >= list.size())
        return {};
    return list.at(i);
}

QString FolderList::edge(MediaType type, bool last) const
{
    const QStringList list = files(type);
    if (list.isEmpty())
        return {};
    return last ? list.last() : list.first();
}

void FolderList::remove(const QString &path)
{
    m_files.removeAll(QFileInfo(path).absoluteFilePath());
}

void FolderList::rename(const QString &from, const QString &to)
{
    const int i = m_files.indexOf(QFileInfo(from).absoluteFilePath());
    if (i >= 0)
        m_files[i] = QFileInfo(to).absoluteFilePath();
    if (m_current == QFileInfo(from).absoluteFilePath())
        m_current = QFileInfo(to).absoluteFilePath();
    applySort();
}
