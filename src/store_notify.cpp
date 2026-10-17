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
    m_client->get(QStringLiteral("api/pl/count"), {}, true,
                  [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject r = body(doc);
        if (!ok || r.isEmpty()) {
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
