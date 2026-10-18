#include "sdk/mv_page.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <cmath>

namespace ncm::sdk {
namespace {

constexpr auto DetailPath = "v1/mv/detail";
constexpr auto DetailRoot = "mv.detail";

} // namespace

Result<MvDetail> parseMvDetail(const QJsonObject &root)
{
    const QString rootPath = QString::fromLatin1(DetailRoot);
    const QString dataPath = rootPath + QStringLiteral(".data");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<MvDetail>(QString::fromLatin1(DetailPath), rootReader, dataPath);
    if (!rootReader.has("data") || data.isEmpty())
        return missingFailure<MvDetail>(QString::fromLatin1(DetailPath), dataPath,
                                        QStringLiteral("object"));

    JsonReader reader(data, dataPath);
    MvDetail detail;
    detail.id = reader.integer("id");
    detail.name = reader.string("name");
    detail.cover = reader.string("cover");
    detail.artist = reader.string("artistName");
    detail.artistId = reader.integer("artistId");
    detail.duration =
        static_cast<qint64>(std::floor(reader.number("duration") / 1000.0));
    detail.playCount = reader.integer("playCount");
    detail.subCount = reader.integer("subCount");
    detail.shareCount = reader.integer("shareCount");
    detail.commentCount = reader.integer("commentCount");
    detail.publishTime = reader.string("publishTime");
    detail.description = reader.string("desc");
    if (!reader.ok())
        return parseFailure<MvDetail>(QString::fromLatin1(DetailPath), reader, dataPath);

    const QString subscribedPath = rootPath + QStringLiteral(".subed");
    JsonReader subscribedReader(root, rootPath);
    detail.subscribed = subscribedReader.boolean("subed");
    if (!subscribedReader.ok())
        return parseFailure<MvDetail>(QString::fromLatin1(DetailPath), subscribedReader,
                                      subscribedPath);
    return Result<MvDetail>::success(detail);
}

} // namespace ncm::sdk
