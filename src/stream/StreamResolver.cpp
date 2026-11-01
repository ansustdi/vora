#include "StreamResolver.h"

#include "Settings.h"
#include "YtDlp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

static constexpr int kMaxRunning = 2;
static constexpr int kMaxQueued = 4;
static constexpr int kFreshSecs = 30 * 60; // stream URLs stay valid for hours; stay well inside

StreamResolver::StreamResolver(QObject *parent)
    : QObject(parent)
{
}

StreamResolver::~StreamResolver()
{
    for (const QPointer<QProcess> &p : m_running)
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
        }
}

const ResolvedStream *StreamResolver::ready(const QString &url, const QString &format) const
{
    const auto it = m_cache.constFind(key(url, format));
    if (it == m_cache.cend() || it->resolvedAt.secsTo(QDateTime::currentDateTime()) > kFreshSecs)
        return nullptr;
    return &it.value();
}

void StreamResolver::prefetch(const QString &url, const QString &format)
{
    if (url.isEmpty() || !YtDlp::needsResolver(url) || YtDlp::find().isEmpty())
        return;
    const QString k = key(url, format);
    if (ready(url, format) || m_running.contains(k))
        return;
    m_queue.removeAll(k);
    m_queue.prepend(k); // most recent interest first
    while (m_queue.size() > kMaxQueued)
        m_queue.removeLast();
    pump();
}

void StreamResolver::pump()
{
    while (m_running.size() < kMaxRunning && !m_queue.isEmpty()) {
        const QString k = m_queue.takeFirst();
        const QString url = k.section('\n', 0, 0);
        const QString format = k.section('\n', 1);

        const QString exe = YtDlp::find();
        if (exe.isEmpty())
            return;
        QStringList args = YtDlp::baseArgs();
        args << "-J" << "--no-playlist" << "-f" << format;
        // Subtitles in the user's languages (manual and auto-generated), like mpv's hook offers.
        args << "--write-subs" << "--write-auto-subs" << "--sub-format" << "vtt/srt/best"
             << "--sub-langs" << Settings::subLanguages().join(',');
        args << url;

        auto *proc = new QProcess(this);
        m_running.insert(k, proc);
        connect(proc, &QProcess::finished, this, [this, proc, k, url](int code, QProcess::ExitStatus) {
            m_running.remove(k);
            proc->deleteLater();
            const QJsonObject o = QJsonDocument::fromJson(proc->readAllStandardOutput()).object();
            if (code == 0 && !o.isEmpty()) {
                ResolvedStream r;
                r.title = o.value("title").toString();
                r.live = o.value("is_live").toBool();
                r.json = QJsonDocument(o).toJson(QJsonDocument::Compact);
                const QJsonArray parts = o.value("requested_formats").toArray();
                QJsonObject headers;
                if (!parts.isEmpty()) {
                    for (const QJsonValue &v : parts)
                        r.urls << v.toObject().value("url").toString();
                    headers = parts.first().toObject().value("http_headers").toObject();
                } else {
                    r.urls << o.value("url").toString();
                    headers = o.value("http_headers").toObject();
                }
                r.userAgent = headers.value("User-Agent").toString();
                for (auto h = headers.begin(); h != headers.end(); ++h)
                    if (h.key() != "User-Agent" && h.key() != "Accept" && h.key() != "Accept-Language"
                        && h.key() != "Sec-Fetch-Mode")
                        r.headers << h.key() + ": " + h.value().toString();
                const QJsonObject subs = o.value("requested_subtitles").toObject();
                for (auto it = subs.begin(); it != subs.end(); ++it) {
                    const QString subUrl = it.value().toObject().value("url").toString();
                    if (!subUrl.isEmpty() && it.key() != "live_chat")
                        r.subtitles << qMakePair(it.key(), subUrl);
                }
                r.urls.removeAll(QString());
                r.resolvedAt = QDateTime::currentDateTime();
                if (!r.urls.isEmpty()) {
                    m_cache.insert(k, r);
                    emit resolved(url);
                }
            }
            pump();
        });
        YtDlp::finishOnStartFailure(proc); // a blocked yt-dlp must not hold a slot forever
        proc->start(exe, args);
    }
}

QString StreamResolver::edl(const ResolvedStream &r)
{
    if (r.urls.size() == 1)
        return r.urls.first(); // muxed file or live manifest: play it directly
    // %len% prefixes keep URLs with ';' or ',' intact. (Plain concatenation: QString::arg()
    // would treat "%" + digits inside the result as further placeholders.)
    QString out = "edl://";
    for (const QString &u : r.urls)
        out += "!new_stream;!no_clip;!no_chapters;%" + QString::number(u.toUtf8().size()) + "%" + u + ";";
    out.chop(1);
    return out;
}
