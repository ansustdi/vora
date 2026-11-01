#include "Platform.h"

#include "FolderList.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace Platform {

void revealInFileManager(const QString &path)
{
#if defined(Q_OS_WIN)
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", path});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool canSetWallpaper()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool setWallpaper(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring w = QDir::toNativeSeparators(path).toStdWString();
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t *>(w.c_str()),
                                 SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool registerOpenWith(QString *error)
{
#ifdef Q_OS_WIN
    // Per-user registration: adds Vora to Explorer's "Open with" list; no admin rights needed.
    QSettings reg("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString app = "Applications/" + exeName;

    reg.setValue(app + "/FriendlyAppName", "Vora");
    reg.setValue(app + "/shell/open/command/Default", QString("\"%1\" \"%2\"").arg(exe, "%1"));

    QStringList exts;
    for (MediaType t : {MediaType::Video, MediaType::Image})
        for (const QString &f : mediaNameFilters(t))
            exts << f.mid(1); // "*.mkv" -> ".mkv"
    for (const QString &ext : exts) {
        reg.setValue(app + "/SupportedTypes/" + ext, "");
        reg.setValue(ext + "/OpenWithList/" + exeName + "/Default", "");
    }
    reg.sync();
    if (reg.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate("Platform", "Could not write to the registry.");
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#else
    if (error)
        *error = QCoreApplication::translate("Platform", "Use your desktop's file manager to set associations.");
    return false;
#endif
}

} // namespace Platform
