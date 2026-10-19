#include "sdk/search_default.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

namespace ncm::sdk {

Result<DefaultSearch> parseDefaultSearch(const QJsonObject &root)
{
    const QString endpoint = QStringLiteral("api/search/defaultkeyword/get");
    const QString rootPath = QStringLiteral("defaultSearch");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<DefaultSearch>(endpoint, rootReader, rootPath + QStringLiteral(".data"));
    if (data.isEmpty())
        return missingFailure<DefaultSearch>(endpoint, rootPath + QStringLiteral(".data"),
                                             QStringLiteral("object"));

    JsonReader reader(data, rootPath + QStringLiteral(".data"));
    DefaultSearch out;
    out.showKeyword = reader.string("showKeyword");
    out.realKeyword = reader.string("realkeyword");
    out.searchType = static_cast<int>(reader.integer("searchType"));
    out.action = static_cast<int>(reader.integer("action"));
    out.alg = reader.string("alg");
    out.imageUrl = reader.string("imageUrl");
    if (!reader.ok())
        return parseFailure<DefaultSearch>(endpoint, reader,
                                           rootPath + QStringLiteral(".data"));
    return Result<DefaultSearch>::success(std::move(out));
}

} // namespace ncm::sdk
