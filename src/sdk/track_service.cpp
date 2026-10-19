#include "sdk/track_service.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QUrlQuery>
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto SongDetailPath = "api/v3/song/detail";

QString songDetailEndpoint()
{
    return QString::fromLatin1(SongDetailPath);
}

} // namespace

Result<QList<Track>> parseSongDetails(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("songDetail"));
    const QJsonArray songs = reader.array("songs");
    const QJsonArray privileges = reader.array("privileges");
    if (!reader.ok())
        return Result<QList<Track>>::failure(
            endpointError(ErrorKind::Parse, songDetailEndpoint(), reader.error(), reader.path()));
    // 详情接口必须回 songs；缺了就是坏包，不能当成「这首不存在」悄悄成功
    if (!reader.has("songs"))
        return missingFailure<QList<Track>>(songDetailEndpoint(),
                                            QStringLiteral("songDetail.songs"));
    return parseTracks(songs, privileges);
}

void fetchSongDetails(NcmClient &client, const QList<qint64> &songIds, bool personal,
                      QObject *context, SongDetailCallback callback)
{
    if (!callback)
        return;

    QList<qint64> ids;
    ids.reserve(songIds.size());
    for (qint64 id : songIds)
        if (id > 0)
            ids.append(id);

    if (ids.isEmpty()) {
        callback(Result<QList<Track>>::success(QList<Track>{}));
        return;
    }

    struct State {
        QList<Track> tracks;
        int remaining = 0;
        std::optional<Error> failure;
    };
    const auto state = std::make_shared<State>();

    // GET 的 URL 长度有限：300 个 id（约 6KB）稳定，350 会回 400。分批更小，失败也能收尾。
    constexpr int Batch = 250;
    state->remaining = static_cast<int>((ids.size() + Batch - 1) / Batch);
    for (qsizetype start = 0; start < ids.size(); start += Batch) {
        const qsizetype end = std::min<qsizetype>(ids.size(), start + Batch);
        QStringList fields;
        fields.reserve(end - start);
        for (qsizetype i = start; i < end; ++i)
            fields.append(QStringLiteral("{\"id\":%1}").arg(ids.at(i)));
        const QString c = QLatin1Char('[') + fields.join(u',') + QLatin1Char(']');

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("c"), c);
        getJson(client, context, songDetailEndpoint(), query, personal,
                [state, callback](Result<QJsonObject> root) {
            if (!root) {
                if (!state->failure)
                    state->failure = root.error;
            } else if (const auto parsed = parseSongDetails(*root.value); !parsed) {
                if (!state->failure)
                    state->failure = parsed.error;
            } else {
                state->tracks.append(*parsed.value);
            }

            if (--state->remaining != 0)
                return;
            if (state->failure)
                callback(Result<QList<Track>>::failure(*state->failure));
            else
                callback(Result<QList<Track>>::success(std::move(state->tracks)));
        });
    }
}

} // namespace ncm::sdk
