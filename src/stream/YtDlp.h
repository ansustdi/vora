#pragma once

#include <QString>
#include <functional>

class QWidget;

// yt-dlp: resolves YouTube / Twitch / Kick (and ~1800 other sites) page URLs into media streams.
// mpv's built-in ytdl hook runs it; this namespace finds, downloads and updates the binary.
class QProcess;

namespace YtDlp {

QString managedPath();          // <AppData>/Vora/yt-dlp(.exe): single-file build (other OSes, old installs)
QString managedDirExe();        // <AppData>/Vora/yt-dlp/yt-dlp.exe: Windows folder build (starts ~4x faster)
QString find();                 // next to the player > folder build > single file > PATH; empty if missing
QString pathForMpv();           // find(), or managedPath() so a later download is picked up
QString rawOptions();           // extra yt-dlp options for mpv's ytdl-raw-options (JS runtime)

// Options every yt-dlp run of ours gets: UTF-8 output (Windows pipes default to the console
// code page, which mangles non-Latin titles/paths in --print) and the JS runtime for YouTube.
QStringList baseArgs();

// A QProcess that fails to start never emits finished(); route that into finished(-1) so
// every caller's cleanup and callback still run.
void finishOnStartFailure(QProcess *proc);

// True for page URLs that need yt-dlp (not direct media links, HLS/DASH manifests, rtsp://...).
bool needsResolver(const QString &url);

// Ensures yt-dlp is present, asking the user before downloading it. cb(true) when usable.
void ensure(QWidget *parent, std::function<void(bool ok)> cb);

// Our own copy older than a week is refreshed in the background (sites change often and old
// versions stop working): the folder build is re-downloaded and swapped, a single file runs -U.
void updateInBackground();

} // namespace YtDlp
