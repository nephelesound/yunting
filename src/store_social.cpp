// 用户社交：关注/粉丝列表、等级、收藏计数。只读 GET；不做关注写操作。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
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
        {"signature", u.signature},
        {"gender", u.gender},
        {"followed", u.followed},
        {"mutual", u.mutual},
        {"vipType", static_cast<double>(u.vipType)},
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
    if (uid <= 0)
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(QStringLiteral("userFollows"))
                                     : m_requests.begin(QStringLiteral("userFollows"));
    m_client->get(QStringLiteral("api/user/getfollows/%1").arg(qlonglong(uid)),
                  query({{"offset", QString::number(offset)}, {"limit", "30"}, {"order", "true"}}), true,
                  [this, uid, offset, epoch, token](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("userFollows"), token))
            return;
        const QJsonObject r = body(doc);
        const QString restricted = Store::privacyMessage(r);
        if (!restricted.isEmpty()) {
            const QString key = pageKey(QStringLiteral("userFollows"), uid, offset);
            put("userFollows", QVariantMap{{"userId", uid}, {"offset", offset},
                                           {"more", false}, {"items", QVariantList{}},
                                           {"restricted", true}, {"message", restricted}}, false);
            emit requestRestricted(key, restricted);
            return;
        }
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(pageKey(QStringLiteral("userFollows"), uid, offset), failure);
            return;
        }
        const auto parsed = sdk::parseUserFollows(r);
        if (!parsed) {
            emit requestFailed(pageKey(QStringLiteral("userFollows"), uid, offset),
                               QStringLiteral("暂时取不到关注列表"));
            return;
        }
        QVariantList items;
        if (offset > 0) {
            const QVariant old = m_data.value("userFollows");
            if (old.toMap().value("userId").toDouble() == uid)
                items = old.toMap().value("items").toList();
        }
        for (const sdk::SocialUser &user : parsed.value->items)
            items.append(socialUser(user));
        put("userFollows", QVariantMap{{"userId", uid}, {"offset", offset},
                                       {"more", parsed.value->more}, {"items", items}}, false);
    });
}

void Store::requestUserFolloweds(double uid, int offset)
{
    if (uid <= 0)
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(QStringLiteral("userFolloweds"))
                                     : m_requests.begin(QStringLiteral("userFolloweds"));
    m_client->get(QStringLiteral("api/user/getfolloweds/%1").arg(qlonglong(uid)),
                  query({{"offset", QString::number(offset)}, {"limit", "30"}}), true,
                  [this, uid, offset, epoch, token](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("userFolloweds"), token))
            return;
        const QJsonObject r = body(doc);
        const QString restricted = Store::privacyMessage(r);
        if (!restricted.isEmpty()) {
            const QString key = pageKey(QStringLiteral("userFolloweds"), uid, offset);
            put("userFolloweds", QVariantMap{{"userId", uid}, {"offset", offset},
                                             {"more", false}, {"items", QVariantList{}},
                                             {"restricted", true}, {"message", restricted}}, false);
            emit requestRestricted(key, restricted);
            return;
        }
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(pageKey(QStringLiteral("userFolloweds"), uid, offset), failure);
            return;
        }
        const auto parsed = sdk::parseUserFolloweds(r);
        if (!parsed) {
            emit requestFailed(pageKey(QStringLiteral("userFolloweds"), uid, offset),
                               QStringLiteral("暂时取不到粉丝列表"));
            return;
        }
        QVariantList items;
        if (offset > 0) {
            const QVariant old = m_data.value("userFolloweds");
            if (old.toMap().value("userId").toDouble() == uid)
                items = old.toMap().value("items").toList();
        }
        for (const sdk::SocialUser &user : parsed.value->items)
            items.append(socialUser(user));
        put("userFolloweds", QVariantMap{{"userId", uid}, {"offset", offset},
                                         {"more", parsed.value->more}, {"items", items}}, false);
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
