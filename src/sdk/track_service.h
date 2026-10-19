#pragma once
// Song metadata (GET song detail for a list of ids), typed as Track.
// One implementation owns batching, the response parse, and failure
// classification for every caller (Store, ListenTogether, DownloadManager).
#include "sdk/track.h"

#include <QList>
#include <functional>

class NcmClient;
class QObject;

namespace ncm::sdk {

// Pure parser for one response body: songs[]/privileges[] -> Tracks.
Result<QList<Track>> parseSongDetails(const QJsonObject &root);

using SongDetailCallback = std::function<void(Result<QList<Track>>)>;
// Fetches every positive id, batch by batch, and calls back exactly once with
// the combined list. Any failed batch fails the whole result; non-positive ids
// are skipped without a request. A gone context object drops the callback.
void fetchSongDetails(NcmClient &client, const QList<qint64> &songIds, bool personal,
                      QObject *context, SongDetailCallback callback);

} // namespace ncm::sdk
