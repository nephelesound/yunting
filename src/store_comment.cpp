// 评论点赞：weapi 写请求，不重试。先乐观改 comments / commentPage，失败回滚。
#include "store.h"

#include "json_util.h"

#include <algorithm>

using namespace ncm;

namespace {
bool patchItems(QVariantList &items, double commentId, bool liked, int delta)
{
    bool changed = false;
    for (QVariant &v : items) {
        QVariantMap c = v.toMap();
        if (c.value("id").toDouble() != commentId)
            continue;
        c.insert("liked", liked);
        c.insert("likes", std::max(0.0, c.value("likes").toDouble() + delta));
        v = c;
        changed = true;
    }
    return changed;
}
} // namespace

void Store::setCommentLiked(double songId, double commentId, bool liked)
{
    if (!m_client->hasMusicU()) {
        emit commentLikeFailed(songId, commentId, QStringLiteral("登录后才能点赞评论"));
        return;
    }
    const QString pendingKey = num(songId) + u':' + num(commentId);
    if (m_commentLikePending.contains(pendingKey))
        return;
    m_commentLikePending.insert(pendingKey);

    // 乐观更新：热评单槽与分页单槽都可能包含这条评论
    const auto apply = [this, songId, commentId](bool state, int delta) {
        QVariantMap hot = m_data.value("comments").toMap();
        if (hot.value("songId").toDouble() == songId) {
            QVariantList items = hot.value("items").toList();
            if (patchItems(items, commentId, state, delta)) {
                hot.insert("items", items);
                put("comments", hot, false);
            }
        }
        const QVariant rawPage = m_data.value("commentPage");
        if (rawPage.isValid() && !rawPage.isNull()) {
            QVariantMap page = rawPage.toMap();
            if (page.value("songId").toDouble() == songId) {
                QVariantList items = page.value("items").toList();
                if (patchItems(items, commentId, state, delta)) {
                    page.insert("items", items);
                    put("commentPage", page, false);
                }
            }
        }
    };
    apply(liked, liked ? 1 : -1);

    const quint64 epoch = m_epoch;
    const QString thread = QStringLiteral("R_SO_4_%1").arg(num(songId));
    m_client->postWeapi(liked ? QStringLiteral("v1/comment/like") : QStringLiteral("v1/comment/unlike"),
                        QJsonObject{{"threadId", thread}, {"commentId", num(commentId)}},
                        true,
                        [this, songId, commentId, liked, epoch, pendingKey, apply](const NcmClient::Response &response) {
        m_commentLikePending.remove(pendingKey);
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(response);
        if (failure.isEmpty())
            return;
        // 失败：把刚才的乐观改动静默改回，并让界面说明原因
        apply(!liked, liked ? -1 : 1);
        emit commentLikeFailed(songId, commentId, failure);
    });
}
