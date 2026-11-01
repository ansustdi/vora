#include "YouTube.h"

#include "Settings.h"
#include "YtDlp.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

static constexpr int kMaxParallel = 2;

// yt-dlp options shared by search and downloads (JS runtime for YouTube challenges).
static QStringList commonArgs()
{
    return YtDlp::baseArgs();
}

// ======================================================================== search

YouTubeSearch::YouTubeSearch(QObject *parent)
    : QObject(parent)
{
}

void YouTubeSearch::search(const QString &query, int count, Callback cb)
{
    run(QString("ytsearch%1:%2").arg(count).arg(query), cb);
}

void YouTubeSearch::fetch(const QString &url, Callback cb)
{
    run(url, cb);
}

static YtVideo videoFromJson(const QJsonObject &e)
{
    YtVideo y;
    y.id = e.value("id").toString();
    y.title = e.value("title").toString();
    y.channel = e.value("channel").toString(e.value("uploader").toString());
    y.url = e.value("webpage_url").toString(e.value("url").toString());
    if (!y.url.startsWith("http"))
        y.url = "https://www.youtube.com/watch?v=" + y.id;
    y.duration = e.value("duration").toDouble();
    y.views = e.contains("view_count") && !e.value("view_count").isNull() ? qint64(e.value("view_count").toDouble()) : -1;
    y.live = e.value("live_status").toString() == "is_live";
    return y;
}

void YouTubeSearch::run(const QString &target, Callback cb)
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->deleteLater();
    }
    const QString exe = YtDlp::find();
    if (exe.isEmpty()) {
        cb({}, tr("yt-dlp is not installed"));
        return;
    }
    auto *proc = new QProcess(this);
    m_proc = proc;
    QStringList args = commonArgs();
    args << "--flat-playlist" << "-J" << target;

    connect(proc, &QProcess::finished, this, [this, proc, cb](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        if (m_proc == proc)
            m_proc = nullptr;
        const QByteArray out = proc->readAllStandardOutput();
        if (code != 0 || out.isEmpty()) {
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed().section('\n', -1);
            cb({}, err.isEmpty() ? tr("Search failed") : err);
            return;
        }
        QVector<YtVideo> results;
        const QJsonObject root = QJsonDocument::fromJson(out).object();
        if (!root.contains("entries")) { // a single video link
            const YtVideo y = videoFromJson(root);
            if (!y.id.isEmpty())
                results << y;
            cb(results, {});
            return;
        }
        for (const QJsonValue &v : root.value("entries").toArray()) {
            const QJsonObject e = v.toObject();
            const YtVideo y = videoFromJson(e);
            if (!y.id.isEmpty() && e.value("ie_key").toString() != "YoutubeTab") // skip channels/playlists
                results << y;
        }
        cb(results, {});
    });
    YtDlp::finishOnStartFailure(proc);
    proc->start(exe, args);
}

// ======================================================================== info lookup

VideoInfoFetcher::VideoInfoFetcher(QObject *parent)
    : QObject(parent)
{
}

VideoInfoFetcher::~VideoInfoFetcher()
{
    cancel();
}

void VideoInfoFetcher::cancel()
{
    for (const QPointer<QProcess> &p : m_procs)
        if (p) {
            p->disconnect(this);
            p->kill();
            p->deleteLater();
        }
    m_procs.clear();
}

void VideoInfoFetcher::fetch(const QStringList &urls, std::function<void(const YtVideo &)> onItem,
                             std::function<void(int, int)> onDone, int parallel)
{
    cancel();
    const QString exe = YtDlp::find();
    if (exe.isEmpty() || urls.isEmpty()) {
        onDone(0, int(urls.size()));
        return;
    }
    // Round-robin into batches so every process gets a similar share.
    const int n = qBound(1, parallel, int(urls.size()));
    QVector<QStringList> batches(n);
    for (int i = 0; i < urls.size(); ++i)
        batches[i % n] << urls.at(i);

    struct State
    {
        int running = 0;
        int found = 0;
    };
    auto state = std::make_shared<State>();
    const int total = int(urls.size());
    for (const QStringList &batch : batches) {
        auto *proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        m_procs << proc;
        ++state->running;
        QStringList args = commonArgs();
        // Only metadata: no download, one tab-separated line per video.
        args << "--skip-download" << "--no-playlist" << "--ignore-errors"
             << "--print" << "LPINFO\t%(id)s\t%(view_count)s\t%(duration)s\t%(channel)s\t%(webpage_url)s\t%(title)s";
        args << batch;
        connect(proc, &QProcess::readyRead, this, [proc, onItem, state] {
            while (proc->canReadLine()) {
                const QString line = QString::fromUtf8(proc->readLine()).trimmed();
                if (!line.startsWith("LPINFO\t"))
                    continue;
                const QStringList f = line.split('\t');
                if (f.size() < 7)
                    continue;
                YtVideo v;
                v.id = f.at(1);
                v.views = f.at(2) == "NA" ? -1 : f.at(2).toLongLong();
                v.duration = f.at(3) == "NA" ? 0 : f.at(3).toDouble();
                v.channel = f.at(4) == "NA" ? QString() : f.at(4);
                v.url = f.at(5);
                v.title = f.mid(6).join('\t');
                ++state->found;
                onItem(v);
            }
        });
        connect(proc, &QProcess::finished, this, [this, proc, onDone, state, total] {
            m_procs.removeAll(proc);
            proc->deleteLater();
            if (--state->running == 0)
                onDone(state->found, total);
        });
        YtDlp::finishOnStartFailure(proc);
        proc->start(exe, args);
    }
}

// ======================================================================== downloads

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
{
}

DownloadManager::~DownloadManager()
{
    for (DownloadJob &j : m_jobs)
        if (j.proc) {
            j.proc->disconnect(this);
            j.proc->kill();
            j.proc->waitForFinished(2000);
        }
}

QString DownloadManager::folder()
{
    return Settings::value("download/dir", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
}

void DownloadManager::setFolder(const QString &dir)
{
    Settings::setValue("download/dir", dir);
}

QString DownloadManager::ffmpegDir()
{
#ifdef Q_OS_WIN
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg";
#endif
    if (QFileInfo::exists(local))
        return QCoreApplication::applicationDirPath();
    const QString onPath = QStandardPaths::findExecutable("ffmpeg");
    return onPath.isEmpty() ? QString() : QFileInfo(onPath).absolutePath();
}

int DownloadManager::add(const QString &url, const QString &title, const QString &kind)
{
    // The same video in the same format twice would write the same .part file and corrupt it.
    for (const DownloadJob &existing : m_jobs)
        if (existing.url == url && existing.kind == kind
            && (existing.state == DownloadJob::Queued || existing.state == DownloadJob::Running))
            return existing.id;
    DownloadJob j;
    j.id = m_nextId++;
    j.url = url;
    j.title = title.isEmpty() ? url : title;
    j.kind = kind;
    j.status = tr("Waiting…");
    m_jobs << j;
    emit jobAdded(j.id);
    pump();
    return j.id;
}

const DownloadJob *DownloadManager::job(int id) const
{
    for (const DownloadJob &j : m_jobs)
        if (j.id == id)
            return &j;
    return nullptr;
}

QList<int> DownloadManager::ids() const
{
    QList<int> out;
    for (const DownloadJob &j : m_jobs)
        out << j.id;
    return out;
}

void DownloadManager::cancel(int id)
{
    for (DownloadJob &j : m_jobs) {
        if (j.id != id)
            continue;
        if (j.state == DownloadJob::Running && j.proc) {
            j.state = DownloadJob::Canceled;
            j.proc->kill(); // finished handler cleans up
        } else if (j.state == DownloadJob::Queued) {
            j.state = DownloadJob::Canceled;
        }
        j.status = tr("Canceled");
        emit jobChanged(id);
    }
    pump();
}

void DownloadManager::pump()
{
    int running = 0;
    for (const DownloadJob &j : m_jobs)
        running += j.state == DownloadJob::Running;
    for (DownloadJob &j : m_jobs) {
        if (running >= kMaxParallel)
            break;
        if (j.state == DownloadJob::Queued) {
            start(j);
            ++running;
        }
    }
}

void DownloadManager::start(DownloadJob &job)
{
    const QString exe = YtDlp::find();
    if (exe.isEmpty()) {
        job.state = DownloadJob::Failed;
        job.status = tr("yt-dlp is not installed");
        emit jobChanged(job.id);
        return;
    }
    QDir().mkpath(folder());
    const QString ffmpeg = ffmpegDir();
    QString cap = Settings::value("stream/quality", "1080").toString();
    if (cap == "audio" || cap == "best")
        cap = cap == "best" ? QString() : QString("1080");
    const QString h = cap.isEmpty() ? QString() : QString("[height<=?%1]").arg(cap);

    QStringList args = commonArgs();
    args << "--newline" << "--no-playlist" << "--no-simulate" << "--progress"
         << "-P" << QDir::toNativeSeparators(folder())
         << "-o" << "%(title).150B [%(id)s].%(ext)s"
         << "--progress-template" << "download:LP %(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s"
         << "--progress-template" << "postprocess:LPPOST %(progress.postprocessor)s"
         << "--print" << "video:LPTITLE %(title)s"
         << "--print" << "after_move:LPFILE %(filepath)s";
    if (!ffmpeg.isEmpty())
        args << "--ffmpeg-location" << QDir::toNativeSeparators(ffmpeg);

    if (job.kind == "mp3" && !ffmpeg.isEmpty()) {
        args << "-f" << "ba/b" << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "0" << "--embed-metadata";
    } else if (job.kind == "mp3" || job.kind == "m4a") {
        args << "-f" << "ba[ext=m4a]/ba/b"; // no conversion needed
    } else if (!ffmpeg.isEmpty()) {
        // Best video+audio up to the chosen height, merged into an MP4 that plays everywhere:
        // prefer H.264 (YouTube offers it up to 1080p) over AV1/VP9, which many devices can't decode.
        args << "-f"
             << QString("bv*%1[vcodec^=avc1]+ba[ext=m4a]/bv*%1[ext=mp4]+ba[ext=m4a]/bv*%1+ba/b%1/b").arg(h)
             << "--merge-output-format" << "mp4" << "--embed-metadata";
    } else {
        args << "-f" << QString("b%1[ext=mp4]/b").arg(h); // single-file formats only (no merging without ffmpeg)
    }
    args << job.url;

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    job.proc = proc;
    job.state = DownloadJob::Running;
    job.status = tr("Starting…");
    const int id = job.id;
    connect(proc, &QProcess::readyRead, this, [this, id, proc] {
        while (proc->canReadLine()) {
            const QString line = QString::fromUtf8(proc->readLine()).trimmed();
            for (DownloadJob &j : m_jobs)
                if (j.id == id)
                    onOutput(j, line);
        }
    });
    connect(proc, &QProcess::finished, this, [this, id, proc](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        for (DownloadJob &j : m_jobs) {
            if (j.id != id)
                continue;
            j.proc = nullptr;
            if (j.state == DownloadJob::Canceled) {
                j.status = tr("Canceled");
            } else if (code == 0 && !j.file.isEmpty()) {
                j.state = DownloadJob::Done;
                j.percent = 100;
                j.status = tr("Saved · %1").arg(QFileInfo(j.file).fileName());
            } else {
                j.state = DownloadJob::Failed;
                if (code == -1)
                    j.status = tr("Could not start yt-dlp (blocked by antivirus?)");
                else if (j.status.isEmpty() || !j.status.startsWith("ERROR"))
                    j.status = tr("Download failed");
            }
            emit jobChanged(id);
        }
        pump();
    });
    emit jobChanged(id);
    YtDlp::finishOnStartFailure(proc);
    proc->start(exe, args);
}

void DownloadManager::onOutput(DownloadJob &job, const QString &line)
{
    if (line.startsWith("LPTITLE ")) {
        job.title = line.mid(8).trimmed(); // real title once yt-dlp has resolved the page
    } else if (line.startsWith("LPFILE ")) {
        job.file = line.mid(7).trimmed();
    } else if (line.startsWith("LP ")) {
        const QStringList p = line.mid(3).split('|');
        job.percent = QString(p.value(0)).remove('%').trimmed().toDouble();
        const QString speed = p.value(1).trimmed(), eta = p.value(2).trimmed();
        job.status = (speed.contains("Unknown") ? QString() : speed) + (eta.isEmpty() || eta.contains("Unknown") ? QString() : "  ·  " + eta + tr(" left"));
    } else if (line.startsWith("LPPOST") || line.startsWith("[Merger]") || line.startsWith("[ExtractAudio]")
               || line.startsWith("[Metadata]")) {
        job.status = tr("Finishing…");
    } else if (line.startsWith("ERROR")) {
        job.status = line.section(':', 2).trimmed().isEmpty() ? line : "ERROR: " + line.section(':', 2).trimmed();
    } else {
        return;
    }
    emit jobChanged(job.id);
}
