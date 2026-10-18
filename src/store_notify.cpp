// 通知计数：/api/pl/count。SDK 负责 typed 解析，这里只做缓存与 QML 兼容形状。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "sdk/notify.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

void Store::requestNotifications()
{
    if (!m_client->hasMusicU())
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = m_requests.begin(QStringLiteral("notifications"));
    m_client->get(QStringLiteral("api/pl/count"), {}, true,
                  [this, epoch, token](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("notifications"), token))
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("notifications"), failure);
            return;
        }
        const QJsonObject r = body(doc);
        if (r.isEmpty()) {
            emit requestFailed(QStringLiteral("notifications"), QStringLiteral("暂时取不到通知计数"));
            return;
        }
        const auto parsed = sdk::parseNotificationCount(r);
        if (!parsed) {
            emit requestFailed(QStringLiteral("notifications"),
                               QStringLiteral("暂时取不到通知计数"));
            return;
        }
        const sdk::NotificationCount &count = *parsed.value;
        const int total = count.msg + count.comment + count.event + count.follow
                        + count.notice + count.forward;
        put("notifications", QVariantMap{
            {"msg", count.msg}, {"comment", count.comment}, {"event", count.event},
            {"follow", count.follow}, {"notice", count.notice}, {"forward", count.forward},
            {"newProgramCount", count.newProgramCount}, {"total", total},
        }, false);
    });
}
