#pragma once

#include <QStringList>
#include <QVariant>

// Thin typed wrapper over QSettings (INI file in the user config dir).
namespace Settings {

QVariant value(const QString &key, const QVariant &def = {});
void setValue(const QString &key, const QVariant &value);

// Subtitles (OpenSubtitles.com)
inline QString apiKey() { return value("subs/apiKey").toString(); }
inline QString osUser() { return value("subs/user").toString(); }
inline QString osPassword() { return value("subs/password").toString(); }
QStringList subLanguages();
// "ask" = show found subtitles to pick from (BSPlayer style), "auto" = load best, "off"
inline QString subsMode() { return value("subs/mode", value("subs/auto", true).toBool() ? "ask" : "off").toString(); }
inline bool autoSubs() { return subsMode() != "off"; }
inline bool subsNextToVideo() { return value("subs/nextToVideo", true).toBool(); }

// Playback
inline bool hwdec() { return value("video/hwdec", true).toBool(); }
inline bool resume() { return value("video/resume", true).toBool(); }
inline bool autoplayNext() { return value("video/autoplayNext", true).toBool(); }
inline double volume() { return value("video/volume", 100.0).toDouble(); }
inline bool repeat() { return value("video/repeat", false).toBool(); } // loop the current video

// Images
inline int slideshowSeconds() { return value("image/slideshowSeconds", 4).toInt(); }
inline bool slideshowShuffle() { return value("image/slideshowShuffle", false).toBool(); }
inline bool slideshowFade() { return value("image/slideshowFade", true).toBool(); }
inline int jpegQuality() { return value("image/jpegQuality", 92).toInt(); }
inline bool showThumbs() { return value("image/showThumbs", true).toBool(); }
inline bool showExif() { return value("image/showExif", false).toBool(); }
inline int sortMode() { return value("image/sortMode", 0).toInt(); }

} // namespace Settings
