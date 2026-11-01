#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <functional>

class QProcess;

struct YtVideo
{
    QString id;
    QString title;
    QString channel;
    QString url;
    double duration = 0; // seconds, 0 for live/unknown
    qint64 views = -1;
    bool live = false;
    QString thumbnailUrl() const { return QString("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id); }
};

// YouTube search through yt-dlp ("ytsearchN:"), no API key needed.
class YouTubeSearch : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(QVector<YtVideo> results, QString error)>;
    explicit YouTubeSearch(QObject *parent = nullptr);

    void search(const QString &query, int count, Callback cb); // cancels a running search
    // Videos of a YouTube playlist link, or the single video of a video link.
    void fetch(const QString &url, Callback cb);

private:
    void run(const QString &target, Callback cb);
    QPointer<QProcess> m_proc;
};

// Looks up view count / duration / channel / title for many videos at once:
// one yt-dlp process per batch (startup and session shared), a few batches in parallel,
// results reported per video as they arrive.
class VideoInfoFetcher : public QObject
{
    Q_OBJECT
public:
    explicit VideoInfoFetcher(QObject *parent = nullptr);
    ~VideoInfoFetcher() override;

    // onItem: called for each video found; onDone: after all batches finished (or were canceled).
    void fetch(const QStringList &urls, std::function<void(const YtVideo &)> onItem,
               std::function<void(int found, int total)> onDone, int parallel = 3);
    void cancel();
    bool busy() const { return !m_procs.isEmpty(); }

private:
    QList<QPointer<QProcess>> m_procs;
};

struct DownloadJob
{
    enum State { Queued, Running, Done, Failed, Canceled };
    int id = 0;
    QString url;
    QString title;
    QString kind; // "video", "mp3", "m4a"
    State state = Queued;
    double percent = 0;
    QString status; // "12.3 MiB/s · 00:41 left", "Converting…", error text
    QString file;   // final path once done
    QProcess *proc = nullptr;
};

// Runs yt-dlp downloads (at most two at a time) into a user-chosen folder.
class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    int add(const QString &url, const QString &title, const QString &kind);
    void cancel(int id);
    const DownloadJob *job(int id) const;
    QList<int> ids() const;

    static QString folder();
    static void setFolder(const QString &dir);
    static QString ffmpegDir(); // empty when ffmpeg isn't available (limits formats)

signals:
    void jobAdded(int id);
    void jobChanged(int id);

private:
    void pump();
    void start(DownloadJob &job);
    void onOutput(DownloadJob &job, const QString &line);

    QList<DownloadJob> m_jobs;
    int m_nextId = 1;
};
