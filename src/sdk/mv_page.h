#pragma once
// Typed MV detail SDK — the cached half of Store's MV state.
//
// Raw endpoint (POST, weapi transport):
//   v1/mv/detail    payload { id }
//
// Only the detail response is parsed here. The signed play URL is refreshed on
// every open and therefore stays in Store (`mvUrl:<id>`); it is deliberately
// not part of this model.
//
// Durations are normalized to whole seconds (data.duration / 1000, floored).
// data.publishTime is already a display string and is preserved verbatim.
// URLs stay raw — Store's boundary applies ncmSized(400). `subscribed` comes
// from root.subed, not from data.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QString>

namespace ncm::sdk {

struct MvDetail {
    qint64 id = 0;           // data.id
    QString name;            // data.name
    QString cover;           // data.cover (raw URL, may be empty)
    QString artist;          // data.artistName
    qint64 artistId = 0;     // data.artistId
    qint64 duration = 0;     // data.duration / 1000 (seconds, floored)
    qint64 playCount = 0;    // data.playCount
    qint64 subCount = 0;     // data.subCount
    qint64 shareCount = 0;   // data.shareCount
    qint64 commentCount = 0; // data.commentCount
    QString publishTime;     // data.publishTime (server-formatted string)
    QString description;     // data.desc
    bool subscribed = false; // root.subed
};

// Pure parser. `root` is the whole response ({ code, data, subed }). Callers
// are responsible for checking the envelope's HTTP status / business code.
// A missing, non-object or empty `data` object is a Parse error: MV detail has
// no meaningful empty page.
Result<MvDetail> parseMvDetail(const QJsonObject &root);

} // namespace ncm::sdk
