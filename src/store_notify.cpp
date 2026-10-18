// 通知计数：/api/pl/count。列表接口暂未归一化，前端先做角标与分类入口。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"

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
        const int msg = r.value("msg").toInt();
        const int comment = r.value("comment").toInt();
        const int event = r.value("event").toInt();
        const int follow = r.value("follow").toInt();
        const int notice = r.value("notice").toInt();
        const int forward = r.value("forward").toInt();
        put("notifications", QVariantMap{
            {"msg", msg}, {"comment", comment}, {"event", event}, {"follow", follow},
            {"notice", notice}, {"forward", forward},
            {"newProgramCount", r.value("newProgramCount").toInt()},
            {"total", msg + comment + event + follow + notice + forward},
        }, false);
    });
}
