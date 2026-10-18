#include "sdk/account_feed.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonValue>

#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto ToplistDetailPath = "api/toplist/detail";
constexpr auto HotSearchListPath = "api/hotsearchlist/get";
constexpr auto LikedIdsPath = "api/song/like/get";

constexpr qsizetype MaxToplists = 12;
constexpr qsizetype MaxHotSearches = 20;

QString fieldPath(const QString &objectPath, const char *key)
{
    return objectPath + QLatin1Char('.') + QLatin1String(key);
}

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

Result<ToplistPreview> previewFrom(const QJsonObject &object, const QString &path)
{
    const QString endpoint = QString::fromLatin1(ToplistDetailPath);
    JsonReader reader(object, path);
    ToplistPreview preview;
    preview.name = reader.string("first");
    if (!reader.ok())
        return parseFailure<ToplistPreview>(endpoint, reader, fieldPath(path, "first"));
    preview.artist = reader.string("second");
    if (!reader.ok())
        return parseFailure<ToplistPreview>(endpoint, reader, fieldPath(path, "second"));
    return Result<ToplistPreview>::success(preview);
}

Result<Toplist> toplistFrom(const QJsonObject &object, const QString &path)
{
    const QString endpoint = QString::fromLatin1(ToplistDetailPath);
    JsonReader reader(object, path);
    Toplist toplist;
    toplist.id = reader.number("id");
    if (!reader.ok())
        return parseFailure<Toplist>(endpoint, reader, fieldPath(path, "id"));
    toplist.name = reader.string("name");
    if (!reader.ok())
        return parseFailure<Toplist>(endpoint, reader, fieldPath(path, "name"));
    toplist.cover = reader.string("coverImgUrl");
    if (!reader.ok())
        return parseFailure<Toplist>(endpoint, reader, fieldPath(path, "coverImgUrl"));
    toplist.update = reader.string("updateFrequency");
    if (!reader.ok())
        return parseFailure<Toplist>(endpoint, reader, fieldPath(path, "updateFrequency"));
    toplist.count = reader.number("trackCount");
    if (!reader.ok())
        return parseFailure<Toplist>(endpoint, reader, fieldPath(path, "trackCount"));

    const QString tracksPath = fieldPath(path, "tracks");
    const QJsonArray tracks = reader.array("tracks");
    if (!reader.ok())
        return parseFailure<Toplist>(endpoint, reader, tracksPath);
    toplist.preview.reserve(tracks.size());
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        const QString trackPath = indexedPath(tracksPath, i);
        if (!tracks.at(i).isObject())
            return itemTypeFailure<Toplist>(endpoint, trackPath);
        const auto preview = previewFrom(tracks.at(i).toObject(), trackPath);
        if (!preview)
            return Result<Toplist>::failure(preview.error);
        toplist.preview.append(*preview.value);
    }
    return Result<Toplist>::success(toplist);
}

Result<HotSearch> hotSearchFrom(const QJsonObject &object, const QString &path)
{
    const QString endpoint = QString::fromLatin1(HotSearchListPath);
    JsonReader reader(object, path);
    HotSearch hot;
    hot.word = reader.string("searchWord");
    if (!reader.ok())
        return parseFailure<HotSearch>(endpoint, reader, fieldPath(path, "searchWord"));
    hot.note = reader.string("content");
    if (!reader.ok())
        return parseFailure<HotSearch>(endpoint, reader, fieldPath(path, "content"));
    hot.score = reader.number("score");
    if (!reader.ok())
        return parseFailure<HotSearch>(endpoint, reader, fieldPath(path, "score"));
    return Result<HotSearch>::success(hot);
}

} // namespace

Result<QList<Toplist>> parseToplists(const QJsonObject &root)
{
    const QString endpoint = QString::fromLatin1(ToplistDetailPath);
    const QString rootPath = QStringLiteral("toplists");
    const QString listPath = fieldPath(rootPath, "list");
    JsonReader reader(root, rootPath);
    const QJsonArray rows = reader.array("list");
    if (!reader.ok())
        return parseFailure<QList<Toplist>>(endpoint, reader, listPath);

    QList<Toplist> toplists;
    toplists.reserve(qMin(rows.size(), MaxToplists));
    for (qsizetype i = 0; i < rows.size() && i < MaxToplists; ++i) {
        const QString rowPath = indexedPath(listPath, i);
        if (!rows.at(i).isObject())
            return itemTypeFailure<QList<Toplist>>(endpoint, rowPath);
        const auto toplist = toplistFrom(rows.at(i).toObject(), rowPath);
        if (!toplist)
            return Result<QList<Toplist>>::failure(toplist.error);
        toplists.append(*toplist.value);
    }
    return Result<QList<Toplist>>::success(std::move(toplists));
}

Result<QList<HotSearch>> parseHotSearches(const QJsonObject &root)
{
    const QString endpoint = QString::fromLatin1(HotSearchListPath);
    const QString rootPath = QStringLiteral("hotSearches");
    const QString dataPath = fieldPath(rootPath, "data");
    JsonReader reader(root, rootPath);
    const QJsonArray rows = reader.array("data");
    if (!reader.ok())
        return parseFailure<QList<HotSearch>>(endpoint, reader, dataPath);

    QList<HotSearch> hotSearches;
    hotSearches.reserve(qMin(rows.size(), MaxHotSearches));
    for (qsizetype i = 0; i < rows.size() && i < MaxHotSearches; ++i) {
        const QString rowPath = indexedPath(dataPath, i);
        if (!rows.at(i).isObject())
            return itemTypeFailure<QList<HotSearch>>(endpoint, rowPath);
        const auto hot = hotSearchFrom(rows.at(i).toObject(), rowPath);
        if (!hot)
            return Result<QList<HotSearch>>::failure(hot.error);
        hotSearches.append(*hot.value);
    }
    return Result<QList<HotSearch>>::success(std::move(hotSearches));
}

Result<LikedIds> parseLikedIds(const QJsonObject &root)
{
    const QString endpoint = QString::fromLatin1(LikedIdsPath);
    const QString rootPath = QStringLiteral("likedIds");
    const QString idsPath = fieldPath(rootPath, "ids");
    JsonReader reader(root, rootPath);
    const QJsonArray values = reader.array("ids");
    if (!reader.ok())
        return parseFailure<LikedIds>(endpoint, reader, idsPath);

    LikedIds liked;
    liked.ids.reserve(values.size());
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!values.at(i).isDouble()) {
            const QString itemPath = indexedPath(idsPath, i);
            return Result<LikedIds>::failure(endpointError(
                ErrorKind::Parse, endpoint, itemPath + QStringLiteral(": expected number"),
                itemPath));
        }
        liked.ids.append(values.at(i).toDouble());
    }
    return Result<LikedIds>::success(std::move(liked));
}

} // namespace ncm::sdk
