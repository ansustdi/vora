#pragma once

#include "BsPlayerClient.h"
#include "OpenSubtitlesClient.h"

#include <QObject>

class MpvWidget;

// Decides when to fetch subtitles for the playing video and loads them into mpv.
// Sources: BSPlayer (keyless, hash match) always; OpenSubtitles.com when an API key is set.
class SubtitleManager : public QObject
{
    Q_OBJECT
public:
    using ResultsCallback = std::function<void(QVector<SubResult> results, QString error)>;

    SubtitleManager(MpvWidget *mpv, QObject *parent = nullptr);

    void reloadSettings();
    bool hasOpenSubtitles() const { return m_os.hasApiKey(); }

    // Called once the video's track list is known.
    void onFileLoaded(const QString &videoPath);
    // The video subtitles belong to; empty for streams / images (search and downloads are refused).
    void setVideo(const QString &videoPath);
    // force=true: search even if a matching subtitle is already present.
    // ask=true: emit subtitlesFound() for the user to choose instead of loading the best one.
    void searchAndLoad(bool force, bool ask = false);
    void downloadAndLoad(const SubResult &result);
    // Queries every available source and returns the merged results.
    void searchAll(const SubQuery &query, ResultsCallback cb);

    QString videoPath() const { return m_video; }
    SubQuery buildQuery() const;
    static QString cleanTitle(const QString &fileName, int *season, int *episode);

signals:
    void status(const QString &message);
    void subtitlesFound(const QVector<SubResult> &ranked);
    void videoChanged();

private:
    bool hasPreferredSubtitle() const;
    void loadSubfolderSubtitles();
    void selectBestLocalSubtitle();
    QVector<SubResult> rank(const QVector<SubResult> &results, const QStringList &languages) const;
    void saveAndLoad(const SubResult &result, const QByteArray &data, const QString &remoteName, const QString &video);
    QString savePath(const QString &lang, const QString &remoteName) const;

    MpvWidget *m_mpv;
    BsPlayerClient m_bsp;
    OpenSubtitlesClient m_os;
    QString m_video;
    QString m_autoTriedFor;
};
