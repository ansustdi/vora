#pragma once

#include "OpenSubtitlesClient.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

// Keyless client for the BSPlayer subtitle service (SOAP, matched by OpenSubtitles-style file hash).
// Same backend BSPlayer itself uses; no account or API key needed.
class BsPlayerClient : public QObject
{
    Q_OBJECT
public:
    using SearchCallback = std::function<void(QVector<SubResult> results, QString error)>;
    using DownloadCallback = std::function<void(QByteArray data, QString error)>;

    explicit BsPlayerClient(QObject *parent = nullptr);

    // languages: ISO 639-1 codes ("en", "mn"); converted to the 3-letter codes the service expects.
    void search(const QString &hash, qint64 size, const QStringList &languages, SearchCallback cb);
    void download(const QString &url, DownloadCallback cb);

    static QByteArray gunzip(const QByteArray &data);

private:
    using SoapCallback = std::function<void(QString result, QString data, QByteArray body, QString error)>;
    void call(const QString &host, const QString &func, const QString &params, int timeoutMs, SoapCallback cb);
    void searchOn(const QString &host, const QString &token, const QString &hash, qint64 size, const QString &langs,
                  SearchCallback cb);

    QNetworkAccessManager m_nam;
};
