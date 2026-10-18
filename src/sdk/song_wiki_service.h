#pragma once
// Fetch orchestration for Song Wiki. The transport is the app's NcmClient; this
// service owns the three endpoint calls and the typed parse result, so callers
// never combine JSON fragments themselves.
#include "sdk/sdk_result.h"
#include "sdk/song_wiki.h"

#include <QObject>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct SongWikiRequest {
    qint64 songId = 0;
};

using SongWikiCallback = std::function<void(Result<SongWiki>)>;

// Starts the three read-only requests. The callback is invoked exactly once on
// the Qt thread. `context` may be nullptr; when it is a QObject, a callback that
// would arrive after its destruction is dropped.
void fetchSongWiki(NcmClient &client, SongWikiRequest request, QObject *context,
                   SongWikiCallback callback);

} // namespace ncm::sdk
