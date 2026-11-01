#include "YtDlp.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace YtDlp {

#if defined(Q_OS_WIN)
static const char *kExe = "yt-dlp.exe";
static const char *kAsset = "yt-dlp.exe";
#elif defined(Q_OS_MACOS)
static const char *kExe = "yt-dlp";
static const char *kAsset = "yt-dlp_macos";
#else
static const char *kExe = "yt-dlp";
static const char *kAsset = "yt-dlp_linux";
#endif

static QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString managedPath()
{
    return dataDir() + "/" + kExe;
}

QString managedDirExe()
{
    return dataDir() + "/yt-dlp/" + kExe;
}

// Written after each successful folder-build download; its age drives the weekly refresh.
static QString stampPath()
{
    return dataDir() + "/yt-dlp/.downloaded";
}

static void writeStamp()
{
    QFile f(stampPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
}

#ifdef Q_OS_WIN
// Windows' own bsdtar (Windows 10+) reads zip files; a GNU tar earlier on PATH (Git, MSYS) can't.
static QString systemTar()
{
    return qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\tar.exe";
}

// Unpacks yt-dlp_win.zip with Windows' own tar.exe into `dir`.
static bool unzipTo(const QString &zip, const QString &dir)
{
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    QProcess tar;
    tar.start(systemTar(), {"-xf", QDir::toNativeSeparators(zip), "-C", QDir::toNativeSeparators(dir)});
    return tar.waitForFinished(120000) && tar.exitCode() == 0 && QFileInfo::exists(dir + "/" + kExe);
}

// Replaces <data>/yt-dlp with a freshly unpacked folder. Only called when no yt-dlp of ours can
// be running (first download, or at startup before anything starts) — deleting files under a
// running yt-dlp would crash it. The previous folder is kept as .old and removed next start.
static bool swapInFolder(const QString &fresh)
{
    const QString live = dataDir() + "/yt-dlp";
    const QString old = dataDir() + "/yt-dlp.old";
    QDir(old).removeRecursively();
    if (QFileInfo::exists(live) && !QDir().rename(live, old))
        return false;
    if (!QDir().rename(fresh, live)) {
        QDir().rename(old, live);
        return false;
    }
    return true;
}
#endif

// Sites (YouTube especially) change constantly; a yt-dlp older than this fails in odd ways
// (e.g. YouTube serving only the first MBs, then 403). yt-dlp itself warns at 90 days.
static constexpr int kStaleDays = 90;

QString find()
{
    const QString local = QCoreApplication::applicationDirPath() + "/" + kExe;
    if (QFileInfo::exists(local))
        return local;
    if (QFileInfo::exists(managedDirExe()))
        return managedDirExe(); // unpacked folder build: no self-extraction on every run
    if (QFileInfo::exists(managedPath()))
        return managedPath(); // kept fresh by updateInBackground()
    // A copy on PATH (pip, package manager) is used only while recent; otherwise we offer our own.
    const QString onPath = QStandardPaths::findExecutable("yt-dlp");
    if (!onPath.isEmpty() && QFileInfo(onPath).lastModified().daysTo(QDateTime::currentDateTime()) < kStaleDays)
        return onPath;
    return {};
}

QString rawOptions()
{
    // YouTube needs a JavaScript runtime for its challenges. yt-dlp only looks for Deno by
    // default; point it at Node.js or Bun when that is what's installed.
    if (!QStandardPaths::findExecutable("deno").isEmpty())
        return {};
    if (!QStandardPaths::findExecutable("node").isEmpty())
        return "js-runtimes=node";
    if (!QStandardPaths::findExecutable("bun").isEmpty())
        return "js-runtimes=bun";
    return {};
}

QString pathForMpv()
{
    const QString p = find();
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(p.isEmpty() ? managedDirExe() : p); // where download() will put it
#else
    return QDir::toNativeSeparators(p.isEmpty() ? managedPath() : p);
#endif
}

bool needsResolver(const QString &url)
{
    const QUrl u(url);
    const QString scheme = u.scheme().toLower();
    if (scheme != "http" && scheme != "https")
        return false; // rtsp, rtmp, udp, srt, ftp, smb...: mpv/ffmpeg open these directly
    static const QSet<QString> direct = {"mp4", "mkv", "webm", "mov", "avi", "ts", "m3u8", "m3u", "mpd", "mp3",
                                         "flac", "aac", "m4a", "ogg", "opus", "wav", "flv"};
    const QString ext = QFileInfo(u.path()).suffix().toLower();
    return !direct.contains(ext);
}

static void download(QWidget *parent, std::function<void(bool)> cb)
{
#ifdef Q_OS_WIN
    const QString target = dataDir() + "/yt-dlp_win.zip"; // unpacked into <data>/yt-dlp below
    const QString asset = "yt-dlp_win.zip";
#else
    const QString target = managedPath();
    const QString asset = kAsset;
#endif
    QDir().mkpath(QFileInfo(target).absolutePath());

    auto *nam = new QNetworkAccessManager(parent);
    QNetworkRequest req(QUrl(QString("https://github.com/yt-dlp/yt-dlp/releases/latest/download/%1").arg(asset)));
    req.setRawHeader("User-Agent", "Vora");
    QNetworkReply *reply = nam->get(req); // Qt follows GitHub's redirect to the release asset

    auto *progress = new QProgressDialog(QCoreApplication::translate("YtDlp", "Downloading yt-dlp…"),
                                         QCoreApplication::translate("YtDlp", "Cancel"), 0, 0, parent);
    progress->setWindowTitle("Vora");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 got, qint64 total) {
        if (total > 0) {
            progress->setMaximum(int(total / 1024));
            progress->setValue(int(got / 1024));
        }
    });
    QObject::connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, parent, [=] {
        reply->deleteLater();
        nam->deleteLater();
        progress->close();
        const QByteArray data = reply->readAll();
        // A real binary is several MB; anything tiny is an error page.
        if (reply->error() != QNetworkReply::NoError || data.size() < 1024 * 1024) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::warning(parent, "Vora",
                                     QCoreApplication::translate("YtDlp", "Could not download yt-dlp: %1")
                                         .arg(reply->errorString()));
            cb(false);
            return;
        }
        QSaveFile f(target);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            QMessageBox::warning(parent, "Vora", QCoreApplication::translate("YtDlp", "Could not save %1").arg(target));
            cb(false);
            return;
        }
#ifdef Q_OS_WIN
        const QString fresh = dataDir() + "/yt-dlp.new";
        if (!unzipTo(target, fresh) || !swapInFolder(fresh)) {
            QMessageBox::warning(parent, "Vora", QCoreApplication::translate("YtDlp", "Could not unpack yt-dlp"));
            cb(false);
            return;
        }
        QFile::remove(target);
        writeStamp();
#else
        QFile::setPermissions(target, QFile::permissions(target) | QFile::ExeOwner | QFile::ExeUser);
#endif
        cb(true);
    });
}

void ensure(QWidget *parent, std::function<void(bool)> cb)
{
    if (!find().isEmpty()) {
        cb(true);
        return;
    }
    const auto answer = QMessageBox::question(
        parent, QCoreApplication::translate("YtDlp", "Enable streaming sites"),
        QCoreApplication::translate("YtDlp",
            "Playing YouTube, Twitch, Kick and similar sites needs the free yt-dlp tool "
            "(about 23 MB, from github.com/yt-dlp).\n\nDownload it now?"));
    if (answer != QMessageBox::Yes) {
        cb(false);
        return;
    }
    download(parent, cb);
}

void updateInBackground()
{
#ifdef Q_OS_WIN
    // 1) A refresh unpacked by the previous session is swapped in now, before any yt-dlp starts.
    const QString staged = dataDir() + "/yt-dlp.new";
    QDir(dataDir() + "/yt-dlp.old").removeRecursively();
    if (QFileInfo::exists(staged + "/.complete") && QFileInfo::exists(staged + "/" + kExe)) {
        QFile::remove(staged + "/.complete");
        if (swapInFolder(staged))
            writeStamp();
    }
    if (QFileInfo::exists(managedDirExe())) {
        QFile stamp(stampPath());
        QDateTime last;
        if (stamp.open(QIODevice::ReadOnly))
            last = QDateTime::fromString(QString::fromUtf8(stamp.readAll()).trimmed(), Qt::ISODate);
        if (last.isValid() && last.daysTo(QDateTime::currentDateTimeUtc()) < 7)
            return;
        // 2) Weekly: fetch the latest folder build and unpack it next to the live one (async, no
        //    UI freeze). It is swapped in at the next start.
        auto *nam = new QNetworkAccessManager(qApp);
        QNetworkRequest req(QUrl("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_win.zip"));
        req.setRawHeader("User-Agent", "Vora");
        req.setTransferTimeout(120000);
        QNetworkReply *reply = nam->get(req);
        QObject::connect(reply, &QNetworkReply::finished, qApp, [reply, nam, staged] {
            reply->deleteLater();
            nam->deleteLater();
            const QByteArray data = reply->readAll();
            if (reply->error() != QNetworkReply::NoError || data.size() < 1024 * 1024)
                return;
            const QString zip = dataDir() + "/yt-dlp_win.zip";
            QSaveFile f(zip);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
                return;
            QDir(staged).removeRecursively();
            QDir().mkpath(staged);
            auto *tar = new QProcess(qApp);
            QObject::connect(tar, &QProcess::finished, qApp, [tar, zip, staged](int code, QProcess::ExitStatus) {
                tar->deleteLater();
                QFile::remove(zip);
                if (code == 0 && QFileInfo::exists(staged + "/" + kExe)) {
                    QFile done(staged + "/.complete");
                    if (done.open(QIODevice::WriteOnly))
                        done.close();
                }
            });
            finishOnStartFailure(tar);
            tar->start(systemTar(), {"-xf", QDir::toNativeSeparators(zip), "-C", QDir::toNativeSeparators(staged)});
        });
        return;
    }
#endif
    const QFileInfo fi(managedPath());
    if (!fi.exists() || fi.lastModified().daysTo(QDateTime::currentDateTime()) < 7)
        return;
    QProcess::startDetached(fi.absoluteFilePath(), {"-U"});
}

QStringList baseArgs()
{
    QStringList args{"--no-warnings", "--encoding", "utf-8"};
    const QString raw = rawOptions(); // "js-runtimes=node"
    if (!raw.isEmpty())
        args << "--" + raw.section('=', 0, 0) << raw.section('=', 1);
    return args;
}

void finishOnStartFailure(QProcess *proc)
{
    QObject::connect(proc, &QProcess::errorOccurred, proc, [proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit proc->finished(-1, QProcess::CrashExit);
    });
}

} // namespace YtDlp
