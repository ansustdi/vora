#pragma once

#include "Playlists.h"
#include "YouTube.h"

#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

class DownloadManager;
class PlaylistStore;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class ThumbView;
class DownloadRow;

// YouTube browser: search, results with thumbnails, play in the player, save to playlists,
// download as MP4 / MP3, and a Playlists tab to manage and play lists in order.
class YouTubePage : public QWidget
{
    Q_OBJECT
public:
    YouTubePage(DownloadManager *downloads, PlaylistStore *playlists, QWidget *parent = nullptr);

    void focusSearch();
    void search(const QString &query); // fill the box and run it
    void setCanGoBack(bool canGoBack);
    void showPlaylists(int list = -1);

    // Menu of playlists + "New playlist…"; adds `item` to the chosen one.
    static void saveToPlaylistMenu(QWidget *parent, PlaylistStore *store, const PlaylistItem &item, const QPoint &globalPos);

signals:
    void playRequested(const QString &urlOrPath, const QString &title);
    void playQueueRequested(const QVector<PlaylistItem> &items, int start, const QString &listName);
    void backRequested();
    void prefetchRequested(const QString &url); // likely to be played soon: resolve it now

protected:
    void paintEvent(QPaintEvent *) override;

private:
    // search
    void runSearch(bool more = false);
    void showResults(const QVector<YtVideo> &results);
    void showState(const QString &icon, const QString &title, const QString &subtitle);
    void download(const YtVideo &video, const QString &kind);
    void setTab(bool playlists);

    // thumbnails (shared by results and playlists)
    void thumbnailInto(const QString &id, ThumbView *view);

    // playlists tab
    QWidget *buildPlaylistsPage();
    void refreshLists();
    void refreshItems();
    int currentList() const;
    void newPlaylist();
    void renamePlaylist();
    void deletePlaylist();
    void addLinkToPlaylist();
    void playList(bool shuffle, int start = 0);
    void sortCurrentList(const QString &mode);

    // downloads panel
    void onJobAdded(int id);
    void onJobChanged(int id);
    void updateDownloadsHeader();
    void chooseFolder();

    DownloadManager *m_downloads;
    PlaylistStore *m_store;
    YouTubeSearch m_search;
    YouTubeSearch m_fetcher;  // playlist imports, independent of searching
    VideoInfoFetcher m_info;  // view counts etc. for videos saved without them
    QNetworkAccessManager *m_nam;
    QHash<QString, QPixmap> m_thumbs;                       // video id -> thumbnail
    QHash<QString, QList<QPointer<ThumbView>>> m_waiting;   // views waiting for a thumbnail

    QPushButton *m_back;
    QPushButton *m_tabSearch;
    QPushButton *m_tabLists;
    QLineEdit *m_query;
    QPushButton *m_searchButton;
    QComboBox *m_quality;
    QPushButton *m_downloadsToggle;
    QLabel *m_status;

    QStackedWidget *m_body;
    QWidget *m_statePage;
    QWidget *m_resultsPage;
    QWidget *m_playlistsPage;
    QLabel *m_stateIcon;
    QLabel *m_stateTitle;
    QLabel *m_stateSubtitle;
    QListWidget *m_list;
    QPushButton *m_more;

    // playlists tab widgets
    QListWidget *m_listsView;
    QListWidget *m_itemsView;
    QLabel *m_listTitle;
    QLabel *m_listInfo;
    QPushButton *m_playAll;
    QPushButton *m_shuffle;
    QPushButton *m_sort;
    QStackedWidget *m_itemsStack;
    QLabel *m_itemsEmpty;
    bool m_refreshing = false;
    bool m_fetchingViews = false; // Sort stays disabled while view counts are looked up
    bool m_importing = false;

    QWidget *m_panel;
    QLabel *m_panelFolder;
    QVBoxLayout *m_panelRows;
    QLabel *m_panelEmpty;
    QHash<int, DownloadRow *> m_rows;

    QString m_lastQuery;
    int m_count = 20;
};
