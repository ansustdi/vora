#include "SubtitleManager.h"

#include "MpvWidget.h"
#include "OsHash.h"
#include "FolderList.h"
#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <climits>
#include <memory>
#include <tuple>

// External subtitle files smaller than this are ads ("Downloaded from YTS.MX") or forced-only.
static constexpr qint64 kJunkSubBytes = 4096;
// BSPlayer reports gzip size; anything this small is not a full subtitle.
static constexpr int kJunkGzBytes = 2000;

// Tags that look like language codes but describe the subtitle instead ("English.HI.srt" is
// hearing-impaired, not Hindi).
static bool isNotALanguage(const QString &code)
{
    static const QStringList tags = {"hi", "sdh", "cc", "forced", "full", "sub", "subs"};
    return tags.contains(code.toLower());
}

static QLocale::Language languageOf(const QString &code)
{
    // ISO 639-1 / 639-2 codes ("en", "eng", "ger") and variants like "pt-br"; ISO 639-3 is left
    // out on purpose — it turns ordinary words and tags into obscure languages.
    const QString c = code.section('-', 0, 0).toLower();
    if (c.isEmpty() || isNotALanguage(c))
        return QLocale::AnyLanguage;
    if (c.size() <= 3)
        return QLocale::codeToLanguage(c, QLocale::ISO639Part1 | QLocale::ISO639Part2);
    // Full English language names ("English", "Brazilian Portuguese" -> Portuguese).
    static const QHash<QString, QLocale::Language> byName = [] {
        QHash<QString, QLocale::Language> h;
        for (int l = QLocale::Abkhazian; l <= QLocale::LastLanguage; ++l) {
            const QString name = QLocale::languageToString(QLocale::Language(l)).toLower();
            if (!name.isEmpty())
                h.insert(name, QLocale::Language(l));
        }
        return h;
    }();
    if (const auto it = byName.constFind(c); it != byName.cend())
        return *it;
    for (const QString &word : c.split(QRegularExpression("[^a-z]+"), Qt::SkipEmptyParts))
        if (const auto it = byName.constFind(word); it != byName.cend())
            return *it;
    return QLocale::AnyLanguage;
}

static int languageRank(const QString &code, const QStringList &preferred)
{
    if (code.isEmpty())
        return -1;
    const QLocale::Language l = languageOf(code);
    if (l == QLocale::AnyLanguage) // unknown never "matches" another unknown
        return -1;
    for (int i = 0; i < preferred.size(); ++i)
        if (languageOf(preferred.at(i)) == l)
            return i;
    return -1;
}

SubtitleManager::SubtitleManager(MpvWidget *mpv, QObject *parent)
    : QObject(parent)
    , m_mpv(mpv)
{
    reloadSettings();
    // Mirror status messages into mpv's log (VORA_MPV_LOG) for troubleshooting.
    connect(this, &SubtitleManager::status, this, [this](const QString &msg) { m_mpv->command({"print-text", "[subs] " + msg}); });
}

void SubtitleManager::reloadSettings()
{
    m_os.setCredentials(Settings::apiKey(), Settings::osUser(), Settings::osPassword());
}

QString SubtitleManager::cleanTitle(const QString &fileName, int *season, int *episode)
{
    QString name = QFileInfo(fileName).completeBaseName();
    name.replace(QRegularExpression("[._]+"), " ");

    static const QRegularExpression se(R"(\b[Ss](\d{1,2})[ ._-]?[Ee](\d{1,3})\b|\b(\d{1,2})x(\d{2,3})\b)");
    const QRegularExpressionMatch m = se.match(name);
    if (m.hasMatch()) {
        if (season)
            *season = (m.captured(1).isEmpty() ? m.captured(3) : m.captured(1)).toInt();
        if (episode)
            *episode = (m.captured(2).isEmpty() ? m.captured(4) : m.captured(2)).toInt();
        name = name.left(m.capturedStart());
    }

    // Cut at the first release tag (year, resolution, source, codec...).
    static const QRegularExpression tags(
        R"(\b((19|20)\d{2}|\d{3,4}p|4k|uhd|hdr|bluray|blu ray|brrip|bdrip|web ?dl|webrip|hdtv|hdrip|dvdrip|)"
        R"(x264|x265|h ?264|h ?265|hevc|xvid|aac|ac3|dts|remux|proper|repack|extended|unrated|imax)\b)",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch t = tags.match(name);
    if (t.hasMatch() && t.capturedStart() > 0)
        name = name.left(t.capturedStart());

    name.remove(QRegularExpression(R"([\[\(].*?[\]\)])"));
    return name.simplified();
}

SubQuery SubtitleManager::buildQuery() const
{
    SubQuery q;
    q.hash = openSubtitlesHash(m_video);
    q.query = cleanTitle(m_video, &q.season, &q.episode);
    q.languages = Settings::subLanguages();
    return q;
}

static bool isJunkTrack(const QVariantMap &t)
{
    if (t.value("forced").toBool())
        return true;
    const QString file = t.value("external-filename").toString();
    if (file.isEmpty())
        return false;
    if (QFileInfo(file).fileName().contains("forced", Qt::CaseInsensitive))
        return true;
    return QFileInfo(file).size() < kJunkSubBytes;
}

bool SubtitleManager::hasPreferredSubtitle() const
{
    const QStringList wanted = Settings::subLanguages();
    for (const QVariant &v : m_mpv->getNode("track-list").toList()) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() != "sub" || isJunkTrack(t))
            continue;
        const QString lang = t.value("lang").toString();
        if (languageRank(lang, wanted) >= 0)
            return true;
        if (lang.isEmpty() && t.value("external").toBool())
            return true; // a real sidecar file without a language tag: assume the user put it there
    }
    return false;
}

void SubtitleManager::selectBestLocalSubtitle()
{
    // mpv's own choice can land on an ad .srt or a forced-only track; pick the most complete
    // subtitle in the most preferred language instead.
    const QStringList wanted = Settings::subLanguages();
    qlonglong bestId = -1;
    std::tuple<int, int, int> bestKey{INT_MAX, 0, 0};
    qlonglong selectedId = -1;
    bool selectedGood = false;
    for (const QVariant &v : m_mpv->getNode("track-list").toList()) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() != "sub")
            continue;
        const qlonglong id = t.value("id").toLongLong();
        int rank = languageRank(t.value("lang").toString(), wanted);
        const bool junk = isJunkTrack(t);
        if (rank < 0 && t.value("lang").toString().isEmpty() && t.value("external").toBool())
            rank = int(wanted.size()); // untagged sidecar the user put there: usable, lowest priority
        if (t.value("selected").toBool()) {
            selectedId = id;
            selectedGood = !junk && rank >= 0;
        }
        if (junk || rank < 0)
            continue;
        const QString name = t.value("title").toString() + t.value("external-filename").toString();
        const bool sdh = name.contains("SDH", Qt::CaseInsensitive) || t.value("hearing-impaired").toBool();
        const std::tuple<int, int, int> key{rank, sdh ? 1 : 0, t.value("external").toBool() ? 0 : 1};
        if (key < bestKey) {
            bestKey = key;
            bestId = id;
        }
    }
    if (bestId >= 0 && (!selectedGood || std::get<0>(bestKey) < languageRank(m_mpv->getString("current-tracks/sub/lang"), wanted)))
        m_mpv->setProp("sid", QString::number(bestId));
    else if (bestId < 0 && selectedId >= 0 && !selectedGood)
        m_mpv->setProp("sid", QStringLiteral("no")); // only junk available: hide the ad text
}

void SubtitleManager::loadSubfolderSubtitles()
{
    // Release groups ship subtitles as Subs/English.eng.srt or Subs/<episode name>/2_English.srt.
    // mpv's sub-auto only picks files named after the video, so add these ourselves.
    const QFileInfo vi(m_video);
    QSet<QString> loaded;
    for (const QVariant &v : m_mpv->getNode("track-list").toList())
        loaded.insert(QFileInfo(v.toMap().value("external-filename").toString()).absoluteFilePath().toLower());

    QStringList dirs;
    QSet<QString> seenDirs;
    for (const QString &name : {"Subs", "Subtitles", "Sub", "subs", "subtitles", "sub"}) {
        const QDir d(vi.absolutePath() + "/" + name);
        if (!d.exists() || seenDirs.contains(d.canonicalPath().toLower()))
            continue;
        seenDirs.insert(d.canonicalPath().toLower());
        const QDir perVideo(d.filePath(vi.completeBaseName()));
        if (perVideo.exists())
            dirs << perVideo.absolutePath();
        dirs << d.absolutePath();
    }

    static const QRegularExpression langSuffix(R"(^(.*?)[._ -]([a-z]{2,3}(?:-[a-z]{2})?)$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression leadingNumber(R"(^\d+[_ .-]+)");
    int added = 0;
    for (const QString &dir : dirs) {
        const QFileInfoList files = QDir(dir).entryInfoList(mediaNameFilters(MediaType::Subtitle), QDir::Files, QDir::Name);
        for (const QFileInfo &f : files) {
            if (added >= 80 || loaded.contains(f.absoluteFilePath().toLower()))
                continue;
            // "English [SDH].eng.srt" -> title "English [SDH]", lang "eng"
            // "2_English.srt" (per-episode folders) -> title "English", lang "en"
            QString title = f.completeBaseName(), lang;
            title.remove(leadingNumber);
            const QRegularExpressionMatch m = langSuffix.match(title);
            if (m.hasMatch() && languageOf(m.captured(2)) != QLocale::AnyLanguage) {
                title = m.captured(1);
                lang = m.captured(2).toLower();
            } else {
                QString name = title;
                name.remove(QRegularExpression(R"([\[\(].*?[\]\)])")); // "English [Forced]" -> "English"
                const QLocale::Language l = languageOf(name.simplified());
                if (l != QLocale::AnyLanguage)
                    lang = QLocale::languageToCode(l, QLocale::ISO639Part1);
            }
            m_mpv->command({"sub-add", f.absoluteFilePath(), "auto", title, lang});
            loaded.insert(f.absoluteFilePath().toLower());
            ++added;
        }
    }
}

void SubtitleManager::setVideo(const QString &videoPath)
{
    if (videoPath == m_video)
        return;
    m_video = videoPath; // empty for streams and images: nothing to search or save for
    emit videoChanged();
}

void SubtitleManager::onFileLoaded(const QString &videoPath)
{
    setVideo(videoPath);
    reloadSettings();
    loadSubfolderSubtitles();
    selectBestLocalSubtitle();
    if (!Settings::autoSubs() || m_autoTriedFor == videoPath)
        return;
    m_autoTriedFor = videoPath;
    if (hasPreferredSubtitle())
        return;
    searchAndLoad(false, Settings::subsMode() == "ask");
}

void SubtitleManager::searchAll(const SubQuery &q, ResultsCallback cb)
{
    if (m_video.isEmpty()) {
        cb({}, tr("Subtitles can only be searched for local video files"));
        return;
    }
    reloadSettings();
    auto merged = std::make_shared<QVector<SubResult>>();
    auto errors = std::make_shared<QStringList>();
    const bool useOs = m_os.hasApiKey();

    auto finish = [this, merged, errors, cb, q, useOs]() {
        if (!useOs) {
            cb(*merged, merged->isEmpty() ? errors->join("; ") : QString());
            return;
        }
        m_os.search(q, [merged, errors, cb](QVector<SubResult> results, const QString &error) {
            if (!error.isEmpty())
                *errors << "OpenSubtitles: " + error;
            *merged += results;
            cb(*merged, merged->isEmpty() ? errors->join("; ") : QString());
        });
    };

    if (q.hash.isEmpty() || !QFileInfo::exists(m_video)) {
        finish();
        return;
    }
    m_bsp.search(q.hash, QFileInfo(m_video).size(), q.languages,
                 [merged, errors, finish](QVector<SubResult> results, const QString &error) {
                     if (!error.isEmpty())
                         *errors << "BSPlayer: " + error;
                     *merged += results;
                     finish();
                 });
}

QVector<SubResult> SubtitleManager::rank(const QVector<SubResult> &results, const QStringList &languages) const
{
    // Preferred language order first; within a language: not forced > same release name > exact
    // file (hash) match > human translation > trusted > rating/popularity. Ads/tiny files dropped.
    const QString videoBase = QFileInfo(m_video).completeBaseName();
    auto key = [&](const SubResult &r) {
        int lr = languageRank(r.language, languages);
        if (lr < 0)
            lr = 1000;
        const bool sameName = QFileInfo(r.fileName).completeBaseName().startsWith(videoBase, Qt::CaseInsensitive);
        const bool forced = r.fileName.contains("forced", Qt::CaseInsensitive);
        return std::make_tuple(-lr, !forced, sameName, r.hashMatch, !r.aiTranslated, r.trusted, r.rating, r.downloads, r.size);
    };
    QVector<SubResult> out;
    for (const SubResult &r : results)
        if (!(r.provider == "bsplayer" && r.size < kJunkGzBytes))
            out << r;
    std::stable_sort(out.begin(), out.end(), [&](const SubResult &a, const SubResult &b) { return key(a) > key(b); });
    return out;
}

void SubtitleManager::searchAndLoad(bool force, bool ask)
{
    if (m_video.isEmpty())
        return;
    if (!force && hasPreferredSubtitle())
        return;

    const SubQuery q = buildQuery();
    const QString video = m_video;
    emit status(tr("Searching subtitles…"));
    searchAll(q, [this, q, video, ask](QVector<SubResult> results, const QString &error) {
        if (video != m_video)
            return; // user moved on
        const QVector<SubResult> ranked = rank(results, q.languages);
        if (!ranked.isEmpty()) {
            if (ask)
                emit subtitlesFound(ranked);
            else
                downloadAndLoad(ranked.first());
            return;
        }
        if (!error.isEmpty())
            emit status(tr("Subtitle search failed: %1").arg(error));
        else
            emit status(tr("No %1 subtitles found online").arg(q.languages.join('/')));
    });
}

QString SubtitleManager::savePath(const QString &lang, const QString &remoteName) const
{
    QString ext = QFileInfo(remoteName).suffix().toLower();
    if (ext.isEmpty() || ext.size() > 4)
        ext = "srt";
    const QFileInfo vi(m_video);
    const QString code = lang.isEmpty() ? QStringLiteral("und") : lang;
    QString dir = vi.absolutePath();
    if (!Settings::subsNextToVideo() || !QFileInfo(dir).isWritable()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/subs";
        QDir().mkpath(dir);
    }
    // Never replace a subtitle that's already there (it may be the user's own, hand-synced one).
    QString path = QString("%1/%2.%3.%4").arg(dir, vi.completeBaseName(), code, ext);
    for (int n = 2; QFileInfo::exists(path) && n < 100; ++n)
        path = QString("%1/%2.%3 (%4).%5").arg(dir, vi.completeBaseName(), code).arg(n).arg(ext);
    return path;
}

void SubtitleManager::saveAndLoad(const SubResult &result, const QByteArray &data, const QString &remoteName, const QString &video)
{
    if (video != m_video || m_video.isEmpty())
        return;
    const QByteArray head = data.left(512).trimmed().toLower();
    if (data.size() < 16 || head.startsWith("<!doctype") || head.startsWith("<html") || head.startsWith("{")) {
        emit status(tr("Subtitle download failed: the server sent something that isn't a subtitle"));
        return;
    }
    QString path = savePath(result.language, remoteName);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        // Folder not writable after all: fall back to the app data dir.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/subs";
        QDir().mkpath(dir);
        path = dir + "/" + QFileInfo(path).fileName();
        QSaveFile f2(path);
        if (!f2.open(QIODevice::WriteOnly) || f2.write(data) != data.size() || !f2.commit()) {
            emit status(tr("Could not save subtitle file"));
            return;
        }
    }
    const QString source = result.provider == "bsplayer" ? QStringLiteral("BSPlayer") : QStringLiteral("OpenSubtitles");
    m_mpv->command({"sub-add", path, "select", source, result.language});
    emit status(tr("Subtitle loaded (%1, %2)").arg(result.language, source));
}

void SubtitleManager::downloadAndLoad(const SubResult &result)
{
    if (m_video.isEmpty())
        return;
    const QString video = m_video;
    emit status(tr("Downloading subtitle (%1)…").arg(result.language));
    if (result.provider == "bsplayer") {
        m_bsp.download(result.url, [this, result, video](const QByteArray &data, const QString &error) {
            if (!error.isEmpty())
                emit status(tr("Subtitle download failed: %1").arg(error));
            else
                saveAndLoad(result, data, result.fileName, video);
        });
        return;
    }
    m_os.download(result.fileId, [this, result, video](const QByteArray &data, const QString &fileName, const QString &error) {
        if (!error.isEmpty())
            emit status(tr("Subtitle download failed: %1").arg(error));
        else
            saveAndLoad(result, data, fileName.isEmpty() ? result.fileName : fileName, video);
    });
}
