// 评论楼中楼：只读分页。点赞已有 setCommentLiked；抱一抱/回复写操作未接。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

void Store::requestCommentReplies(double songId, double commentId, qint64 before)
{
    if (!(songId > 0) || !(commentId > 0))
        return;
    const quint64 token = before > 0 ? m_requests.token(QStringLiteral("commentReplies"))
                                     : m_requests.begin(QStringLiteral("commentReplies"));
    const QString thread = QStringLiteral("R_SO_4_%1").arg(num(songId));
    m_client->postWeapi(QStringLiteral("resource/comment/floor/get"),
                        QJsonObject{{"parentCommentId", num(commentId)},
                                    {"threadId", thread},
                                    {"time", QString::number(before)},
                                    {"limit", 20}},
                        true,
                        [this, songId, commentId, before, token](const QJsonDocument &doc, bool ok) {
        if (!m_requests.current(QStringLiteral("commentReplies"), token))
            return;
        const QJsonObject root = body(doc);
        if (!ok || root.value("code").toInt() != 200) {
            // 按请求 key 报错：界面按 commentReplies:<songId>:<commentId> 找到这一槽并重试
            emit requestFailed(QStringLiteral("commentReplies:%1:%2").arg(num(songId), num(commentId)),
                               QStringLiteral("暂时取不到评论回复"));
            return;
        }
        QJsonArray source = root.value("data").toObject().value("comments").toArray();
        if (source.isEmpty())
            source = root.value("comments").toArray();
        QVariantList items;
        if (before > 0) {
            const QVariant old = m_data.value("commentReplies");
            if (old.toMap().value("commentId").toDouble() == commentId)
                items = old.toMap().value("items").toList();
        }
        for (const QJsonValue &v : source)
            items.append(ncmComment(v.toObject()));
        const qint64 next = source.isEmpty() ? before
                                             : qint64(source.last().toObject().value("time").toDouble());
        put("commentReplies", QVariantMap{
            {"songId", songId}, {"commentId", commentId}, {"before", next},
            {"more", root.value("more").toBool()}, {"items", items},
        }, false);
    });
}
