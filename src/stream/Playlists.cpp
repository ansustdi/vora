#include "Playlists.h"

#include <QDateTime>
#include <QJsonParseError>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <tuple>

PlaylistStore::PlaylistStore(QObject *parent)
    : QObject(parent)
{
    m_loadFailed = !load();
    if (m_lists.isEmpty())
        m_lists.push_back({tr("Watch later"), {}}); // written on the first real change, not now
}

QString PlaylistStore::path() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/playlists.json";
}

bool PlaylistStore::load()
{
    QFile f(path());
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly))
        return false; // locked by antivirus / sync: never treat that as "no playlists"
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return false;
    const QJsonArray lists = doc.object().value("playlists").toArray();
    for (const QJsonValue &lv : lists) {
        const QJsonObject lo = lv.toObject();
        Playlist p;
        p.name = lo.value("name").toString();
        for (const QJsonValue &iv : lo.value("items").toArray()) {
            const QJsonObject io = iv.toObject();
            PlaylistItem it;
            it.url = io.value("url").toString();
            it.title = io.value("title").toString();
            it.channel = io.value("channel").toString();
            it.id = io.value("id").toString();
            it.duration = io.value("duration").toDouble();
            it.views = io.contains("views") ? qint64(io.value("views").toDouble()) : -1;
            it.added = qint64(io.value("added").toDouble());
            if (!it.url.isEmpty())
                p.items.push_back(it);
        }
        if (!p.name.isEmpty())
            m_lists.push_back(p);
    }
    return true;
}

void PlaylistStore::save()
{
    QJsonArray lists;
    for (const Playlist &p : m_lists) {
        QJsonArray items;
        for (const PlaylistItem &it : p.items)
            items.append(QJsonObject{{"url", it.url}, {"title", it.title}, {"channel", it.channel},
                                     {"id", it.id}, {"duration", it.duration}, {"views", double(it.views)},
                                     {"added", double(it.added)}});
        lists.append(QJsonObject{{"name", p.name}, {"items", items}});
    }
    if (m_loadFailed) {
        // The file on disk couldn't be read at startup: keep it before replacing it.
        QFile::copy(path(), path() + QDateTime::currentDateTime().toString(".yyyyMMdd-HHmmss") + ".bak");
        m_loadFailed = false;
    }
    QSaveFile f(path());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"playlists", lists}}).toJson());
        f.commit();
    }
    emit changed();
}

int PlaylistStore::create(const QString &name)
{
    m_lists.push_back({name.trimmed().isEmpty() ? tr("New playlist") : name.trimmed(), {}});
    save();
    return int(m_lists.size()) - 1;
}

void PlaylistStore::rename(int index, const QString &name)
{
    if (index < 0 || index >= m_lists.size() || name.trimmed().isEmpty())
        return;
    m_lists[index].name = name.trimmed();
    save();
}

void PlaylistStore::remove(int index)
{
    if (index < 0 || index >= m_lists.size())
        return;
    m_lists.removeAt(index);
    if (m_lists.isEmpty())
        m_lists.push_back({tr("Watch later"), {}});
    save();
}

bool PlaylistStore::addItem(int list, const PlaylistItem &item)
{
    if (list < 0 || list >= m_lists.size() || item.url.isEmpty())
        return false;
    const QString yid = youtubeId(item.url);
    for (const PlaylistItem &existing : m_lists.at(list).items)
        if (existing.url == item.url || (!yid.isEmpty() && youtubeId(existing.url) == yid))
            return false;
    PlaylistItem copy = item;
    if (copy.id.isEmpty())
        copy.id = youtubeId(copy.url);
    if (copy.added == 0)
        copy.added = QDateTime::currentMSecsSinceEpoch();
    m_lists[list].items.push_back(copy);
    save();
    return true;
}

void PlaylistStore::addItems(int list, const QVector<PlaylistItem> &items)
{
    if (list < 0 || list >= m_lists.size())
        return;
    for (const PlaylistItem &item : items) {
        bool dup = false;
        const QString yid = youtubeId(item.url);
        for (const PlaylistItem &existing : m_lists.at(list).items)
            dup = dup || existing.url == item.url || (!yid.isEmpty() && youtubeId(existing.url) == yid);
        if (!dup && !item.url.isEmpty()) {
            PlaylistItem copy = item;
            if (copy.id.isEmpty())
                copy.id = youtubeId(copy.url);
            if (copy.added == 0) // keep the import order when sorting by date added
                copy.added = QDateTime::currentMSecsSinceEpoch() + m_lists.at(list).items.size();
            m_lists[list].items.push_back(copy);
        }
    }
    save();
}

void PlaylistStore::removeItem(int list, int item)
{
    if (list < 0 || list >= m_lists.size() || item < 0 || item >= m_lists.at(list).items.size())
        return;
    m_lists[list].items.removeAt(item);
    save();
}

void PlaylistStore::moveItem(int list, int from, int to)
{
    if (list < 0 || list >= m_lists.size())
        return;
    auto &items = m_lists[list].items;
    if (from < 0 || from >= items.size() || to < 0 || to >= items.size() || from == to)
        return;
    items.move(from, to);
    save();
}

void PlaylistStore::updateTitle(const QString &url, const QString &title)
{
    if (title.isEmpty())
        return;
    bool touched = false;
    for (Playlist &p : m_lists)
        for (PlaylistItem &it : p.items)
            if (it.url == url && (it.title.isEmpty() || it.title == it.url)) {
                it.title = title;
                touched = true;
            }
    if (touched)
        save();
}

void PlaylistStore::updateInfo(const PlaylistItem &info)
{
    updateInfos({info});
}

int PlaylistStore::indexOf(const QString &name) const
{
    for (int i = 0; i < m_lists.size(); ++i)
        if (m_lists.at(i).name == name)
            return i;
    return -1;
}

void PlaylistStore::updateInfos(const QVector<PlaylistItem> &infos)
{
    bool touched = false;
    for (const PlaylistItem &info : infos)
    for (Playlist &p : m_lists)
        for (PlaylistItem &it : p.items) {
            const QString yid = youtubeId(info.url);
            const bool same = it.url == info.url || (!yid.isEmpty() && youtubeId(it.url) == yid);
            if (!same)
                continue;
            if (info.views >= 0)
                it.views = info.views;
            if (info.duration > 0)
                it.duration = info.duration;
            if (!info.channel.isEmpty() && it.channel.isEmpty())
                it.channel = info.channel;
            if (!info.title.isEmpty() && (it.title.isEmpty() || it.title == it.url))
                it.title = info.title;
            touched = true;
        }
    if (touched)
        save();
}

void PlaylistStore::sortList(int list, const QString &mode)
{
    if (list < 0 || list >= m_lists.size())
        return;
    auto &items = m_lists[list].items;
    // Unknown values (views -1, duration 0) always sink to the bottom.
    auto unknownLast = [](bool aKnown, bool bKnown, bool aBefore) {
        if (aKnown != bKnown)
            return aKnown;
        return aBefore;
    };
    std::stable_sort(items.begin(), items.end(), [&](const PlaylistItem &a, const PlaylistItem &b) {
        if (mode == "views_desc")
            return unknownLast(a.views >= 0, b.views >= 0, a.views > b.views);
        if (mode == "views_asc")
            return unknownLast(a.views >= 0, b.views >= 0, a.views < b.views);
        if (mode == "duration_desc")
            return unknownLast(a.duration > 0, b.duration > 0, a.duration > b.duration);
        if (mode == "duration_asc")
            return unknownLast(a.duration > 0, b.duration > 0, a.duration < b.duration);
        if (mode == "added_desc")
            return a.added > b.added;
        if (mode == "added_asc")
            return a.added < b.added;
        return QString::localeAwareCompare(a.title, b.title) < 0; // "title"
    });
    save();
}

QString PlaylistStore::youtubeId(const QString &url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    if (host.endsWith("youtu.be"))
        return u.path().mid(1).section('/', 0, 0);
    if (host.contains("youtube.com")) {
        const QString v = QUrlQuery(u).queryItemValue("v");
        if (!v.isEmpty())
            return v;
        static const QRegularExpression shorts(R"(/(?:shorts|live|embed)/([\w-]{6,}))");
        const QRegularExpressionMatch m = shorts.match(u.path());
        if (m.hasMatch())
            return m.captured(1);
    }
    return {};
}
