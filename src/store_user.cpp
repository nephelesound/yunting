// 用户主页：资料 + 公开歌单。两个 weapi 请求独立；资料到了就显示，歌单失败只是空列表。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/track.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

using namespace ncm;

void Store::requestUser(const QString &key, double uid)
{
    if (!(uid > 0))
        return;   // 退栈时 userId 会短暂归零；不能请求 /user/detail/0
    const quint64 token = m_requests.begin(QStringLiteral("user"));
    m_cache.query(key,
        [this, uid](QueryCache::Done done) {
            struct State {
                QVariantMap user;
                QVariantList playlists;
                bool detailOk = false;
                QString restricted;
                int left = 2;
            };
            auto state = std::make_shared<State>();
            const auto finish = [state, done, uid] {
                if (--state->left != 0)
                    return;
                if (!state->detailOk && state->restricted.isEmpty()) {
                    done({}, false);
                    return;
                }
                if (!state->restricted.isEmpty()) {
                    state->user.insert("id", uid);
                    state->user.insert("restricted", true);
                    state->user.insert("message", state->restricted);
                }
                state->user.insert("playlists", state->playlists);
                done(state->user, true);
            };

            m_client->postWeapi(QStringLiteral("v1/user/detail/%1").arg(qlonglong(uid)), {}, true,
                                [state, finish](const QJsonDocument &doc, bool ok) {
                const QJsonObject root = body(doc);
                const QString privacy = Store::privacyMessage(root);
                if (!privacy.isEmpty()) {
                    state->restricted = privacy;
                    state->detailOk = true;
                    finish();
                    return;
                }
                const QJsonObject p = root.value("profile").toObject();
                if (ok && root.value("code").toInt() == 200 && !p.isEmpty()) {
                    state->user = ncmProfile(p, 300, 800);
                    // 等级与累计听歌在响应顶层，不在 profile 里
                    state->user.insert("level", root.value("level").toDouble());
                    state->user.insert("listenSongs", root.value("listenSongs").toDouble());
                    state->user.insert("playlists", QVariantList{});
                    state->detailOk = true;
                }
                finish();
            });

            m_client->postWeapi(QStringLiteral("user/playlist"),
                                QJsonObject{{"uid", num(uid)},
                                            {"limit", QStringLiteral("1000")},
                                            {"offset", QStringLiteral("0")},
                                            {"includeVideo", true}},
                                true,
                                [state, finish, uid](const QJsonDocument &doc, bool ok) {
                const QJsonObject root = body(doc);
                const QString privacy = Store::privacyMessage(root);
                if (!privacy.isEmpty()) {
                    state->restricted = privacy;
                    finish();
                    return;
                }
                if (ok && root.value("code").toInt() == 200) {
                    for (const QJsonValue &v : arr(root, "playlist")) {
                        const auto parsedCard =
                            sdk::parsePlaylistCard(v.toObject(), "coverImgUrl");
                        if (!parsedCard)
                            continue;
                        QVariantMap card = sdk::playlistCardToVariantMap(*parsedCard.value);
                        card.insert("owned",
                                    static_cast<double>(parsedCard.value->creatorId) == uid);
                        state->playlists.append(card);
                    }
                }
                finish();
            });
        },
        [this, token, uid](const QVariant &user) {
            if (!m_requests.current(QStringLiteral("user"), token))
                return;
            put("user", user, false);
            const QVariantMap map = user.toMap();
            if (map.value("restricted").toBool())
                emit requestRestricted(QStringLiteral("user:") + num(uid),
                                       map.value("message").toString());
        });
}
