#pragma once
// Default-search SDK — the keyword the search box should show when empty.
//
// Raw endpoint:
//   GET /api/search/defaultkeyword/get   (anonymous)
//
// data is a single object; `realkeyword` is the actual query, `showKeyword` is
// the display phrase (they can differ, e.g. a curated campaign). URLs stay raw.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QString>

namespace ncm::sdk {

struct DefaultSearch {
    QString showKeyword;  // data.showKeyword
    QString realKeyword;  // data.realkeyword
    int searchType = 0;   // data.searchType
    int action = 0;       // data.action
    QString alg;          // data.alg
    QString imageUrl;     // data.imageUrl (raw)
};

Result<DefaultSearch> parseDefaultSearch(const QJsonObject &root);

} // namespace ncm::sdk
