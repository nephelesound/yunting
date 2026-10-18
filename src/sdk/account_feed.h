#pragma once
// Account first-screen SDK — typed top-list cards, hot searches and liked ids.
//
// Raw endpoints (GET; the Store owns query parameters and the personal flag):
//   api/toplist/detail     list[] top-list cards, first 12 in server order
//   api/hotsearchlist/get  data[] hot search entries, first 20 in server order
//   api/song/like/get      ids[] every liked song id of the account
//
// Missing or null arrays are empty successes. A present array with the wrong
// type, a non-object row, or a present field of the wrong type is a Parse
// error; rows beyond the 12/20 cap are not inspected. Unknown fields are
// ignored. URLs stay raw — the Store boundary applies ncmSized(cover, 300).
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace ncm::sdk {

// One entry of a top-list's tracks[] preview.
struct ToplistPreview {
    QString name;   // tracks[].first
    QString artist; // tracks[].second
};

// list[] of /api/toplist/detail.
struct Toplist {
    double id = 0.0;                // id
    QString name;                   // name
    QString cover;                  // coverImgUrl (raw URL)
    QString update;                 // updateFrequency
    double count = 0.0;             // trackCount
    QList<ToplistPreview> preview;  // tracks[], in server order
};

// data[] of /api/hotsearchlist/get.
struct HotSearch {
    QString word;      // searchWord
    QString note;      // content
    double score = 0.0; // score
};

// ids[] of /api/song/like/get. Only JSON numbers are accepted; strings are not
// coerced (a string id would otherwise become 0).
struct LikedIds {
    QList<double> ids; // ids[], in server order
};

// Pure parsers, exposed for tests and callers that already hold a response.
Result<QList<Toplist>> parseToplists(const QJsonObject &root);
Result<QList<HotSearch>> parseHotSearches(const QJsonObject &root);
Result<LikedIds> parseLikedIds(const QJsonObject &root);

} // namespace ncm::sdk
