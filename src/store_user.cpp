// 用户主页：资料 + 公开歌单。两个 weapi 请求独立；资料到了就显示，歌单失败只是空列表。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

using namespace ncm;

void Store::requestUser(const QString &key, double uid)
{
    m_cache.query(key,
        [this, uid](QueryCache::Done done) {
            struct State {
                QVariantMap user;
                QVariantList playlists;
                bool detailOk = false;
                int left = 2;
            };
            auto state = std::make_shared<State>();
            const auto finish = [state, done] {
                if (--state->left != 0)
                    return;
                if (!state->detailOk) {
                    done({}, false);
                    return;
                }
                state->user.insert("playlists", state->playlists);
                done(state->user, true);
            };

            m_client->postWeapi(QStringLiteral("v1/user/detail/%1").arg(qlonglong(uid)), {}, true,
                                [state, finish](const QJsonDocument &doc, bool ok) {
                const QJsonObject root = body(doc);
                const QJsonObject p = root.value("profile").toObject();
                if (ok && root.value("code").toInt() == 200 && !p.isEmpty()) {
                    state->user = QVariantMap{
                        {"id", p.value("userId").toDouble()},
                        {"name", p.value("nickname").toString()},
                        {"avatar", ncmSized(p.value("avatarUrl"), 300)},
                        {"background", ncmSized(p.value("backgroundUrl"), 800)},
                        {"signature", p.value("signature").toString()},
                        {"followed", p.value("followed").toBool()},
                        {"follows", p.value("follows").toDouble()},
                        {"followeds", p.value("followeds").toDouble()},
                        {"playlistCount", p.value("playlistCount").toDouble()},
                        {"level", p.value("level").toDouble()},
                        {"playlists", QVariantList{}},
                    };
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
                if (ok && root.value("code").toInt() == 200) {
                    for (const QJsonValue &v : arr(root, "playlist")) {
                        const QJsonObject p = v.toObject();
                        QVariantMap card = ncmPlaylistCard(p, "coverImgUrl");
                        card.insert("owned", p.value("creator").toObject().value("userId").toDouble() == uid);
                        card.insert("description", ncmFirstLine(p.value("description").toString()));
                        state->playlists.append(card);
                    }
                }
                finish();
            });
        },
        [this](const QVariant &user) { put("user", user, false); });
}
