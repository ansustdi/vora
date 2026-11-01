#pragma once

#include <QStringList>

enum class MediaType { None, Video, Image, Subtitle };

MediaType mediaTypeOf(const QString &path);
bool isAudioFile(const QString &path);
QStringList mediaNameFilters(MediaType type);

// Media files of the current file's folder, naturally sorted.
class FolderList
{
public:
    enum Sort { ByName = 0, ByDate = 1, BySize = 2 };

    void setCurrent(const QString &path);
    void rescan();
    void setSort(Sort sort);
    Sort sort() const { return m_sort; }

    QString current() const { return m_current; }
    const QStringList &files() const { return m_files; }
    QStringList files(MediaType type) const;
    int indexOf(const QString &path, MediaType type) const;

    // Neighbour of `from` of the same media type; empty if none.
    QString step(const QString &from, int delta, MediaType type, bool wrap) const;
    QString edge(MediaType type, bool last) const;

    void remove(const QString &path);
    void rename(const QString &from, const QString &to);

private:
    void applySort();

    QString m_dir;
    QString m_current;
    QStringList m_files;
    Sort m_sort = ByName;
};
