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
} // namespace

void Store::requestUserFollows(double uid, int offset)
{
    if (uid <= 0)
        return;
    m_client->get(QStringLiteral("api/user/getfollows/%1").arg(qlonglong(uid)),
                  query({{"offset", QString::number(offset)}, {"limit", "30"}, {"order", "true"}}), true,
                  [this, uid, offset](const QJsonDocument &doc, bool ok) {
        const QJsonObject r = body(doc);
        if (!ok || r.value("code").toInt() != 200) {
            emit requestFailed(QStringLiteral("userFollows"), QStringLiteral("暂时取不到关注列表"));
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
    m_client->get(QStringLiteral("api/user/getfolloweds/%1").arg(qlonglong(uid)),
                  query({{"offset", QString::number(offset)}, {"limit", "30"}}), true,
                  [this, uid, offset](const QJsonDocument &doc, bool ok) {
        const QJsonObject r = body(doc);
        if (!ok || r.value("code").toInt() != 200) {
            emit requestFailed(QStringLiteral("userFolloweds"), QStringLiteral("暂时取不到粉丝列表"));
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
    m_client->get(QStringLiteral("api/user/level"), {}, true,
                  [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject d = body(doc).value("data").toObject();
        if (!ok || d.isEmpty()) {
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
    m_client->get(QStringLiteral("api/subcount"), {}, true,
                  [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject r = body(doc);
        if (!ok || r.value("code").toInt() != 200) {
            emit requestFailed(QStringLiteral("userSubcount"), QStringLiteral("暂时取不到收藏计数"));
            return;
        }
        put("userSubcount", QVariantMap{
            {"artistCount", r.value("artistCount").toInt()},
            {"createdPlaylistCount", r.value("createdPlaylistCount").toInt()},
            {"subPlaylistCount", r.value("subPlaylistCount").toInt()},
            {"mvCount", r.value("mvCount").toInt()},
            {"djRadioCount", r.value("djRadioCount").toInt()},
        }, false);
    });
}
