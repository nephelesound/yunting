// 只读信箱：私信会话与历史。只走 weapi、只进内存；不缓存到磁盘，不发任何写请求。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/track.h"

#include <QJsonDocument>
#include <QJsonObject>

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

QString payloadText(const QJsonObject &payload)
{
    const QString text = payload.value("msg").toString();
    if (!text.isEmpty())
        return text;
    if (payload.contains("song")) return payload.value("song").toObject().value("name").toString();
    if (payload.contains("album")) return payload.value("album").toObject().value("name").toString();
    if (payload.contains("playlist")) return payload.value("playlist").toObject().value("name").toString();
    if (payload.contains("mv")) return payload.value("mv").toObject().value("name").toString();
    return {};
}

QString payloadKind(const QJsonObject &payload)
{
    for (const char *key : {"song", "album", "playlist", "mv", "program", "event", "dj"})
        if (payload.contains(QLatin1String(key)))
            return QString::fromLatin1(key);
    return QStringLiteral("text");
}

QVariantMap normalizePayload(const QJsonObject &payload)
{
    QVariantMap out{{"text", payloadText(payload)}, {"kind", payloadKind(payload)}};
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
        {"userId", other.value("userId").toDouble()},
        {"name", other.value("nickname").toString()},
        {"avatar", ncmSized(other.value("avatarUrl"), 96)},
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
        {"toUserId", to.value("userId").toDouble()},
        {"toName", to.value("nickname").toString()},
        {"mine", from.value("userId").toDouble() == me},
        {"time", m.value("time").toDouble()},
        {"batchId", m.value("batchId").toDouble()},
    };
    out.insert("message", normalizePayload(payload));
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
        if (offset > 0) {
            const QVariant raw = m_data.value("inbox");
            if (raw.isValid() && !raw.isNull())
                items = raw.toMap().value("items").toList();
        }
        for (const QJsonValue &v : arr(resp, "msgs"))
            items.append(normalizeConversation(v.toObject(), m_accountUid));
        put("inbox", QVariantMap{
            {"offset", offset},
            {"unread", resp.value("newMsgCount").toDouble()},
            {"more", resp.value("more").toBool()},
            {"items", items},
        }, false);
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
        if (before > 0) {
            const QVariant raw = m_data.value("inboxThread");
            if (raw.isValid() && !raw.isNull()
                && raw.toMap().value("userId").toDouble() == userId)
                items = raw.toMap().value("items").toList();
        }
        for (const QJsonValue &v : arr(resp, "msgs"))
            items.append(normalizeMessage(v.toObject(), m_accountUid));
        const QJsonArray msgs = arr(resp, "msgs");
        const qint64 next = msgs.isEmpty() ? before
                                          : qint64(msgs.last().toObject().value("time").toDouble());
        put("inboxThread", QVariantMap{
            {"userId", userId},
            {"before", next},
            {"more", resp.value("more").toBool()},
            {"items", items},
        }, false);
    });
}
