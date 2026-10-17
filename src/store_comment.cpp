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

void Store::applyCommentLike(double songId, double commentId, bool liked, int delta)
{
    QVariantMap hot = m_data.value("comments").toMap();
    if (hot.value("songId").toDouble() == songId) {
        QVariantList items = hot.value("items").toList();
        if (patchItems(items, commentId, liked, delta)) {
            hot.insert("items", items);
            put("comments", hot, false);
        }
    }
    const QVariant rawPage = m_data.value("commentPage");
    if (rawPage.isValid() && !rawPage.isNull()) {
        QVariantMap page = rawPage.toMap();
        if (page.value("songId").toDouble() == songId) {
            QVariantList items = page.value("items").toList();
            if (patchItems(items, commentId, liked, delta)) {
                page.insert("items", items);
                put("commentPage", page, false);
            }
        }
    }
}

void Store::setCommentLiked(double songId, double commentId, bool liked)
{
    if (!m_client->hasMusicU()) {
        emit commentLikeFailed(songId, commentId, QStringLiteral("登录后才能点赞评论"));
        return;
    }
    const QString pendingKey = num(songId) + u':' + num(commentId);
    const auto existing = m_commentLikes.find(pendingKey);
    if (existing != m_commentLikes.end()) {
        if (existing->desired == liked)
            return;
        const bool oldDesired = existing->desired;
        existing->desired = liked;
        // 只调整计数一次：false→true +1，true→false -1
        const int delta = (liked ? 1 : 0) - (oldDesired ? 1 : 0);
        if (delta != 0) {
            applyCommentLike(songId, commentId, liked, delta);
        }
        if (!existing->inFlight)
            debounceWrite(QStringLiteral("comment:") + pendingKey,
                          [this, pendingKey] { flushCommentLike(pendingKey); });
        return;
    }

    // 评论列表已经加载时以服务器状态为准；CLI/异常路径不知道状态时也不能静默不做事。
    const bool serverState = commentLiked(songId, commentId).value_or(!liked);
    if (serverState == liked)
        return;
    m_commentLikes.insert(pendingKey, CommentLikeState{serverState, liked, false});
    // 乐观更新：热评单槽与分页单槽都可能包含这条评论
    applyCommentLike(songId, commentId, liked, liked ? 1 : -1);
    debounceWrite(QStringLiteral("comment:") + pendingKey,
                  [this, pendingKey] { flushCommentLike(pendingKey); });
}

void Store::sendCommentLike(const QString &key, double songId, double commentId, bool liked)
{
    const quint64 epoch = m_epoch;
    const QString thread = QStringLiteral("R_SO_4_%1").arg(num(songId));
    m_client->postWeapi(liked ? QStringLiteral("v1/comment/like") : QStringLiteral("v1/comment/unlike"),
                        QJsonObject{{"threadId", thread}, {"commentId", num(commentId)}},
                        true,
                        [this, key, songId, commentId, liked, epoch](const NcmClient::Response &response) {
        if (epoch != m_epoch)
            return;
        const auto state = m_commentLikes.find(key);
        if (state == m_commentLikes.end())
            return;
        const QString failure = requestFailure(response);
        if (!failure.isEmpty()) {
            const int delta = (state->serverState ? 1 : 0) - (state->desired ? 1 : 0);
            if (delta != 0)
                applyCommentLike(songId, commentId, state->serverState, delta);
            m_commentLikes.erase(state);
            emit commentLikeFailed(songId, commentId, failure);
            return;
        }
        state->serverState = liked;
        state->inFlight = false;
        if (state->desired != liked) {
            debounceWrite(key, [this, key] { flushCommentLike(key); });
            return;
        }
        m_commentLikes.erase(state);
        emit commentLikeSucceeded(songId, commentId, liked);
    });
}

void Store::flushCommentLike(const QString &key)
{
    auto it = m_commentLikes.find(key);
    if (it == m_commentLikes.end() || it->inFlight)
        return;
    if (it->desired == it->serverState) {
        m_commentLikes.erase(it);
        return;
    }
    const QStringList parts = key.split(u':');
    if (parts.size() != 2)
        return;
    it->inFlight = true;
    sendCommentLike(key, parts.at(0).toDouble(), parts.at(1).toDouble(), it->desired);
}
