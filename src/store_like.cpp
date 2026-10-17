// Store 的红心与曲目查找：乐观更新、在途意图排队、失败回滚、最近播放。
#include "store.h"

#include "json_util.h"

#include <QJsonDocument>

#include <algorithm>

using namespace ncm;

// 与歌单/评论不同，失败不需要用户重试按钮：下一次点击会重新发。
void Store::setLiked(double songId, bool liked)
{
    if (!m_client->hasMusicU()) {
        emit likeFailed(songId, liked, QStringLiteral("登录后才能同步红心"));
        return;
    }

    const auto pending = m_likes.find(songId);
    if (pending != m_likes.end()) {
        // 请求在途：不丢用户的新意图，记下来，回包落地后追发
        if (pending->desired == liked)
            return;
        pending->desired = liked;
        ++m_likeGeneration;
        m_likeTouched.insert(songId);
        setLikedLocal(songId, liked);
        return;
    }

    const bool currentlyLiked = isLiked(songId);
    if (currentlyLiked == liked) {
        emit likeSucceeded(songId, liked);
        return;
    }

    m_likes.insert(songId, LikeState{currentlyLiked, liked});
    ++m_likeGeneration;
    m_likeTouched.insert(songId);
    setLikedLocal(songId, liked);
    sendLike(songId, liked);
}

void Store::sendLike(double songId, bool liked)
{
    const quint64 epoch = m_epoch;
    m_client->postWeapi("radio/like",
                        QJsonObject{{"alg", QStringLiteral("itembased")},
                                    {"trackId", num(songId)},
                                    {"like", liked ? QStringLiteral("true") : QStringLiteral("false")},
                                    {"time", QStringLiteral("25")}},
                        true,
                        [this, songId, liked, epoch](const QJsonDocument &doc, bool ok) {
        const auto pending = m_likes.find(songId);
        if (epoch != m_epoch) {   // 期间退出登录：不要把回包写到新会话
            if (pending != m_likes.end())
                m_likes.erase(pending);
            return;
        }
        if (pending == m_likes.end())
            return;
        const int code = doc.object().value("code").toInt();
        if (ok && code == 200) {
            pending->serverState = liked;
            if (pending->desired != liked) {
                sendLike(songId, pending->desired);   // 用户在回包前又点了一次
                return;
            }
            const bool finalState = pending->desired;
            m_likes.erase(pending);
            if (finalState)
                addLikedTrack(songId);
            else
                removeLikedTrack(songId);
            emit likeSucceeded(songId, finalState);
            return;
        }

        // 失败：退回服务器确认过的状态（可能是同一批里的上一次成功），丢弃排队的意图。
        const bool serverState = pending->serverState;
        const bool desired = pending->desired;
        m_likes.erase(pending);
        setLikedLocal(songId, serverState);
        if (code == 301)
            setSessionExpired(true);

        // 接口的 message 已经是给用户看的中文（例如「操作频繁，请稍候再试」），优先用它。
        const QString serverMessage = doc.object().value("message").toString();
        const QString reason = !ok ? QStringLiteral("网络不可用，请稍后再试")
            : !serverMessage.isEmpty() ? serverMessage
            : code == 301 ? QStringLiteral("登录已过期，请重新登录")
            : code == 250 ? QStringLiteral("这首歌暂时不能收藏")
                          : QStringLiteral("服务器没有接受这次操作");
        emit likeFailed(songId, desired, reason);
        qWarning() << "like failed:" << songId << code;
    });
}

// 乐观写入：保持原有顺序，过滤掉这首歌，目标为喜欢时追加到末尾。
void Store::setLikedLocal(double songId, bool liked)
{
    if (isLiked(songId) == liked)
        return;

    QVariantList ids = m_personal.value("likedIds").toList();
    for (qsizetype i = ids.size() - 1; i >= 0; --i)
        if (ids.at(i).toDouble() == songId)
            ids.removeAt(i);
    if (liked)
        ids.append(songId);
    put("likedIds", ids, true);

    const QVariant raw = m_personal.value("liked");
    if (!raw.isValid() || raw.isNull())
        return;
    QVariantMap likedMap = raw.toMap();
    likedMap.insert("count", std::max(0.0, likedMap.value("count").toDouble() + (liked ? 1 : -1)));
    put("liked", likedMap, true);
}

bool Store::isLiked(double songId) const
{
    for (const QVariant &v : m_personal.value("likedIds").toList())
        if (v.toDouble() == songId)
            return true;
    return false;
}

void Store::notePlayed(const QVariantMap &track)
{
    const QVariant raw = m_personal.value("recent");
    if (track.isEmpty() || !raw.isValid() || raw.isNull())
        return;
    QVariantMap recent = raw.toMap();
    const double id = track.value("id").toDouble();
    QVariantList tracks = recent.value("tracks").toList();
    for (qsizetype i = tracks.size() - 1; i >= 0; --i)
        if (tracks.at(i).toMap().value("id").toDouble() == id)
            tracks.removeAt(i);
    tracks.prepend(track);
    recent.insert("tracks", tracks);
    recent.insert("total", std::max(recent.value("total").toDouble(), double(tracks.size())));
    put("recent", recent, true);
}

QVariantMap Store::trackById(double songId) const
{
    const auto in = [songId](const QVariantList &list) -> QVariantMap {
        for (const QVariant &v : list) {
            const QVariantMap track = v.toMap();
            if (track.value("id").toDouble() == songId)
                return track;
        }
        return {};
    };
    QVariantMap found = in(m_personal.value("recent").toMap().value("tracks").toList());
    if (!found.isEmpty())
        return found;
    found = in(m_data.value("tracks").toList());
    if (!found.isEmpty())
        return found;
    found = in(m_data.value("search").toMap().value("songs").toList());
    if (!found.isEmpty())
        return found;
    for (const char *key : {"daily", "heart", "radar", "similar", "fm"}) {
        found = in(m_personal.value(QLatin1String(key)).toMap().value("tracks").toList());
        if (!found.isEmpty())
            return found;
    }
    const QVariantMap details = m_personal.value("playlistDetails").toMap();
    for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
        found = in(it.value().toMap().value("tracks").toList());
        if (!found.isEmpty())
            return found;
    }
    return {};
}

void Store::addLikedTrack(double songId)
{
    const QVariant raw = m_personal.value("liked");
    if (!raw.isValid() || raw.isNull())
        return;
    QVariantMap liked = raw.toMap();
    QVariantList tracks = liked.value("tracks").toList();
    for (const QVariant &v : tracks)
        if (v.toMap().value("id").toDouble() == songId)
            return;
    const QVariantMap known = trackById(songId);
    if (!known.isEmpty()) {
        tracks.prepend(known);
        liked.insert("tracks", tracks);
        put("liked", liked, true);
        return;
    }
    // 本地没有这首歌（比如从播放条直接红心）：取一次详情再插到列表首位
    const quint64 epoch = m_epoch;
    fetchSongDetails({songId}, true,
                     [this, songId, epoch](const QHash<double, QVariantMap> &details, bool ok) {
        // 详情回来前用户可能已经取消红心（甚至退出登录）：那时不能再把它插回去
        if (epoch != m_epoch || !isLiked(songId))
            return;
        if (!ok || !details.contains(songId))
            return;
        const QVariant raw = m_personal.value("liked");
        if (!raw.isValid() || raw.isNull())
            return;
        QVariantMap liked = raw.toMap();
        QVariantList tracks = liked.value("tracks").toList();
        for (const QVariant &v : tracks)
            if (v.toMap().value("id").toDouble() == songId)
                return;
        tracks.prepend(details.value(songId));
        liked.insert("tracks", tracks);
        put("liked", liked, true);
    });
}

void Store::removeLikedTrack(double songId)
{
    const QVariant raw = m_personal.value("liked");
    if (!raw.isValid() || raw.isNull())
        return;
    QVariantMap liked = raw.toMap();
    QVariantList tracks = liked.value("tracks").toList();
    for (qsizetype i = tracks.size() - 1; i >= 0; --i)
        if (tracks.at(i).toMap().value("id").toDouble() == songId)
            tracks.removeAt(i);
    liked.insert("tracks", tracks);
    put("liked", liked, true);
}
