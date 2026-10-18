#pragma once
// Dynamic cover SDK — typed data for the animated song cover.
//
// Raw endpoint (plaintext /api; requires a signed-in MUSIC_U for the real asset):
//   POST /api/songplay/dynamic-cover  form: songId
//
// The endpoint answers 301 to anonymous clients, so the service surfaces that as
// an Api error; a signed-in empty/null data object means the song has no dynamic
// cover and is a successful `available=false` result.
//
// Upstream uses more than one field spelling across clients. The parser accepts
// the aliases documented on each field and normalizes them into one model.
#include "sdk/sdk_result.h"

#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct DynamicCover {
    qint64 songId = 0;
    bool available = false;
    QString url;       // data.videoPlayUrl (live); data.url accepted as fallback
    int width = 0;     // data.width, fallback data.weight (upstream typo)
    int height = 0;    // data.height
    bool needTransition = false; // data.needTransition
};

using DynamicCoverCallback = std::function<void(Result<DynamicCover>)>;

void fetchDynamicCover(NcmClient &client, qint64 songId, QObject *context,
                       DynamicCoverCallback callback);

Result<DynamicCover> parseDynamicCover(const QJsonObject &root, qint64 songId);

} // namespace ncm::sdk
