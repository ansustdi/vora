#include "OpenSubtitlesClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>

static const char *kUserAgent = "Vora v" VORA_VERSION;

// API wants lowercase, alphabetically ordered params, '+' for spaces (otherwise it redirects).
static QString encodeParam(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value.toLower())).replace("%20", "+");
}

OpenSubtitlesClient::OpenSubtitlesClient(QObject *parent)
    : QObject(parent)
{
    m_nam.setTransferTimeout(20000); // a stalled request reports an error instead of hanging
}

void OpenSubtitlesClient::setCredentials(const QString &apiKey, const QString &user, const QString &password)
{
    if (apiKey != m_apiKey || user != m_user || password != m_password) {
        m_token.clear();
        m_host = "api.opensubtitles.com";
        m_loginFailed = false;
    }
    m_apiKey = apiKey.trimmed();
    if (m_apiKey.isEmpty())
        m_apiKey = QStringLiteral(VORA_OS_KEY); // optional key baked in at build time (-DVORA_OS_KEY=...)
    m_user = user.trimmed();
    m_password = password;
}

QNetworkRequest OpenSubtitlesClient::makeRequest(const QString &path, const QString &query) const
{
    QUrl url(QString("https://%1/api/v1%2").arg(m_host, path));
    if (!query.isEmpty())
        url.setQuery(query, QUrl::TolerantMode);
    QNetworkRequest req(url);
    req.setRawHeader("Api-Key", m_apiKey.toUtf8());
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    return req;
}

QString OpenSubtitlesClient::errorText(QNetworkReply *reply, const QByteArray &body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString msg = o.value("message").toString();
    if (msg.isEmpty()) {
        const QJsonArray errs = o.value("errors").toArray();
        if (!errs.isEmpty())
            msg = errs.first().toString();
    }
    if (msg.isEmpty())
        msg = reply->errorString();
    return status ? QString("HTTP %1: %2").arg(status).arg(msg) : msg;
}

void OpenSubtitlesClient::ensureLogin(std::function<void(QString)> cb)
{
    if (m_user.isEmpty() || m_password.isEmpty() || !m_token.isEmpty() || m_loginFailed) {
        cb({});
        return;
    }
    const QJsonObject body{{"username", m_user}, {"password", m_password}};
    QNetworkReply *reply = m_nam.post(makeRequest("/login"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            // Fall back to anonymous (API key only) usage; report once.
            m_loginFailed = true;
            cb(tr("Login failed (%1), continuing anonymously").arg(errorText(reply, data)));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        m_token = o.value("token").toString();
        QString base = o.value("base_url").toString();
        base.remove("https://").remove("http://");
        if (!base.isEmpty())
            m_host = base.section('/', 0, 0);
        cb({});
    });
}

void OpenSubtitlesClient::search(const SubQuery &q, SearchCallback cb)
{
    if (m_apiKey.isEmpty()) {
        cb({}, tr("OpenSubtitles API key is not set (Settings → Subtitles)."));
        return;
    }
    ensureLogin([this, q, cb](const QString &loginWarning) {
        Q_UNUSED(loginWarning);
        QStringList langs = q.languages;
        langs.sort();
        QStringList params; // alphabetical order
        if (q.episode >= 0)
            params << "episode_number=" + QString::number(q.episode);
        if (!langs.isEmpty())
            params << "languages=" + encodeParam(langs.join(','));
        if (!q.hash.isEmpty())
            params << "moviehash=" + q.hash;
        if (!q.query.isEmpty())
            params << "query=" + encodeParam(q.query);
        if (q.season >= 0)
            params << "season_number=" + QString::number(q.season);

        QNetworkReply *reply = m_nam.get(makeRequest("/subtitles", params.join('&')));
        connect(reply, &QNetworkReply::finished, this, [reply, cb] {
            reply->deleteLater();
            const QByteArray data = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                cb({}, errorText(reply, data));
                return;
            }
            QVector<SubResult> results;
            const QJsonArray items = QJsonDocument::fromJson(data).object().value("data").toArray();
            for (const QJsonValue &v : items) {
                const QJsonObject a = v.toObject().value("attributes").toObject();
                const QJsonArray files = a.value("files").toArray();
                if (files.isEmpty())
                    continue;
                const QJsonObject file = files.first().toObject();
                SubResult r;
                r.fileId = file.value("file_id").toInt();
                r.fileName = file.value("file_name").toString();
                r.language = a.value("language").toString();
                r.release = a.value("release").toString();
                r.downloads = a.value("download_count").toInt();
                r.trusted = a.value("from_trusted").toBool();
                r.hashMatch = a.value("moviehash_match").toBool();
                r.aiTranslated = a.value("ai_translated").toBool() || a.value("machine_translated").toBool();
                const QJsonObject fd = a.value("feature_details").toObject();
                r.title = fd.value("title").toString();
                if (fd.contains("year") && fd.value("year").toInt() > 0)
                    r.title += QString(" (%1)").arg(fd.value("year").toInt());
                if (r.fileId)
                    results << r;
            }
            cb(results, {});
        });
    });
}

void OpenSubtitlesClient::download(int fileId, DownloadCallback cb)
{
    ensureLogin([this, fileId, cb](const QString &) {
        const QJsonObject body{{"file_id", fileId}};
        QNetworkReply *reply = m_nam.post(makeRequest("/download"), QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
            reply->deleteLater();
            const QByteArray data = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                cb({}, {}, errorText(reply, data));
                return;
            }
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            const QUrl link(o.value("link").toString());
            const QString fileName = o.value("file_name").toString();
            if (!link.isValid() || link.isEmpty()) {
                cb({}, {}, tr("No download link returned."));
                return;
            }
            QNetworkRequest req(link);
            req.setRawHeader("User-Agent", kUserAgent);
            QNetworkReply *file = m_nam.get(req);
            connect(file, &QNetworkReply::finished, this, [file, fileName, cb] {
                file->deleteLater();
                const QByteArray bytes = file->readAll();
                if (file->error() != QNetworkReply::NoError)
                    cb({}, {}, errorText(file, bytes));
                else
                    cb(bytes, fileName, {});
            });
        });
    });
}
