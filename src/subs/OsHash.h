#pragma once

#include <QString>

// OpenSubtitles movie hash: file size + 64-bit sums of the first and last 64 KiB.
// Returns empty string if the file is unreadable or smaller than 128 KiB.
QString openSubtitlesHash(const QString &path);
