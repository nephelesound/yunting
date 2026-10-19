// 只读信箱：私信会话与历史。只走 weapi、只进内存；不缓存到磁盘，不发任何写请求。
#include "store.h"

#include "json_util.h"
#include "link_parser.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/track.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

using namespace ncm;

namespace {
QJsonObject parsePayload(const QString &raw)
{
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

QJsonObject otherUser(const QJsonObject &from, const QJsonObject &to, double me)
{
    if (from.value("userId").toDouble() == me)
        return to;
    return from;
}

// 卡片类消息里的应用内链接：orpheus://open?url1=<应用内链接>&url2=<网页兜底>，取 url1
QUrl appTarget(const QString &nativeUrl)
{
    const QUrl url(nativeUrl);
    if (url.scheme() == QLatin1String("orpheus") && url.host() == QLatin1String("open")) {
        const QString inner = QUrlQuery(url).queryItemValue(QStringLiteral("url1"), QUrl::FullyDecoded);
        if (!inner.isEmpty())
            return QUrl(inner);
    }
    return url;
}

// 网页兜底：「请升级 App」页不算，点了只会叫人去装官方客户端
QString webFallback(const QString &webUrl)
{
    const QUrl url(webUrl);
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        return {};
    if (url.path().startsWith(QLatin1String("/app-upgrade/")))
        return {};
    return webUrl;
}

// 卡片类消息：type 23（generalMsg：官方号的上新、活动、一起听邀请）与 type 12（promotionUrl：推广链接）。
// 一起听邀请（应用内链接 orpheus://nm/play/listenTogether?roomId=…&inviterId=…）给 kind=listen；
// 其余给 kind=card：能在云听里打开的（歌、歌单、专辑…）带 openKind/openId，否则带网页链接 web。不是卡片时返回空
QVariantMap cardOf(const QJsonObject &payload)
{
    QString title, subtitle, cover, web;
    QUrl target;
    if (payload.value("generalMsg").isObject()) {
        const QJsonObject g = payload.value("generalMsg").toObject();
        title = g.value("title").toString();
        subtitle = g.value("subTitle").toString();
        cover = ncmSized(g.value("cover"), 120);
        web = webFallback(g.value("webUrl").toString());
        target = appTarget(g.value("nativeUrl").toString());
        if (target.scheme() == QLatin1String("orpheus") && target.host() == QLatin1String("nm")
            && target.path() == QLatin1String("/play/listenTogether")) {
            const QUrlQuery query(target);
            return {{"kind", QStringLiteral("listen")}, {"title", title}, {"subtitle", subtitle}, {"cover", cover},
                    {"roomId", query.queryItemValue(QStringLiteral("roomId"), QUrl::FullyDecoded)},
                    {"inviterId", query.queryItemValue(QStringLiteral("inviterId"), QUrl::FullyDecoded)}};
        }
    } else if (payload.value("promotionUrl").isObject()) {
        const QJsonObject p = payload.value("promotionUrl").toObject();
        title = p.value("title").toString();
        subtitle = p.value("text").toString();
        cover = ncmSized(p.value("coverUrl"), 120);
        web = webFallback(p.value("url").toString());
    } else {
        return {};
    }
    QVariantMap card{{"kind", QStringLiteral("card")}, {"title", title}, {"subtitle", subtitle}, {"cover", cover},
                     {"web", web}};
    OpenLink open = parseOpenLink(target.toString());
    if (!open.valid())
        open = parseOpenLink(web);
    if (open.valid()) {
        card.insert("openKind", linkKindName(open.kind));
        card.insert("openId", double(open.id));
    }
    return card;
}

QString payloadText(const QJsonObject &payload)
{
    const QString text = payload.value("msg").toString();
    if (!text.isEmpty())
        return text;
    if (const QVariantMap card = cardOf(payload); !card.isEmpty())
        return card.value("title").toString();
    if (payload.contains("song")) return payload.value("song").toObject().value("name").toString();
    if (payload.contains("album")) return payload.value("album").toObject().value("name").toString();
    if (payload.contains("playlist")) return payload.value("playlist").toObject().value("name").toString();
    if (payload.contains("mv")) return payload.value("mv").toObject().value("name").toString();
    return {};
}

QString payloadKind(const QJsonObject &payload)
{
    if (const QVariantMap card = cardOf(payload); !card.isEmpty())
        return card.value("kind").toString();
    for (const char *key : {"song", "album", "playlist", "mv", "program", "event", "dj"})
        if (payload.contains(QLatin1String(key)))
            return QString::fromLatin1(key);
    return QStringLiteral("text");
}

QVariantMap normalizePayload(const QJsonObject &payload)
{
    QVariantMap out{{"text", payloadText(payload)}, {"kind", payloadKind(payload)}};
    if (const QVariantMap card = cardOf(payload); !card.isEmpty())
        out.insert(card.value("kind").toString(), card);   // message.card / message.listen
    if (payload.value("song").isObject()) {
        const auto track = sdk::parseTrack(payload.value("song").toObject());
        if (track)
            out.insert("song", sdk::trackToVariantMap(*track.value));
    }
    if (payload.value("album").isObject()) {
        const QJsonObject a = payload.value("album").toObject();
        out.insert("album", QVariantMap{
            {"id", a.value("id").toDouble()},
            {"name", a.value("name").toString()},
            {"cover", ncmSized(a.value("picUrl"), 300)},
            {"artist", a.value("artist").toObject().value("name").toString()},
        });
    }
    if (payload.value("playlist").isObject()) {
        const auto card = sdk::parsePlaylistCard(payload.value("playlist").toObject(),
                                                 "coverImgUrl");
        if (card)
            out.insert("playlist", sdk::playlistCardToVariantMap(*card.value));
    }
    if (payload.value("mv").isObject()) {
        const QJsonObject m = payload.value("mv").toObject();
        out.insert("mv", QVariantMap{
            {"id", m.value("id").toDouble()},
            {"name", m.value("name").toString()},
            {"cover", ncmSized(m.value("cover"), 300)},
            {"artist", m.value("artistName").toString()},
        });
    }
    return out;
}

QString pendantUrl(const QJsonObject &user)
{
    return user.value("pendantData").toObject().value("imageUrl").toString();
}

QString identityIconUrl(const QJsonObject &user)
{
    return user.value("avatarDetail").toObject().value("identityIconUrl").toString();
}

QVariantMap normalizeConversation(const QJsonObject &c, double me)
{
    const QJsonObject from = c.value("fromUser").toObject();
    const QJsonObject to = c.value("toUser").toObject();
    QJsonObject other = otherUser(from, to, me);
    if (other.isEmpty())
        other = from;
    const QJsonObject user = c.value("user").toObject();
    QJsonObject payload = parsePayload(c.value("lastMsg").toString());
    if (payload.isEmpty())
        payload = parsePayload(user.value("lastMsg").toString());
    return {
        {"key", QString::number(qlonglong(other.value("userId").toDouble()))},   // 列表里认同一行（见 Store::putPage）
        {"userId", other.value("userId").toDouble()},
        {"name", other.value("nickname").toString()},
        {"avatar", ncmSized(other.value("avatarUrl"), 96)},
        {"avatarPendant", pendantUrl(other)},
        {"identityIcon", identityIconUrl(other)},
        {"vipType", other.value("vipType").toDouble()},
        {"lastText", payloadText(payload)},
        {"kind", payloadKind(payload)},
        {"lastTime", c.value("lastMsgTime").toDouble()},
        {"lastMsgId", c.value("lastMsgId").toDouble()},
        {"unread", c.value("newMsgCount").toDouble()},
        {"messageCount", user.value("msgCount").toDouble()},
    };
}

QVariantMap normalizeMessage(const QJsonObject &m, double me)
{
    QJsonObject from = m.value("fromUser").toObject();
    const QJsonObject to = m.value("toUser").toObject();
    if (from.isEmpty() && m.value("realFromUser").isObject())
        from = m.value("realFromUser").toObject();
    const QJsonObject payload = parsePayload(m.value("msg").toString());
    QVariantMap out{
        {"id", m.value("id").toDouble()},
        {"fromUserId", from.value("userId").toDouble()},
        {"fromName", from.value("nickname").toString()},
        {"fromAvatar", ncmSized(from.value("avatarUrl"), 96)},
        {"fromAvatarPendant", pendantUrl(from)},
        {"fromIdentityIcon", identityIconUrl(from)},
        {"toUserId", to.value("userId").toDouble()},
        {"toName", to.value("nickname").toString()},
        {"mine", from.value("userId").toDouble() == me},
        {"time", m.value("time").toDouble()},
        {"batchId", m.value("batchId").toDouble()},
    };
    out.insert("message", normalizePayload(payload));
    // 列表里认同一条（见 Store::putPage）：有些消息没有 id，用发送人、时间与批次
    const qint64 id = qint64(out.value("id").toDouble());
    out.insert("key", id > 0 ? QString::number(id)
                             : QStringLiteral("%1:%2:%3").arg(qlonglong(out.value("fromUserId").toDouble()))
                                   .arg(qlonglong(out.value("time").toDouble()))
                                   .arg(qlonglong(out.value("batchId").toDouble())));
    return out;
}
} // namespace

void Store::requestInbox(int offset)
{
    if (!m_client->hasMusicU())
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(QStringLiteral("inbox"))
                                     : m_requests.begin(QStringLiteral("inbox"));
    m_client->postWeapi(QStringLiteral("msg/private/users"),
                        QJsonObject{{"offset", offset},
                                    {"limit", 30},
                                    {"total", QStringLiteral("true")}},
                        true,
                        [this, offset, epoch, token](const NcmClient::Response &response) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("inbox"), token))
            return;
        const QString failure = requestFailure(response);
        if (!failure.isEmpty()) {
            emit requestFailed(offset > 0 ? QStringLiteral("inbox:%1").arg(offset) : QStringLiteral("inbox"), failure);
            return;
        }
        const QJsonObject resp = response.doc.object();
        QVariantList items;
        for (const QJsonValue &v : arr(resp, "msgs"))
            items.append(normalizeConversation(v.toObject(), m_accountUid));
        // 续页按 key 去重接在后面：同一页回来两次也不会重复
        const QVariant old = m_data.value("inbox");
        putPage("inbox", QVariantMap{
            {"offset", offset},
            {"next", offset + int(arr(resp, "msgs").size())},   // 下一页的起点：不拿去重后的条数翻页
            {"unread", resp.value("newMsgCount").toDouble()},
            {"more", resp.value("more").toBool()},
            {"items", items},
        }, offset > 0 && old.isValid() && !old.isNull(), false);
    });
}

void Store::requestInboxThread(double userId, qint64 before)
{
    if (!m_client->hasMusicU() || userId <= 0)
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = before > 0 ? m_requests.token(QStringLiteral("inboxThread"))
                                     : m_requests.begin(QStringLiteral("inboxThread"));
    m_client->postWeapi(QStringLiteral("msg/private/history"),
                        QJsonObject{{"userId", num(userId)},
                                    {"limit", 30},
                                    {"time", QString::number(before)},
                                    {"total", QStringLiteral("true")}},
                        true,
                        [this, userId, before, epoch, token](const NcmClient::Response &response) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("inboxThread"), token))
            return;
        const QString failure = requestFailure(response);
        if (!failure.isEmpty()) {
            // 按请求时的 key 报：界面按会话（与翻页位置）给重试
            emit requestFailed(before > 0 ? QStringLiteral("inboxThread:%1:%2").arg(num(userId)).arg(before)
                                          : QStringLiteral("inboxThread:%1").arg(num(userId)),
                               failure);
            return;
        }
        const QJsonObject resp = response.doc.object();
        QVariantList items;
        for (const QJsonValue &v : arr(resp, "msgs"))
            items.append(normalizeMessage(v.toObject(), m_accountUid));
        const QJsonArray msgs = arr(resp, "msgs");
        const qint64 next = msgs.isEmpty() ? before
                                          : qint64(msgs.last().toObject().value("time").toDouble());
        // 更早的一页按 key 去重接在后面（只接同一个人的）：同一页回来两次也不会重复
        const bool samePerson = m_data.value("inboxThread").toMap().value("userId").toDouble() == userId;
        putPage("inboxThread", QVariantMap{
            {"userId", userId},
            {"before", next},
            {"more", resp.value("more").toBool()},
            {"items", items},
        }, before > 0 && samePerson, false);
    });
}
