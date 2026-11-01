#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

struct SubResult
{
    QString provider = "opensubtitles"; // or "bsplayer"
    QString url;                        // bsplayer: direct (gzip) download link
    int size = 0;                       // bsplayer: compressed size, tiny = junk/ad
    double rating = 0;
    int fileId = 0;
    QString language;
    QString release;
    QString fileName;
    QString title;
    int downloads = 0;
    bool trusted = false;
    bool hashMatch = false;
    bool aiTranslated = false;
};

struct SubQuery
{
    QString hash;
    QString query;
    QStringList languages;
    int season = -1;
    int episode = -1;
};

// Minimal async client for the OpenSubtitles.com REST API v1.
class OpenSubtitlesClient : public QObject
{
    Q_OBJECT
public:
    using SearchCallback = std::function<void(QVector<SubResult> results, QString error)>;
    using DownloadCallback = std::function<void(QByteArray data, QString fileName, QString error)>;

    explicit OpenSubtitlesClient(QObject *parent = nullptr);

    void setCredentials(const QString &apiKey, const QString &user, const QString &password);
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }

    void search(const SubQuery &query, SearchCallback cb);
    void download(int fileId, DownloadCallback cb);

private:
    QNetworkRequest makeRequest(const QString &path, const QString &query = {}) const;
    void ensureLogin(std::function<void(QString error)> cb);
    static QString errorText(class QNetworkReply *reply, const QByteArray &body);

    QNetworkAccessManager m_nam;
    QString m_apiKey;
    QString m_user;
    QString m_password;
    QString m_token;
    QString m_host = "api.opensubtitles.com";
    bool m_loginFailed = false;
};
