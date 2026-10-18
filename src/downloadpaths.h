#pragma once
// Offline downloads live outside the cache so clearing the query cache does not
// delete the user's audio. Both the downloader and AudioEngine use this path.
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

inline QString yuntingDownloadsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/downloads");
}

// A completed file is named <songId>.<ext>; .part files are never returned.
inline QString yuntingDownloadedFile(qint64 songId)
{
    if (songId <= 0)
        return {};
    const QDir dir(yuntingDownloadsDir());
    const QStringList names = dir.entryList(
        QStringList{QString::number(songId) + QStringLiteral(".*")}, QDir::Files);
    for (const QString &name : names) {
        if (!name.endsWith(QLatin1String(".part")))
            return dir.filePath(name);
    }
    return {};
}
