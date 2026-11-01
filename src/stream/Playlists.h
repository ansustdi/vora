#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct PlaylistItem
{
    QString url;
    QString title;
    QString channel;
    QString id;        // YouTube video id (thumbnail), may be empty for other sites
    double duration = 0;
    qint64 views = -1;  // -1 = unknown
    qint64 added = 0;   // ms since epoch, when it was saved to the list
};

struct Playlist
{
    QString name;
    QVector<PlaylistItem> items;
};

// User playlists ("Watch later" + own lists), saved as JSON in the app data folder.
class PlaylistStore : public QObject
{
    Q_OBJECT
public:
    explicit PlaylistStore(QObject *parent = nullptr);

    const QVector<Playlist> &lists() const { return m_lists; }
    int count() const { return int(m_lists.size()); }
    const Playlist &at(int index) const { return m_lists.at(index); }

    int create(const QString &name);              // returns index
    void rename(int index, const QString &name);
    void remove(int index);
    bool addItem(int list, const PlaylistItem &item); // false if already in the list
    void addItems(int list, const QVector<PlaylistItem> &items);
    void removeItem(int list, int item);
    void moveItem(int list, int from, int to);
    void updateTitle(const QString &url, const QString &title); // fill in titles learned while playing
    // Fill in details fetched later (views, duration, channel, title) for every copy of this video.
    void updateInfo(const PlaylistItem &info);
    void updateInfos(const QVector<PlaylistItem> &infos); // many at once: one save, one changed()
    int indexOf(const QString &name) const;               // -1 if no playlist has that name

    // Reorders the list for good, so Play all follows the new order.
    // mode: views_desc, views_asc, title, duration_desc, duration_asc, added_desc, added_asc
    void sortList(int list, const QString &mode);

    static QString youtubeId(const QString &url);

signals:
    void changed();

private:
    bool load(); // false if the file exists but couldn't be read/parsed
    void save();
    QString path() const;

    QVector<Playlist> m_lists;
    bool m_loadFailed = false; // keep a backup before the first save then
};
