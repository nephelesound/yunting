// 用户社交：关注/粉丝列表、等级、收藏计数。只读 GET；不做关注写操作。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/comment.h"
#include "sdk/social.h"
#include "sdk/user_social.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

namespace {
// QML-facing legacy shape. The SDK model keeps the avatar raw; sizing stays at
// this boundary, and ids/counters keep the double scalar types the existing
// bindings receive.
QVariantMap socialUser(const sdk::SocialUser &u)
{
    return {
        {"id", static_cast<double>(u.id)},
        {"name", u.name},
        {"avatar", ncmSized(QJsonValue(u.avatar), 96)},
        {"avatarPendant", u.pendant},
        {"identityIcon", u.identityIcon},
        {"signature", u.signature},
        {"gender", u.gender},
        {"followed", u.followed},
        {"mutual", u.mutual},
        {"vipType", static_cast<double>(u.vipType)},
        {"vipLevel", u.vipLevel},
    };
}

// 失败按请求时的 key 报（第一页不带 offset），界面才能对上这一页的重试
QString pageKey(const QString &kind, double uid, int offset)
{
    const QString key = kind + u':' + QString::number(qlonglong(uid));
    return offset > 0 ? key + u':' + QString::number(offset) : key;
}

} // namespace

void Store::requestUserFollows(double uid, int offset)
{
    requestPeople("userFollows", QStringLiteral("api/user/getfollows/%1"), sdk::parseUserFollows,
                  QStringLiteral("暂时取不到关注列表"), uid, offset, /*newestFirst=*/true);
}

void Store::requestUserFolloweds(double uid, int offset)
{
    requestPeople("userFolloweds", QStringLiteral("api/user/getfolloweds/%1"), sdk::parseUserFolloweds,
                  QStringLiteral("暂时取不到粉丝列表"), uid, offset, /*newestFirst=*/false);
}

// 关注 / 粉丝：同一种分页（每页 30）。第一页开新一代（换了人，旧的回包作废）；续页经 putPage
// 按 id 去重接在后面：同一页回来两次（滚到底触发了两次、点了重试）也不会出现重复的人
void Store::requestPeople(const char *slot, const QString &path,
                          sdk::Result<sdk::FollowPage> (*parse)(const QJsonObject &),
                          const QString &failText, double uid, int offset, bool newestFirst)
{
    if (uid <= 0)
        return;
    const QString kind = QLatin1String(slot);
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(kind) : m_requests.begin(kind);
    QUrlQuery params = query({{"offset", QString::number(offset)}, {"limit", "30"}});
    if (newestFirst)
        params.addQueryItem(QStringLiteral("order"), QStringLiteral("true"));
    m_client->get(path.arg(qlonglong(uid)), params, true,
                  [this, slot, kind, parse, failText, uid, offset, epoch, token](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch || !m_requests.current(kind, token))
            return;
        const QJsonObject r = body(doc);
        const QString restricted = Store::privacyMessage(r);
        if (!restricted.isEmpty()) {
            put(slot, QVariantMap{{"userId", uid}, {"offset", offset},
                                  {"more", false}, {"items", QVariantList{}},
                                  {"restricted", true}, {"message", restricted}}, false);
            emit requestRestricted(pageKey(kind, uid, offset), restricted);
            return;
        }
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(pageKey(kind, uid, offset), failure);
            return;
        }
        const auto parsed = parse(r);
        if (!parsed) {
            emit requestFailed(pageKey(kind, uid, offset), failText);
            return;
        }
        QVariantList items;
        for (const sdk::SocialUser &user : parsed.value->items)
            items.append(socialUser(user));
        const bool samePerson = m_data.value(kind).toMap().value("userId").toDouble() == uid;
        // next：服务器上的下一页从哪开始。去重后的条数可能比它少（两页之间名单变了），不能拿条数翻页
        putPage(slot, QVariantMap{{"userId", uid}, {"offset", offset},
                                  {"next", offset + int(parsed.value->items.size())},
                                  {"more", parsed.value->more}, {"items", items}},
                offset > 0 && samePerson, false);
    });
}

void Store::requestUserLevel()
{
    if (!m_client->hasMusicU())
        return;
    const quint64 epoch = m_epoch;
    m_client->get(QStringLiteral("api/user/level"), {}, true,
                  [this, epoch](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("userLevel"), failure);
            return;
        }
        const QJsonObject r = body(doc);
        const auto parsed = sdk::parseUserLevel(r);
        if (!parsed) {
            emit requestFailed(QStringLiteral("userLevel"), QStringLiteral("暂时取不到等级信息"));
            return;
        }
        const sdk::UserLevel &level = *parsed.value;
        put("userLevel", QVariantMap{
            {"level", level.level},
            {"nowPlayCount", level.nowPlayCount},
            {"nextPlayCount", level.nextPlayCount},
            {"nowLoginCount", level.nowLoginCount},
            {"nextLoginCount", level.nextLoginCount},
            {"progress", level.progress},
            {"info", level.info},
        }, false);
    });
}

void Store::requestUserSubcount()
{
    if (!m_client->hasMusicU())
        return;
    const quint64 epoch = m_epoch;
    m_client->get(QStringLiteral("api/subcount"), {}, true,
                  [this, epoch](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("userSubcount"), failure);
            return;
        }
        const QJsonObject r = body(doc);
        const auto parsed = sdk::parseUserSubcount(r);
        if (!parsed) {
            emit requestFailed(QStringLiteral("userSubcount"),
                               QStringLiteral("暂时取不到收藏计数"));
            return;
        }
        const sdk::UserSubcount &counts = *parsed.value;
        put("userSubcount", QVariantMap{
            {"artistCount", counts.artistCount},
            {"createdPlaylistCount", counts.createdPlaylistCount},
            {"subPlaylistCount", counts.subPlaylistCount},
            {"mvCount", counts.mvCount},
            {"djRadioCount", counts.djRadioCount},
        }, false);
    });
}

namespace {
QString commentThreadFor(const QString &kind, double ownerId)
{
    if (kind == QLatin1String("playlist"))
        return sdk::commentThreadId(sdk::CommentThreadKind::Playlist, static_cast<qint64>(ownerId));
    return sdk::commentThreadId(sdk::CommentThreadKind::Song, static_cast<qint64>(ownerId));
}

QString commentActionName(const QString &kind)
{
    return kind == QLatin1String("playlist") ? QStringLiteral("歌单评论") : QStringLiteral("歌曲评论");
}
} // namespace

void Store::hugComment(const QString &kind, double ownerId, double commentId, double targetUserId)
{
    const QString action = commentActionName(kind) + QStringLiteral("抱一抱");
    if (kind != QLatin1String("song") && kind != QLatin1String("playlist")) {
        emit commentActionFailed(action, commentId, QStringLiteral("评论类型无效"));
        return;
    }
    if (!m_client->hasMusicU()) {
        emit commentActionFailed(action, commentId, QStringLiteral("登录后才能抱一抱"));
        return;
    }
    if (ownerId <= 0 || commentId <= 0 || targetUserId <= 0) {
        emit commentActionFailed(action, commentId, QStringLiteral("评论信息无效"));
        return;
    }
    const QString threadId = commentThreadFor(kind, ownerId);
    sdk::hugComment(*m_client, threadId, static_cast<qint64>(commentId),
                    static_cast<qint64>(targetUserId), this,
                    [this, action, commentId](sdk::Result<bool> result) {
        if (!result) {
            emit commentActionFailed(action, commentId,
                                     requestFailure(result.error, QStringLiteral("暂时不能抱一抱")));
            return;
        }
        emit commentActionSucceeded(action, commentId);
    });
}

void Store::reportComment(const QString &kind, double ownerId, double commentId, int reason)
{
    const QString action = commentActionName(kind) + QStringLiteral("举报");
    if (kind != QLatin1String("song") && kind != QLatin1String("playlist")) {
        emit commentActionFailed(action, commentId, QStringLiteral("评论类型无效"));
        return;
    }
    if (!m_client->hasMusicU()) {
        emit commentActionFailed(action, commentId, QStringLiteral("登录后才能举报"));
        return;
    }
    if (ownerId <= 0 || commentId <= 0 || reason < 0) {
        emit commentActionFailed(action, commentId, QStringLiteral("举报信息无效"));
        return;
    }
    const QString threadId = commentThreadFor(kind, ownerId);
    sdk::reportComment(*m_client, threadId, static_cast<qint64>(commentId), reason, this,
                       [this, action, commentId](sdk::Result<bool> result) {
        if (!result) {
            emit commentActionFailed(action, commentId,
                                     requestFailure(result.error, QStringLiteral("暂时不能举报")));
            return;
        }
        emit commentActionSucceeded(action, commentId);
    });
}

void Store::forwardEvent(double eventId, double eventUserId, const QString &text)
{
    const QString action = QStringLiteral("转发动态");
    if (!m_client->hasMusicU()) {
        emit eventActionFailed(action, eventId, QStringLiteral("登录后才能转发动态"));
        return;
    }
    if (eventId <= 0 || eventUserId <= 0) {
        emit eventActionFailed(action, eventId, QStringLiteral("动态信息无效"));
        return;
    }
    sdk::forwardEvent(*m_client, static_cast<qint64>(eventId),
                      static_cast<qint64>(eventUserId), text, this,
                      [this, action, eventId](sdk::Result<bool> result) {
        if (!result) {
            emit eventActionFailed(action, eventId,
                                   requestFailure(result.error, QStringLiteral("暂时不能转发动态")));
            return;
        }
        emit eventActionSucceeded(action, eventId);
    });
}

void Store::deleteEvent(double eventId)
{
    const QString action = QStringLiteral("删除动态");
    if (!m_client->hasMusicU()) {
        emit eventActionFailed(action, eventId, QStringLiteral("登录后才能删除动态"));
        return;
    }
    if (eventId <= 0) {
        emit eventActionFailed(action, eventId, QStringLiteral("动态信息无效"));
        return;
    }
    sdk::deleteEvent(*m_client, static_cast<qint64>(eventId), this,
                     [this, action, eventId](sdk::Result<bool> result) {
        if (!result) {
            emit eventActionFailed(action, eventId,
                                   requestFailure(result.error, QStringLiteral("暂时不能删除动态")));
            return;
        }
        QVariantMap page = m_data.value("userEvents").toMap();
        QVariantList items = page.value("items").toList();
        for (qsizetype i = items.size() - 1; i >= 0; --i) {
            if (items.at(i).toMap().value("id").toDouble() == eventId)
                items.removeAt(i);
        }
        page.insert("items", items);
        put("userEvents", page, false);
        emit eventActionSucceeded(action, eventId);
    });
}

void Store::setEventPrivacy(double eventId, int privacy)
{
    const QString action = QStringLiteral("修改动态可见性");
    if (!m_client->hasMusicU()) {
        emit eventActionFailed(action, eventId, QStringLiteral("登录后才能修改动态可见性"));
        return;
    }
    if (eventId <= 0
        || (privacy != 0 && privacy != 1 && privacy != 2 && privacy != 6)) {
        emit eventActionFailed(action, eventId, QStringLiteral("动态可见性无效"));
        return;
    }
    sdk::setEventPrivacy(*m_client, static_cast<qint64>(eventId), privacy, this,
                         [this, action, eventId, privacy](sdk::Result<bool> result) {
        if (!result) {
            emit eventActionFailed(action, eventId,
                                   requestFailure(result.error,
                                                  QStringLiteral("暂时不能修改动态可见性")));
            return;
        }
        QVariantMap page = m_data.value("userEvents").toMap();
        QVariantList items = page.value("items").toList();
        for (QVariant &value : items) {
            QVariantMap item = value.toMap();
            if (item.value("id").toDouble() == eventId) {
                item.insert("privacySetting", privacy);
                value = item;
            }
        }
        page.insert("items", items);
        put("userEvents", page, false);
        emit eventActionSucceeded(action, eventId);
    });
}
