#pragma once

#include <QString>

// OS-specific helpers (kept here so <windows.h> stays out of the rest of the code).
namespace Platform {

void revealInFileManager(const QString &path);
bool setWallpaper(const QString &path);
bool canSetWallpaper();
bool registerOpenWith(QString *error);

} // namespace Platform
