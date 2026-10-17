// Store 的关注/收藏写操作：歌手关注、专辑收藏。两者都是单发写请求，不重试。
#include "store.h"

#include "json_util.h"

#include <QJsonDocument>

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
                        [this, epoch, action, onSuccess](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)
            return;
        const QJsonObject resp = doc.object();
        if (ok && resp.value("code").toInt() == 200) {
            onSuccess();
            return;
        }
        QString message = resp.value("message").toString();
        if (message.isEmpty())
            message = resp.value("msg").toString();
        if (message.isEmpty())
            message = ok ? QStringLiteral("服务器没有接受这次操作")
                         : QStringLiteral("网络不可用，请稍后再试");
        emit followFailed(action, message);
    });
}

void Store::setArtistSubscribed(double id, bool subscribed)
{
    const QString action = subscribed ? QStringLiteral("关注歌手") : QStringLiteral("取消关注歌手");
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
        emit artistChanged(id, subscribed);
    });
}

void Store::setAlbumSubscribed(double id, bool subscribed)
{
    const QString action = subscribed ? QStringLiteral("收藏专辑") : QStringLiteral("取消收藏专辑");
    followWrite(subscribed ? QStringLiteral("album/sub") : QStringLiteral("album/unsub"),
                QJsonObject{{"id", num(id)}}, action, [this, id, subscribed] {
        QVariantMap album = m_data.value("album").toMap();
        if (album.value("id").toDouble() == id) {
            album.insert("subscribed", subscribed);
            put("album", album, false);
        }
        m_cache.forget(QStringLiteral("album:") + num(id));
        emit albumChanged(id, subscribed);
    });
}
