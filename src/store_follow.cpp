// Store 的关注/收藏写操作：用户关注、歌手关注、专辑收藏。都是单发写请求，不重试。
#include "store.h"

#include "json_util.h"
#include "sdk/social.h"

#include <algorithm>

using namespace ncm;

void Store::followWrite(const QString &path, const QJsonObject &payload, const QString &action,
                        std::function<void()> onSuccess)
{
    if (!m_client->hasMusicU()) {
        emit followFailed(action, QStringLiteral("登录后才能操作"));
        return;
    }
    const quint64 epoch = m_epoch;
    m_client->postWeapi(path, payload, true,
                        [this, epoch, action, onSuccess](const NcmClient::Response &response) {
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(response);
        if (failure.isEmpty()) {
            onSuccess();
            return;
        }
        emit followFailed(action, failure);
    });
}

// 收藏列表（savedArtists / savedAlbums）只由取它的请求和这里写。在途的回包一律作废：它是改动前的状态。
// 取消：把这一项立刻移掉；新收藏：清掉整张列表，下次打开向服务器取（新的一项排在哪由服务器决定）
void Store::updateSaved(const char *slot, double id, bool subscribed)
{
    m_requests.begin(QLatin1String(slot));
    if (subscribed) {
        put(slot, QVariant::fromValue(nullptr), true);
        return;
    }
    QVariantMap saved = m_personal.value(QLatin1String(slot)).toMap();
    QVariantList kept;
    for (const QVariant &v : saved.value("items").toList()) {
        if (v.toMap().value("id").toDouble() != id)
            kept.append(v);
    }
    if (kept.size() == saved.value("items").toList().size())
        return;
    saved.insert("items", kept);
    saved.insert("count", std::max(0.0, saved.value("count").toDouble() - 1));
    put(slot, saved, true);
}

void Store::setArtistSubscribed(double id, bool subscribed)
{
    // 快速防抖：连续点关注/取消只发最后一次意图，避免连点触发风控。
    const QString action = subscribed ? QStringLiteral("关注歌手") : QStringLiteral("取消关注歌手");
    debounceWrite(QStringLiteral("follow:artist:") + num(id), [this, id, subscribed, action] {
        followWrite(subscribed ? QStringLiteral("artist/sub") : QStringLiteral("artist/unsub"),
                QJsonObject{{"artistId", num(id)},
                            {"artistIds", QStringLiteral("[%1]").arg(num(id))}},
                action, [this, id, subscribed] {
        QVariantMap artist = m_data.value("artist").toMap();
        if (artist.value("id").toDouble() == id) {
            artist.insert("followed", subscribed);
            put("artist", artist, false);
        }
        m_cache.forget(QStringLiteral("artist:") + num(id));
        updateSaved("savedArtists", id, subscribed);
        emit artistChanged(id, subscribed);
    });
    });
}

void Store::setAlbumSubscribed(double id, bool subscribed)
{
    const QString action = subscribed ? QStringLiteral("收藏专辑") : QStringLiteral("取消收藏专辑");
    debounceWrite(QStringLiteral("follow:album:") + num(id), [this, id, subscribed, action] {
        followWrite(subscribed ? QStringLiteral("album/sub") : QStringLiteral("album/unsub"),
                QJsonObject{{"id", num(id)}}, action, [this, id, subscribed] {
        QVariantMap album = m_data.value("album").toMap();
        if (album.value("id").toDouble() == id) {
            album.insert("subscribed", subscribed);
            put("album", album, false);
        }
        m_cache.forget(QStringLiteral("album:") + num(id));
        updateSaved("savedAlbums", id, subscribed);
        emit albumChanged(id, subscribed);
    });
    });
}

void Store::setUserFollowed(double userId, bool followed)
{
    if (!(userId > 0))
        return;
    const QString action = followed ? QStringLiteral("关注用户") : QStringLiteral("取消关注");
    if (!m_client->hasMusicU()) {
        emit followFailed(action, QStringLiteral("登录后才能关注"));
        return;
    }
    const quint64 epoch = m_epoch;
    sdk::setUserFollowed(*m_client, static_cast<qint64>(userId), followed, this,
                         [this, userId, followed, action, epoch]
                         (sdk::Result<sdk::UserFollowState> result) {
        if (epoch != m_epoch)
            return;
        if (!result) {
            emit followFailed(action, requestFailure(
                result.error, followed ? QStringLiteral("暂时无法关注用户")
                                       : QStringLiteral("暂时无法取消关注")));
            return;
        }
        const bool state = result.value->followed;
        // 与关注歌手一样：缓存里的主页还是旧的关注状态，下次打开要重新取
        m_cache.forget(QStringLiteral("user:") + num(userId));

        QVariantMap user = m_data.value("user").toMap();
        if (user.value("id").toDouble() == userId) {
            const bool was = user.value("followed").toBool();
            user.insert("followed", state);
            if (was != state) {
                const double followers = user.value("followeds").toDouble();
                user.insert("followeds", std::max(0.0, followers + (state ? 1.0 : -1.0)));
            }
            put("user", user, false);
        }

        // 关注/粉丝列表里的同一个人同步成新状态，不必等重新翻页。
        for (const char *slot : {"userFollows", "userFolloweds"}) {
            QVariantMap page = m_data.value(QLatin1String(slot)).toMap();
            QVariantList items = page.value("items").toList();
            bool changed = false;
            for (QVariant &v : items) {
                QVariantMap row = v.toMap();
                if (row.value("id").toDouble() == userId
                    && row.value("followed").toBool() != state) {
                    row.insert("followed", state);
                    v = row;
                    changed = true;
                }
            }
            if (changed) {
                page.insert("items", items);
                put(slot, page, false);
            }
        }
        emit userFollowChanged(userId, state);
    });
}
