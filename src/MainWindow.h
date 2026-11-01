#pragma once

#include "FolderList.h"
#include "image/ImageLoader.h"
#include "stream/Playlists.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QVector>

class ControlBar;
class ExifPanel;
class ImageView;
class MpvWidget;
class QLabel;
class QMenu;
class QStackedWidget;
class QVBoxLayout;
class SubtitleManager;
class ThumbStrip;
class VideoPage;
class WelcomePage;
class YouTubePage;
class DownloadManager;
class PlaylistStore;
class StreamResolver;
struct PlaylistItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPath(const QString &path);
    void openYouTube(const QString &query = {}); // --youtube [query]
    void startDownload(const QString &url, const QString &kind); // --download url [video|mp3|m4a]
    void openPlaylists();                            // --playlists
    bool playSavedPlaylist(const QString &name);     // --playlist "name"

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void changeEvent(QEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    enum class Mode { Empty, Video, Image, YouTube };

    struct EditState
    {
        QImage image;
        bool lossy = false;
        int rotation = 0;
    };

    // navigation / modes
    void openVideo(const QString &path, bool saveState = true);
    void openImage(const QString &path);
    void showWelcome();
    void showYouTube();
    void leaveYouTube();
    bool hadVideo() const; // a video is loaded (possibly behind the YouTube page)
    void downloadCurrent(const QString &kind);
    void addRecent(const QString &path);
    QStringList recentFiles() const;
    void navigate(int delta);
    void navigateEdge(bool last);
    void openFileDialog();
    void openFolderDialog();
    void openUrlDialog();
    void openStream(const QString &url);
    void openSettings();

    // chrome
    void toggleFullscreen();
    bool overVideo(const QPoint &globalPos) const;
    void updateChrome();
    void revealChrome();
    void osd(const QString &text);
    void showToast(const QString &text); // OSD for pages without a video / image surface
    void setMediaTitle(const QString &title); // window title, kept for later while YouTube is shown
    QString folderVideo(int delta) const;  // next/previous video in the folder, always by name
    void userSeek(const QStringList &cmd);
    void playFromYouTube(const QString &urlOrPath);

    // video
    void videoKey(QKeyEvent *e);
    void togglePause();
    void applySubtitleLanguages();
    void onFileLoaded();
    void updateChapters();
    void onEof();
    void replay();
    void setRepeat(bool on);

    // playlist playback (videos in order)
    void playQueue(const QVector<PlaylistItem> &items, int start, const QString &name);
    void playQueueIndex(int index);
    bool queueActive() const { return m_queueIndex >= 0 && m_queueIndex < m_queue.size(); }
    void fillQueueMenu(QMenu *menu);
    QList<QPair<int, int>> streamQualities() const; // (height, max fps) offered by the current online video
    void fillQualityMenu(QMenu *menu);
    void setStreamQuality(const QString &quality);   // "best", "1080", "720", …, "audio"; reloads in place
    void updateQualityBadge();
    QString currentStreamFormat() const;
    void fillTrackMenu(QMenu *menu, const QString &type);
    void fillSubsMenu(QMenu *menu);
    void buildVideoMenu(QMenu *menu);
    QString trackLabel(const QString &type) const;
    void loadSubtitleFile();
    void openSubtitleSearch();
    void showVolumeOsd();

    // image
    void imageKey(QKeyEvent *e);
    void buildImageMenu(QMenu *menu);
    void onImageReady(const LoadedImage &img);
    void updateImageOverlay();
    void applyEdit(const QImage &img, bool lossy, int rotation);
    void undoEdit();
    void resetEditState();
    bool maybeSaveImage();
    bool saveImage();
    bool saveImageAs();
    void afterSave(const QString &path);
    void startCrop(double ratio);
    void applyCrop();
    void resizeImage();
    void renameFile();
    void deleteFile();
    void copyImage();
    void copyPath();
    void setWallpaper();
    void toggleSlideshow();
    void stopSlideshow();
    void slideshowStep();
    void setThumbsVisible(bool on);
    void setGrid(bool on);
    void setExifVisible(bool on);
    void setSort(int sort);

    Mode m_mode = Mode::Empty;
    QString m_current;
    FolderList m_list;

    QStackedWidget *m_stack;
    WelcomePage *m_welcome;
    YouTubePage *m_youtube;
    DownloadManager *m_downloads;
    PlaylistStore *m_playlists;
    StreamResolver *m_resolver;                       // resolves YouTube links ahead of Play
    QByteArray m_resolvedJson;                        // yt-dlp answer when we resolved it ourselves
    QList<QPair<QString, QString>> m_resolvedSubs;    // (language, url) to add once loaded
    QVector<PlaylistItem> m_queue;   // the playlist being played, in play order
    int m_queueIndex = -1;
    QString m_queueName;
    bool m_queueAdvancing = false;   // openVideo() keeps the queue while this is set
    bool m_queueEnded = false;       // end screen offers "Play list again"
    bool m_finishedCurrent = false;  // played to the end: don't save a resume position
    Mode m_youtubeReturn = Mode::Empty; // where Esc / Back leaves the YouTube page to
    QString m_titleBeforeYouTube;
    bool m_openedFromYouTube = false; // offer "Back to YouTube" on the end screen
    bool m_resumeAfterYouTube = false; // the video was playing when the YouTube page opened
    int m_queueFailures = 0;           // playlist items in a row that couldn't be played
    class QLabel *m_toast = nullptr;
    QTimer m_toastTimer;
    VideoPage *m_videoPage;
    MpvWidget *m_mpv;
    ControlBar *m_controls;
    SubtitleManager *m_subs;

    QWidget *m_imagePage;
    QVBoxLayout *m_imageColumn;
    ImageView *m_imageView;
    ThumbStrip *m_thumbs;
    ExifPanel *m_exif;
    ImageLoader *m_loader;
    LoadedImage m_info; // metadata of the shown image (pixels live in the view)

    QVector<EditState> m_undo;
    bool m_dirty = false;
    bool m_lossy = false;
    int m_rotation = 0;
    bool m_showOverlay = true;

    QTimer m_hideTimer;
    QTimer m_slideTimer;
    QTimer m_clickTimer;          // single click = pause, after the double-click interval
    bool m_swallowRelease = false;
    bool m_chromeShown = false;
    bool m_wasMaximized = false;
    bool m_slideshowFullscreen = false;
    bool m_ready = false; // constructor finished
    QPointer<QDialog> m_subPicker;
};
