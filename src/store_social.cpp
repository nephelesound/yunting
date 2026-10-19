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
