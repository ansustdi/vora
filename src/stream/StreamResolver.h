#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QProcess;

struct ResolvedStream
{
    QStringList urls;      // media URLs (video + audio, or one muxed / live manifest)
    QString title;
    QString userAgent;     // googlevideo expects the same browser identity yt-dlp used
    QStringList headers;   // other HTTP headers yt-dlp says the site needs ("Referer: …")
    QByteArray json;       // yt-dlp's full answer (formats list for the quality menu)
    QList<QPair<QString, QString>> subtitles; // (language, url) in the user's languages
    bool live = false;
    QDateTime resolvedAt;
};

// Resolves YouTube / Twitch / Kick page URLs with yt-dlp *ahead of time* (hovered results,
// the next playlist video), so pressing Play can start the stream almost instantly.
class StreamResolver : public QObject
{
    Q_OBJECT
public:
    explicit StreamResolver(QObject *parent = nullptr);
    ~StreamResolver() override;

    void prefetch(const QString &url, const QString &format);
    // A fresh result (resolved within the last 30 minutes), or nullptr.
    const ResolvedStream *ready(const QString &url, const QString &format) const;

    // mpv EDL that plays all streams of `r` together (what mpv's ytdl hook builds itself).
    static QString edl(const ResolvedStream &r);

signals:
    void resolved(const QString &url);

private:
    void pump();
    static QString key(const QString &url, const QString &format) { return url + "\n" + format; }

    QHash<QString, ResolvedStream> m_cache;
    QStringList m_queue;               // keys waiting, newest first
    QHash<QString, QPointer<QProcess>> m_running;
};
