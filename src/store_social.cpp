// 用户社交：关注/粉丝列表、等级、收藏计数。只读 GET；不做关注写操作。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

namespace {
QVariantMap socialUser(const QJsonObject &u)
{
    return {
        {"id", u.value("userId").toDouble()},
        {"name", u.value("nickname").toString()},
        {"avatar", ncmSized(u.value("avatarUrl"), 96)},
        {"signature", u.value("signature").toString()},
        {"gender", u.value("gender").toInt()},
        {"followed", u.value("followed").toBool()},
        {"mutual", u.value("mutual").toBool()},
        {"vipType", u.value("vipType").toDouble()},
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
        QVariantList items;
        if (offset > 0) {
            const QVariant old = m_data.value("userFollows");
            if (old.toMap().value("userId").toDouble() == uid)
                items = old.toMap().value("items").toList();
        }
        for (const QJsonValue &v : r.value("follow").toArray())
            items.append(socialUser(v.toObject()));
        put("userFollows", QVariantMap{{"userId", uid}, {"offset", offset},
                                       {"more", r.value("more").toBool()}, {"items", items}}, false);
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
        QVariantList items;
        if (offset > 0) {
            const QVariant old = m_data.value("userFolloweds");
            if (old.toMap().value("userId").toDouble() == uid)
                items = old.toMap().value("items").toList();
        }
        for (const QJsonValue &v : r.value("followeds").toArray())
            items.append(socialUser(v.toObject()));
        put("userFolloweds", QVariantMap{{"userId", uid}, {"offset", offset},
                                         {"more", r.value("more").toBool()}, {"items", items}}, false);
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
        const QJsonObject d = body(doc).value("data").toObject();
        if (d.isEmpty()) {
            emit requestFailed(QStringLiteral("userLevel"), QStringLiteral("暂时取不到等级信息"));
            return;
        }
        put("userLevel", QVariantMap{
            {"level", d.value("level").toInt()},
            {"nowPlayCount", d.value("nowPlayCount").toInt()},
            {"nextPlayCount", d.value("nextPlayCount").toInt()},
            {"nowLoginCount", d.value("nowLoginCount").toInt()},
            {"nextLoginCount", d.value("nextLoginCount").toInt()},
            {"progress", d.value("progress").toDouble()},
            {"info", d.value("info").toString()},
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
        put("userSubcount", QVariantMap{
            {"artistCount", r.value("artistCount").toInt()},
            {"createdPlaylistCount", r.value("createdPlaylistCount").toInt()},
            {"subPlaylistCount", r.value("subPlaylistCount").toInt()},
            {"mvCount", r.value("mvCount").toInt()},
            {"djRadioCount", r.value("djRadioCount").toInt()},
        }, false);
    });
}
