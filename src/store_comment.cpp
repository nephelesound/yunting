// 评论点赞：weapi 写请求，不重试。先乐观改该线程的热评 / 分页槽，失败回滚。
#include "store.h"

#include "json_util.h"
#include "sdk/comment.h"

#include <algorithm>

using namespace ncm;

namespace {
// QML 的线程种类 → 数据槽、owner 字段与 NetEase 资源类型。
struct ThreadSpec {
    const char *kind;
    const char *hot;
    const char *page;
    const char *owner;
    sdk::CommentThreadKind wireKind;
};

const ThreadSpec *threadSpec(const QString &kind)
{
    static const ThreadSpec specs[] = {
        {"song", "comments", "commentPage", "songId", sdk::CommentThreadKind::Song},
        {"playlist", "playlistComments", "playlistCommentPage", "playlistId",
         sdk::CommentThreadKind::Playlist},
    };
    for (const ThreadSpec &spec : specs)
        if (kind == QLatin1String(spec.kind))
            return &spec;
    return nullptr;
}

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

void Store::applyCommentLike(const QString &kind, double ownerId, double commentId,
                             bool liked, int delta)
{
    const ThreadSpec *spec = threadSpec(kind);
    if (!spec)
        return;
    // 热评单槽与分页单槽都可能包含这条评论
    for (const char *slot : {spec->hot, spec->page}) {
        const QVariant raw = m_data.value(QString::fromLatin1(slot));
        if (raw.isNull())
            continue;
        QVariantMap data = raw.toMap();
        if (data.value(QString::fromLatin1(spec->owner)).toDouble() != ownerId)
            continue;
        QVariantList items = data.value("items").toList();
        if (patchItems(items, commentId, liked, delta)) {
            data.insert("items", items);
            put(slot, data, false);
        }
    }
}

std::optional<bool> Store::commentLiked(const QString &kind, double ownerId,
                                        double commentId) const
{
    const ThreadSpec *spec = threadSpec(kind);
    if (!spec)
        return std::nullopt;
    const auto find = [commentId](const QVariantList &items) -> std::optional<bool> {
        for (const QVariant &v : items) {
            const QVariantMap c = v.toMap();
            if (c.value("id").toDouble() == commentId)
                return c.value("liked").toBool();
        }
        return std::nullopt;
    };
    for (const char *slot : {spec->hot, spec->page}) {
        const QVariantMap data = m_data.value(QString::fromLatin1(slot)).toMap();
        if (data.value(QString::fromLatin1(spec->owner)).toDouble() != ownerId)
            continue;
        if (const auto found = find(data.value("items").toList()))
            return found;
    }
    return std::nullopt;
}

void Store::setCommentLiked(const QString &kind, double ownerId, double commentId, bool liked)
{
    if (!threadSpec(kind) || !(ownerId > 0) || !(commentId > 0))
        return;
    if (!m_client->hasMusicU()) {
        emit commentLikeFailed(kind, ownerId, commentId, QStringLiteral("登录后才能点赞评论"));
        return;
    }
    const QString pendingKey = kind + u':' + num(ownerId) + u':' + num(commentId);
    const auto existing = m_commentLikes.find(pendingKey);
    if (existing != m_commentLikes.end()) {
        if (existing->desired == liked)
            return;
        const bool oldDesired = existing->desired;
        existing->desired = liked;
        // 只调整计数一次：false→true +1，true→false -1
        const int delta = (liked ? 1 : 0) - (oldDesired ? 1 : 0);
        if (delta != 0) {
            applyCommentLike(kind, ownerId, commentId, liked, delta);
        }
        if (!existing->inFlight)
            debounceWrite(QStringLiteral("comment:") + pendingKey,
                          [this, pendingKey] { flushCommentLike(pendingKey); });
        return;
    }

    // 评论列表已经加载时以服务器状态为准；CLI/异常路径不知道状态时也不能静默不做事。
    const bool serverState = commentLiked(kind, ownerId, commentId).value_or(!liked);
    if (serverState == liked)
        return;
    m_commentLikes.insert(pendingKey, CommentLikeState{serverState, liked, false});
    applyCommentLike(kind, ownerId, commentId, liked, liked ? 1 : -1);
    debounceWrite(QStringLiteral("comment:") + pendingKey,
                  [this, pendingKey] { flushCommentLike(pendingKey); });
}

void Store::sendCommentLike(const QString &key, const QString &kind, double ownerId,
                            double commentId, bool liked)
{
    const ThreadSpec *spec = threadSpec(kind);
    if (!spec)
        return;
    const quint64 epoch = m_epoch;
    sdk::setCommentLiked(*m_client, spec->wireKind, qlonglong(ownerId), qlonglong(commentId),
                         liked, this,
                         [this, key, kind, ownerId, commentId, liked, epoch]
                         (sdk::Result<sdk::CommentLikeState> result) {
        if (epoch != m_epoch)
            return;
        const auto state = m_commentLikes.find(key);
        if (state == m_commentLikes.end())
            return;
        const QString failure = result
                                    ? QString()
                                    : requestFailure(result.error,
                                                     QStringLiteral("暂时不能更新评论点赞"));
        if (!failure.isEmpty()) {
            const int delta = (state->serverState ? 1 : 0) - (state->desired ? 1 : 0);
            if (delta != 0)
                applyCommentLike(kind, ownerId, commentId, state->serverState, delta);
            m_commentLikes.erase(state);
            emit commentLikeFailed(kind, ownerId, commentId, failure);
            return;
        }
        state->serverState = liked;
        state->inFlight = false;
        if (state->desired != liked) {
            debounceWrite(QStringLiteral("comment:") + key,
                          [this, key] { flushCommentLike(key); });
            return;
        }
        m_commentLikes.erase(state);
        emit commentLikeSucceeded(kind, ownerId, commentId, liked);
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
    if (parts.size() != 3)
        return;
    it->inFlight = true;
    sendCommentLike(key, parts.at(0), parts.at(1).toDouble(), parts.at(2).toDouble(),
                    it->desired);
}
