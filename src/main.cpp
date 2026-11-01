#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <clocale>

#include "MainWindow.h"
#include "ui/Theme.h"

// The player used to be called LitePlayer: move its settings, playlists, yt-dlp and subtitle cache
// (%APPDATA%\LitePlayer\LitePlayer.ini + LitePlayer\) to the new name once, before anything reads them.
static void migrateFromOldName()
{
    const QString fresh = QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).absolutePath();
    const QString old = QFileInfo(fresh).absolutePath() + "/LitePlayer";
    if (QFileInfo::exists(fresh) || !QFileInfo::exists(old))
        return;
    if (QDir().rename(old, fresh)) {
        QFile::rename(fresh + "/LitePlayer.ini", fresh + "/Vora.ini");
        QDir().rename(fresh + "/LitePlayer", fresh + "/Vora");
        return;
    }
    // Something holds a file in the old folder (a running old copy): copy what matters most.
    QDir().mkpath(fresh + "/Vora");
    QFile::copy(old + "/LitePlayer.ini", fresh + "/Vora.ini");
    QFile::copy(old + "/LitePlayer/playlists.json", fresh + "/Vora/playlists.json");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Vora");
    QApplication::setApplicationName("Vora");
    QApplication::setApplicationVersion(VORA_VERSION);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    migrateFromOldName();

    // libmpv requires C numeric locale (Qt may change it on init)
    std::setlocale(LC_NUMERIC, "C");

    Theme::apply(app);

    MainWindow w;
    w.show();

    // vora <file|folder|url>
    // vora --youtube ["search words"]
    // vora --download <url> [video|mp3|m4a]
    const QStringList args = app.arguments();
    // vora --playlists        |  vora --playlist ["name"]
    if (args.size() > 1 && args.at(1) == "--playlists")
        w.openPlaylists();
    else if (args.size() > 1 && args.at(1) == "--playlist")
        w.playSavedPlaylist(args.mid(2).join(' '));
    else if (args.size() > 2 && args.at(1) == "--download")
        w.startDownload(args.at(2), args.value(3, "video"));
    else if (args.size() > 1 && args.at(1) == "--youtube")
        w.openYouTube(args.mid(2).join(' '));
    else if (args.size() > 1)
        w.openPath(args.at(1));

    return app.exec();
}
