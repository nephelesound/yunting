// 听歌足迹：总时长、周/月实时报告、歌曲播放排行。全部只读 GET，不经缓存。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/listen_data.h"

#include <QJsonDocument>

using namespace ncm;

void Store::requestListenTotal()
{
    if (!m_client->hasMusicU())
        return;
    const quint64 epoch = m_epoch;
    m_client->get(QStringLiteral("api/content/activity/listen/data/total"), {}, true,
                  [this, epoch](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("listen:total"), failure);
            return;
        }
        const auto parsed = sdk::parseListenTotal(body(doc));
        if (!parsed) {
            emit requestFailed(QStringLiteral("listen:total"), QStringLiteral("暂时取不到听歌统计"));
            return;
        }
        put("listenTotal", QVariantMap{{"seconds", static_cast<double>(parsed.value->seconds)}},
            false);
    });
}

void Store::requestListenRealtime(const QString &type)
{
    if (!m_client->hasMusicU())
        return;
    const QString kind = (type == QLatin1String("month")) ? QStringLiteral("month")
                                                          : QStringLiteral("week");
    const quint64 epoch = m_epoch;
    m_client->get(QStringLiteral("api/content/activity/listen/data/realtime/report"),
                  query({{"type", kind}}), true,
                  [this, kind, epoch](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("listen:") + kind, failure);
            return;
        }
        const auto parsed = sdk::parseListenRealtime(body(doc));
        if (!parsed) {
            emit requestFailed(QStringLiteral("listen:") + kind, QStringLiteral("暂时取不到听歌统计"));
            return;
        }
        const sdk::ListenRealtime &realtime = *parsed.value;
        put("listenRealtime", QVariantMap{
            {"type", kind},
            {"startTime", static_cast<double>(realtime.startTime)},
            {"endTime", static_cast<double>(realtime.endTime)},
            {"userNickName", realtime.userNickName},
            {"listenTimeDistribution", realtime.listenTimeDistribution},
            {"weekFriends", realtime.weekFriends},
            {"weekToday", realtime.weekToday},
        }, false);
    });
}

void Store::requestListenRank(const QString &type)
{
    if (!m_client->hasMusicU())
        return;
    const QString kind = (type == QLatin1String("week")) ? QStringLiteral("week")
                                                         : QStringLiteral("month");
    const quint64 epoch = m_epoch;
    // Week and month share one ledger slot: the latest tab wins, and a late
    // response for the previous tab must not overwrite the new tab's data.
    const QString slot = QStringLiteral("listenRank");
    const quint64 token = m_requests.begin(slot);
    m_client->get(QStringLiteral("api/content/activity/listen/data/song/play/rank"),
                  query({{"type", kind}}), true,
                  [this, kind, epoch, token, slot](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch || !m_requests.current(slot, token))
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("listenRank:") + kind, failure);
            return;
        }
        const auto parsed = sdk::parseListenRank(body(doc));
        if (!parsed) {
            emit requestFailed(QStringLiteral("listenRank:") + kind,
                               QStringLiteral("暂时取不到播放排行"));
            return;
        }
        const sdk::ListenRank &rank = *parsed.value;
        QVariantList items;
        items.reserve(rank.items.size());
        for (const sdk::ListenRankSong &song : rank.items) {
            QVariantList artists;
            artists.reserve(song.artists.size());
            for (const sdk::ListenRankArtist &artist : song.artists)
                artists.append(QVariantMap{{"id", static_cast<double>(artist.id)},
                                           {"name", artist.name}});
            items.append(QVariantMap{
                {"id", static_cast<double>(song.id)},
                {"name", song.name},
                {"album", song.album},
                {"cover", ncmSized(QJsonValue(song.cover), 120)},
                {"playCount", static_cast<double>(song.playCount)},
                {"artists", artists},
            });
        }
        put("listenRank", QVariantMap{
            {"type", kind},
            {"startTime", static_cast<double>(rank.startTime)},
            {"endTime", static_cast<double>(rank.endTime)},
            {"songCount", static_cast<double>(rank.songCount)},
            {"items", items},
        }, false);
    });
}
