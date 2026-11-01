#include "BsPlayerClient.h"

#include <QFileInfo>
#include <QLocale>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <memory>
#include <zlib.h>

static constexpr int kHostCount = 8; // s1..s8.api.bsplayer-subtitles.com, not all are always up
static const char *kAppId = "BSPlayer v2.72";
static const char *kUserAgent = "BSPlayer/2.x (1022.12362)";

static QString hostName(int i)
{
    return QString("s%1.api.bsplayer-subtitles.com").arg(i);
}

// "en" -> "eng" (bibliographic 639-2, what the service uses: ger, fre, chi...)
static QString toServiceLang(const QString &code)
{
    const QLocale::Language l = QLocale::codeToLanguage(code.section('-', 0, 0).toLower());
    return l == QLocale::AnyLanguage ? code : QLocale::languageToCode(l, QLocale::ISO639Part2B);
}

// "eng" -> "en"
static QString fromServiceLang(const QString &code)
{
    const QLocale::Language l = QLocale::codeToLanguage(code.toLower());
    if (l == QLocale::AnyLanguage)
        return code;
    const QString two = QLocale::languageToCode(l, QLocale::ISO639Part1);
    return two.isEmpty() ? code : two;
}

BsPlayerClient::BsPlayerClient(QObject *parent)
    : QObject(parent)
{
}

QByteArray BsPlayerClient::gunzip(const QByteArray &data)
{
    if (data.size() < 2 || uchar(data[0]) != 0x1f || uchar(data[1]) != 0x8b)
        return data; // not compressed
    z_stream s{};
    if (inflateInit2(&s, 16 + MAX_WBITS) != Z_OK)
        return {};
    s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    s.avail_in = uInt(data.size());
    QByteArray out;
    char buf[65536];
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        s.next_out = reinterpret_cast<Bytef *>(buf);
        s.avail_out = sizeof(buf);
        ret = inflate(&s, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&s);
            return {};
        }
        out.append(buf, int(sizeof(buf) - s.avail_out));
        if (s.avail_in == 0 && ret != Z_STREAM_END && s.avail_out != 0)
            break; // truncated input
    }
    inflateEnd(&s);
    return ret == Z_STREAM_END ? out : QByteArray(); // a cut-off download is not a subtitle
}

void BsPlayerClient::call(const QString &host, const QString &func, const QString &params, int timeoutMs, SoapCallback cb)
{
    const QString url = QString("http://%1/v1.php").arg(host);
    const QString body = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:ns1=\"%1\">"
        "<SOAP-ENV:Body SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<ns1:%2>%3</ns1:%2></SOAP-ENV:Body></SOAP-ENV:Envelope>").arg(url, func, params);

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=utf-8");
    req.setRawHeader("SOAPAction", QString("\"http://api.bsplayer-subtitles.com/v1.php#%1\"").arg(func).toUtf8());
    req.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = m_nam.post(req, body.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, cb] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, {}, {}, reply->errorString());
            return;
        }
        // <return><result>200</result><status>OK</status><data>…</data></return>
        QString result, payload;
        QXmlStreamReader xml(data);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (xml.name() == u"result" && result.isEmpty())
                result = xml.readElementText();
            else if (xml.name() == u"data" && payload.isEmpty())
                payload = xml.readElementText(QXmlStreamReader::IncludeChildElements);
        }
        cb(result, payload, data, {});
    });
}

void BsPlayerClient::search(const QString &hash, qint64 size, const QStringList &languages, SearchCallback cb)
{
    if (hash.isEmpty()) {
        cb({}, tr("File too small to hash"));
        return;
    }
    QStringList list;
    for (const QString &l : languages)
        list << toServiceLang(l);
    const QString langs = list.join(',');

    // Log in on every server at once and continue with whichever answers first:
    // individual servers are often down, and trying them one by one takes minutes.
    struct Race
    {
        bool won = false;
        int pending = kHostCount;
    };
    auto race = std::make_shared<Race>();
    const QString login = QString("<username></username><password></password><AppID>%1</AppID>").arg(kAppId);
    for (int i = 1; i <= kHostCount; ++i) {
        const QString host = hostName(i);
        call(host, "logIn", login, 8000, [=](const QString &result, const QString &token, const QByteArray &, const QString &error) {
            const bool ok = error.isEmpty() && result == "200" && !token.isEmpty();
            if (race->won) {
                if (ok)
                    call(host, "logOut", QString("<handle>%1</handle>").arg(token), 5000, [](auto &&...) {});
                return;
            }
            if (ok) {
                race->won = true;
                searchOn(host, token, hash, size, langs, cb);
            } else if (--race->pending == 0) {
                cb({}, tr("servers not responding, try again later"));
            }
        });
    }
}

void BsPlayerClient::searchOn(const QString &host, const QString &token, const QString &hash, qint64 size,
                              const QString &langs, SearchCallback cb)
{
    const QString params = QString("<handle>%1</handle><movieHash>%2</movieHash><movieSize>%3</movieSize>"
                                   "<languageId>%4</languageId><imdbId>*</imdbId>")
                               .arg(token, hash).arg(size).arg(langs);
    call(host, "searchSubtitles", params, 15000, [=](const QString &, const QString &, const QByteArray &body, const QString &err) {
        call(host, "logOut", QString("<handle>%1</handle>").arg(token), 5000, [](auto &&...) {});
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        QVector<SubResult> results;
        QXmlStreamReader xml(body);
        QMap<QString, QString> item;
        bool inItem = false;
        while (!xml.atEnd()) {
            const auto t = xml.readNext();
            if (t == QXmlStreamReader::StartElement) {
                if (xml.name() == u"item") {
                    inItem = true;
                    item.clear();
                } else if (inItem) {
                    item.insert(xml.name().toString(), xml.readElementText());
                }
            } else if (t == QXmlStreamReader::EndElement && xml.name() == u"item") {
                inItem = false;
                SubResult r;
                r.provider = "bsplayer";
                r.url = item.value("subDownloadLink");
                r.fileName = item.value("subName");
                r.release = QFileInfo(r.fileName).completeBaseName();
                r.language = fromServiceLang(item.value("subLang"));
                r.size = item.value("subSize").toInt();
                r.rating = item.value("subRating").toDouble();
                r.hashMatch = true; // the service only matches by hash
                r.trusted = r.rating >= 5;
                if (!r.url.isEmpty())
                    results << r;
            }
        }
        cb(results, {});
    });
}

void BsPlayerClient::download(const QString &url, DownloadCallback cb)
{
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, reply->errorString());
            return;
        }
        const QByteArray data = gunzip(reply->readAll());
        if (data.isEmpty())
            cb({}, tr("Could not decompress subtitle"));
        else
            cb(data, {});
    });
}
