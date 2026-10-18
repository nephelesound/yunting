// 评论楼中楼：只读分页。点赞已有 setCommentLiked；抱一抱/回复写操作未接。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/comment.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

void Store::requestCommentReplies(double songId, double commentId, qint64 before)
{
    requestCommentRepliesFor(QStringLiteral("R_SO_4_%1").arg(num(songId)), songId,
                             QStringLiteral("songId"), QStringLiteral("commentReplies"),
                             QStringLiteral("commentReplies:%1:%2")
                                 .arg(num(songId), num(commentId)),
                             commentId, before);
}

void Store::requestPlaylistCommentReplies(double playlistId, double commentId, qint64 before)
{
    requestCommentRepliesFor(QStringLiteral("A_PL_0_%1").arg(num(playlistId)), playlistId,
                             QStringLiteral("playlistId"),
                             QStringLiteral("playlistCommentReplies"),
                             QStringLiteral("playlistCommentReplies:%1:%2")
                                 .arg(num(playlistId), num(commentId)),
                             commentId, before);
}

void Store::requestCommentRepliesFor(const QString &threadId, double ownerId,
                                     const QString &ownerKey, const QString &slot,
                                     const QString &failureKey, double commentId, qint64 before)
{
    if (!(ownerId > 0) || !(commentId > 0))
        return;
    const quint64 token = before > 0 ? m_requests.token(slot)
                                     : m_requests.begin(slot);
    m_client->postWeapi(QStringLiteral("resource/comment/floor/get"),
                        QJsonObject{{"parentCommentId", num(commentId)},
                                    {"threadId", threadId},
                                    {"time", QString::number(before)},
                                    {"limit", 20}},
                        true,
                        [this, ownerId, ownerKey, slot, failureKey, commentId, before, token]
                        (const QJsonDocument &doc, bool ok) {
        if (!m_requests.current(slot, token))
            return;
        const QJsonObject root = body(doc);
        if (!ok || root.value("code").toInt() != 200) {
            emit requestFailed(failureKey, QStringLiteral("暂时取不到评论回复"));
            return;
        }
        const auto parsed = sdk::parseCommentReplies(root);
        if (!parsed) {
            emit requestFailed(failureKey, QStringLiteral("暂时取不到评论回复"));
            return;
        }
        QVariantList items;
        if (before > 0) {
            const QVariant old = m_data.value(slot);
            if (old.toMap().value("commentId").toDouble() == commentId)
                items = old.toMap().value("items").toList();
        }
        for (const sdk::Comment &comment : parsed.value->comments)
            items.append(commentToVariantMap(comment));
        const qint64 next = parsed.value->comments.isEmpty()
                                ? before
                                : parsed.value->comments.last().time;
        put(slot.toUtf8().constData(), QVariantMap{
            {ownerKey, ownerId}, {"commentId", commentId}, {"before", next},
            {"more", parsed.value->more}, {"items", items},
        }, false);
    });
}
