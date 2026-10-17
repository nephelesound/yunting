// 听歌足迹：总时长、周/月实时报告、歌曲播放排行。全部只读 GET，不经缓存。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QJsonDocument>
#include <QJsonObject>

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
        const QJsonObject data = body(doc).value("data").toObject();
        if (data.isEmpty()) {
            emit requestFailed(QStringLiteral("listen:total"), QStringLiteral("暂时取不到听歌统计"));
            return;
        }
        put("listenTotal", QVariantMap{{"seconds", data.value("totalDuration").toDouble()}}, false);
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
        const QJsonObject data = body(doc).value("data").toObject();
        if (data.isEmpty()) {
            emit requestFailed(QStringLiteral("listen:") + kind, QStringLiteral("暂时取不到听歌统计"));
            return;
        }
        put("listenRealtime", QVariantMap{
            {"type", kind},
            {"startTime", data.value("startTime").toDouble()},
            {"endTime", data.value("endTime").toDouble()},
            {"userNickName", data.value("userNickName").toString()},
            {"listenTimeDistribution", data.value("listenTimeDistributionBlock").toObject()},
            {"weekFriends", data.value("weekFriendsListenBlock").toObject()},
            {"weekToday", data.value("weekTodayListenBlock").toObject()},
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
    m_client->get(QStringLiteral("api/content/activity/listen/data/song/play/rank"),
                  query({{"type", kind}}), true,
                  [this, kind, epoch](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("listenRank:") + kind, failure);
            return;
        }
        const QJsonObject data = body(doc).value("data").toObject();
        if (data.isEmpty()) {
            emit requestFailed(QStringLiteral("listenRank:") + kind, QStringLiteral("暂时取不到播放排行"));
            return;
        }
        QVariantList items;
        for (const QJsonValue &v : data.value("songItems").toArray()) {
            const QJsonObject o = v.toObject();
            QVariantList artists;
            for (const QJsonValue &a : o.value("artists").toArray()) {
                const QJsonObject ao = a.toObject();
                artists.append(QVariantMap{{"id", ao.value("artistId").toDouble()},
                                           {"name", ao.value("artistName").toString()}});
            }
            items.append(QVariantMap{
                {"id", o.value("songId").toDouble()},
                {"name", o.value("songName").toString()},
                {"album", o.value("albumName").toString()},
                {"cover", ncmSized(o.value("picUrl"), 120)},
                {"playCount", o.value("playCount").toDouble()},
                {"artists", artists},
            });
        }
        put("listenRank", QVariantMap{
            {"type", kind},
            {"startTime", data.value("startTime").toDouble()},
            {"endTime", data.value("endTime").toDouble()},
            {"songCount", data.value("songCount").toDouble()},
            {"items", items},
        }, false);
    });
}
