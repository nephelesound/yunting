#pragma once
// Song download SDK — signed download URL + file metadata.
//
// Raw endpoint (POST form, plaintext /api, personal MUSIC_U):
//   api/song/enhance/download/url/v1   id, level
//
// The URL is time-limited and may be a different level than requested (VIP /
// rights). `type` is the container suffix (mp3/flac/...), `size` is bytes.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct SongDownload {
    qint64 id = 0;     // data.id
    QString url;       // data.url (raw signed URL)
    qint64 size = 0;    // data.size
    qint64 br = 0;      // data.br
    QString md5;        // data.md5
    QString type;       // data.type (file extension)
    QString level;      // data.level (actual level)
    qint64 expires = 0; // data.expi seconds (0 if absent)
};

using SongDownloadCallback = std::function<void(Result<SongDownload>)>;

void fetchSongDownload(NcmClient &client, qint64 songId, QString level,
                       QObject *context, SongDownloadCallback callback);

Result<SongDownload> parseSongDownload(const QJsonObject &root);

} // namespace ncm::sdk
