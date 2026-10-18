#include "sdk/dynamic_cover.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto DynamicCoverPath = "api/songplay/dynamic-cover";

QString firstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString() && !value.toString().isEmpty())
            return value.toString();
    }
    return {};
}


} // namespace

Result<DynamicCover> parseDynamicCover(const QJsonObject &root, qint64 songId)
{
    if (songId <= 0)
        return Result<DynamicCover>::failure(
            endpointError(ErrorKind::InvalidRequest, DynamicCoverPath,
                          QStringLiteral("songId must be positive")));

    JsonReader reader(root, QStringLiteral("dynamicCover"));
    const QJsonObject data = reader.object("data");
    if (!reader.ok())
        return Result<DynamicCover>::failure(
            endpointError(ErrorKind::Parse, DynamicCoverPath, reader.error(),
                          QStringLiteral("dynamicCover.data")));

    DynamicCover cover;
    cover.songId = songId;
    if (data.isEmpty())
        return Result<DynamicCover>::success(cover);

    cover.url = firstString(data, {"videoPlayUrl", "url", "dynamicCoverUrl",
                                   "dynamicCoverVideoUrl", "videoUrl"});
    cover.coverUrl = firstString(data, {"coverUrl", "picUrl"});
    cover.format = firstString(data, {"format", "type"});

    JsonReader dataReader(data, QStringLiteral("dynamicCover.data"));
    cover.width = static_cast<int>(dataReader.integer("width"));
    if (cover.width == 0)
        cover.width = static_cast<int>(dataReader.integer("weight")); // upstream typo
    cover.height = static_cast<int>(dataReader.integer("height"));
    cover.size = dataReader.integer("size");
    cover.needTransition = dataReader.boolean("needTransition");
    if (!dataReader.ok())
        return Result<DynamicCover>::failure(
            endpointError(ErrorKind::Parse, DynamicCoverPath, dataReader.error(),
                          QStringLiteral("dynamicCover.data")));
    cover.available = !cover.url.isEmpty();
    return Result<DynamicCover>::success(cover);
}

void fetchDynamicCover(NcmClient &client, qint64 songId, QObject *context,
                       DynamicCoverCallback callback)
{
    if (!callback)
        return;
    if (songId <= 0) {
        callback(Result<DynamicCover>::failure(
            endpointError(ErrorKind::InvalidRequest, DynamicCoverPath,
                          QStringLiteral("songId must be positive"))));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("songId"), QString::number(songId));
    postFormJson(client, context, QString::fromLatin1(DynamicCoverPath), form,
                 [callback = std::move(callback), songId](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<DynamicCover>::failure(root.error));
            return;
        }
        callback(parseDynamicCover(*root.value, songId));
    }, true);
}

} // namespace ncm::sdk
