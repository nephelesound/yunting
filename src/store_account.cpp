// Store 的首屏账号链：匿名首页、账号、每日推荐、红心列表、心动、私人雷达、最近播放、云盘分页。
#include "store.h"

#include "json_util.h"
#include "normalize.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

#include <algorithm>

using namespace ncm;

namespace {
constexpr double HotChart = 3778678;
constexpr double Radar = 3136952023;
} // namespace

void Store::fetchAnonymous()
{
    fetch("api/toplist/detail", {}, false, [this](const QJsonDocument &doc, bool ok) {
        QVariantList toplists;
        if (ok) {
            const QJsonArray list = arr(body(doc), "list");
            for (int i = 0; i < list.size() && i < 12; ++i) {
                const QJsonObject t = list.at(i).toObject();
                QVariantList preview;
                for (const QJsonValue &p : t.value("tracks").toArray()) {
                    const QJsonObject o = p.toObject();
                    preview.append(QVariantMap{{"name", o.value("first").toString()},
                                               {"artist", o.value("second").toString()}});
                }
                toplists.append(QVariantMap{
                    {"id", t.value("id").toDouble()},
                    {"name", t.value("name").toString()},
                    {"cover", ncmSized(t.value("coverImgUrl"), 300)},
                    {"update", t.value("updateFrequency").toString()},
                    {"count", t.value("trackCount").toDouble()},
                    {"preview", preview},
                });
            }
        }
        put("toplists", toplists, false);
    });

    fetchPlaylistTracks(HotChart, false, [this](const QVariantList &tracks, const QJsonObject &hot, bool ok) {
        if (!ok)
            return;
        put("tracks", tracks, false);
        put("playlist", QVariantMap{
                            {"id", hot.value("id").toDouble()},
                            {"name", hot.value("name").toString()},
                            {"cover", ncmSized(hot.value("coverImgUrl"), 400)},
                            {"description", ncmFirstLine(hot.value("description").toString())},
                            {"count", hot.value("trackCount").toDouble()},
                            {"plays", hot.value("playCount").toDouble()},
                        }, false);
        // 相似歌曲的种子：未登录用热歌榜第一首；登录后等红心第一首（fetchHeart）。
        // 歌词不在首屏取：正在播放页按当前歌曲 request("lyrics:<id>")
        if (!tracks.isEmpty()) {
            m_similarSeed = tracks.at(0).toMap();
            if (!m_client->hasMusicU())
                maybeFetchSimilar();
        }
    });

    fetch("api/hotsearchlist/get", {}, false, [this](const QJsonDocument &doc, bool ok) {
        QVariantList hot;
        if (ok) {
            const QJsonArray data = arr(body(doc), "data");
            for (int i = 0; i < data.size() && i < 20; ++i) {
                const QJsonObject h = data.at(i).toObject();
                hot.append(QVariantMap{
                    {"word", h.value("searchWord").toString()},
                    {"note", h.value("content").toString()},
                    {"score", h.value("score").toDouble()},
                });
            }
        }
        put("hotSearches", hot, false);
    });
}

void Store::fetchAccount()
{
    // 账号是个人链的根；radar / 推荐歌单 / 最近 / 云盘 与红心列表无关，独立拉，
    // 红心为空或某一步失败时它们仍然会到（否则新账号的首页会缺块）。
    fetch("api/nuser/account/get", {}, true, [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject profile = body(doc).value("profile").toObject();
        const double uid = profile.value("userId").toDouble();
        // 会话已死（200 且无 profile，或回 301）：个人字段保持匿名替身的形状
        setSessionExpired(ok && uid <= 0);

        // 换账号只认 userId（昵称会重复）。请求失败（ok=false）时不清：失败保留已有的值。
        const bool accountChanged = ok && uid > 0 && m_accountUid > 0 && uid != m_accountUid;
        const bool accountGone = ok && uid <= 0
            && (m_accountUid > 0
                || (m_personal.value("account").isValid() && !m_personal.value("account").isNull()));
        if (accountChanged || accountGone) {
            // 旧账号的首屏、按需缓存与在途回包一起作废：它们可能带着私人标记（followed/subscribed）
            ++m_epoch;
            m_cache.clear();
            m_accountUid = 0;
            m_likes.clear();
            m_likeTouched.clear();
            resetPersonal();
            // 本次 refresh 早先发出的公共首屏请求属于旧 epoch，回包会被丢弃：补发一次
            fetchAnonymous();
            fetchPublicPersonal();
        }

        if (uid > 0) {
            m_accountUid = uid;
            m_cache.remember(QStringLiteral("screen:uid"), uid);
            put("account", QVariantMap{
                               {"name", profile.value("nickname").toString()},
                               {"vip", profile.value("vipType").toDouble() > 0},
                               {"avatar", ncmSized(profile.value("avatarUrl"), 96)},
                               {"background", ncmSized(profile.value("backgroundUrl"), 1200)},
                           }, true);
            // 全部红心曲目 id（播放条与行内小红心）；没有红心时接口也可能回 null。
            // 快照是在请求前算的：期间点过红心的歌用本地状态覆盖，别让旧快照把心点回去。
            const quint64 likeGeneration = m_likeGeneration;
            fetch("api/song/like/get", query({{"uid", num(uid)}}), true,
                  [this, likeGeneration](const QJsonDocument &doc, bool /*ok*/) {
                QVariantList ids;
                QSet<double> serverIds;
                for (const QJsonValue &v : arr(body(doc), "ids")) {
                    ids.append(v.toDouble());
                    serverIds.insert(v.toDouble());
                }
                if (likeGeneration != m_likeGeneration) {
                    for (double songId : std::as_const(m_likeTouched)) {
                        const bool local = isLiked(songId);
                        const bool present = serverIds.contains(songId);
                        if (local && !present) {
                            ids.append(songId);
                        } else if (!local && present) {
                            for (qsizetype i = ids.size() - 1; i >= 0; --i)
                                if (ids.at(i).toDouble() == songId)
                                    ids.removeAt(i);
                        }
                    }
                } else {
                    m_likeTouched.clear();
                }
                put("likedIds", ids, true);
            });
            fetchDaily();
            fetchUserLists(uid);
            fetchRadar();
        } else {
            // cookie 在但会话已过期（或请求失败）：个人字段保持匿名替身的形状
            if (ok)
                qWarning() << "session expired, serving anonymous personal data";
            fetchAnonymousPlaylists();
        }
    }, /*acceptNotLoggedIn=*/true);
}

void Store::fetchPublicPersonal()
{
    // 私人漫游：匿名时也是公共流，登录后是个人的；snapshot.py 同样带 cookie 取
    fetch("api/v1/radio/get", {}, true, [this](const QJsonDocument &doc, bool /*ok*/) {
        put("fm", QVariantMap{{"tracks", ncmTracks(arr(body(doc), "data"))}}, true);
    });
}

void Store::fetchAnonymousPlaylists()
{
    // snapshot.py 匿名分支：公共推荐歌单，曲目与简介留空，点开再取
    fetch("api/personalized/playlist", query({{"limit", "12"}}), false, [this](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList cards;
        for (const QJsonValue &v : arr(body(doc), "result")) {
            QVariantMap card = ncmPlaylistCard(v.toObject(), "picUrl");
            card.insert("tracks", QVariantList{});
            card.insert("description", QString{});
            cards.append(card);
        }
        put("playlists", cards, true);
    });
}

// 给 personal.playlists 里某张卡补上曲目与简介。按 id 找当前值再改，
// 几个详情请求先后返回时不会互相覆盖
void Store::patchPlaylistCard(double id, const QVariantList &tracks, const QString &description)
{
    QVariantList cards = m_personal.value("playlists").toList();
    for (QVariant &v : cards) {
        QVariantMap card = v.toMap();
        if (card.value("id").toDouble() == id) {
            card.insert("tracks", tracks);
            card.insert("description", description);
            v = card;
        }
    }
    put("playlists", cards, true);
}

void Store::fetchDaily()
{
    fetch("api/v3/discovery/recommend/songs", {}, true, [this](const QJsonDocument &doc, bool ok) {
        if (ok) {
            const QDate today = QDate::currentDate();
            // Python 的 strftime("%m月%d日").lstrip("0")：去掉月份的补零
            QString date = today.toString(QStringLiteral("MM月dd日"));
            if (date.startsWith(u'0'))
                date.remove(0, 1);
            const QJsonObject data = body(doc).value("data").toObject();
            // 推荐理由按 songId 对上：data.recommendReasons 里每条 { songId, reason }
            QHash<double, QString> reasons;
            for (const QJsonValue &r : arr(data, "recommendReasons")) {
                const QJsonObject o = r.toObject();
                reasons.insert(o.value("songId").toDouble(), o.value("reason").toString());
            }
            QVariantList tracks;
            const QJsonArray dailySongs = take(arr(data, "dailySongs"), 30);
            tracks.reserve(dailySongs.size());
            for (const QJsonValue &s : dailySongs) {
                const QJsonObject song = s.toObject();
                tracks.append(ncmTrack(song, reasons.value(song.value("id").toDouble())));
            }
            put("daily", QVariantMap{
                             {"date", date},
                             {"day", QString::number(today.day())},
                             {"tracks", tracks},
                         }, true);
        }
    });
}

void Store::fetchUserLists(double uid)
{
    // 第一次请求的「我喜欢的音乐」+ 全部创建/收藏歌单。limit=1000 是接口上限；
    // 这里不再截前 12 个，界面自己决定首屏显示几块，「全部」页拿到的是完整列表。
    fetch("api/user/playlist", query({{"uid", num(uid)}, {"limit", "1000"}, {"offset", "0"}}), true,
          [this, uid](const QJsonDocument &doc, bool ok) {
        const QJsonArray lists = arr(body(doc), "playlist");
        if (!ok || lists.isEmpty())
            return;
        QVariantList mine, subscribed;
        for (int i = 1; i < lists.size(); ++i) {
            const QJsonObject p = lists.at(i).toObject();
            QVariantMap card = ncmPlaylistCard(p, "coverImgUrl");
            const bool owned = p.value("creator").toObject().value("userId").toDouble() == uid;
            (owned ? &mine : &subscribed)->append(card);
        }
        put("mine", mine, true);
        put("subscribed", subscribed, true);
        fetchLiked(lists.at(0).toObject());
    });
}

void Store::fetchLiked(const QJsonObject &likedList)
{
    // 红心往往上千首：playlist.tracks 只回前 1000 条，fetchPlaylistTracks 用 trackIds 补齐。
    fetchPlaylistTracks(likedList.value("id").toDouble(), true,
                        [this, likedList](const QVariantList &tracks, const QJsonObject &liked, bool ok) {
        if (!ok)
            return;
        put("liked", QVariantMap{{"id", liked.value("id").toDouble()},
                                 {"count", liked.value("trackCount").toDouble()},
                                 {"tracks", tracks}}, true);
        // snapshot.py：心动模式以红心第一首为种子；红心为空就没有这一块
        if (!tracks.isEmpty())
            fetchHeart(tracks.at(0).toMap(), likedList);
    });
}

void Store::fetchHeart(const QVariantMap &seed, const QJsonObject &likedList)
{
    m_similarSeed = seed;
    maybeFetchSimilar();
    fetch("api/playmode/intelligence/list",
          query({{"songId", seed.value("id").toString()},
                 {"type", "fromPlayAll"},
                 {"playlistId", num(likedList.value("id").toDouble())},
                 {"startMusicId", seed.value("id").toString()},
                 {"count", "1"}}), true,
          [this, seed](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList tracks;
        for (const QJsonValue &x : arr(body(doc), "data"))
            tracks.append(ncmTrack(x.toObject().value("songInfo").toObject()));
        put("heart", QVariantMap{{"seed", seed}, {"tracks", tracks.mid(0, 30)}}, true);
        // radar / tail 由 fetchAccount 独立发起，不挂在这条链尾
    });
}

void Store::fetchRadar()
{
    fetchPlaylistTracks(Radar, true,
                        [this](const QVariantList &tracks, const QJsonObject &radar, bool ok) {
        if (ok)
            put("radar", QVariantMap{{"id", radar.value("id").toDouble()},
                                     {"name", QStringLiteral("私人雷达")},
                                     {"description", radar.value("name").toString()},
                                     {"cover", ncmSized(radar.value("coverImgUrl"), 300)},
                                     {"tracks", tracks}}, true);
        fetchTail();
    });
}

void Store::fetchTail()
{
    fetch("api/v1/discovery/recommend/resource", {}, true, [this](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList cards;
        const QJsonArray recommend = arr(body(doc), "recommend");
        for (const QJsonValue &v : recommend) {
            QVariantMap card = ncmPlaylistCard(v.toObject(), "picUrl");
            card.insert("tracks", QVariantList{});
            card.insert("description", QString{});
            cards.append(card);
        }
        put("playlists", cards, true);
        // 前 6 个补完整曲目和一句简介；其余保持空，点开再取。
        // 这是首页磁贴的预览数据，不截 30 首：点进去就是完整歌单，不需要再等一次按需加载。
        for (int i = 0; i < cards.size() && i < 6; ++i) {
            const double id = cards.at(i).toMap().value("id").toDouble();
            fetchPlaylistTracks(id, true,
                                [this, id](const QVariantList &tracks, const QJsonObject &pl, bool detailOk) {
                if (detailOk)
                    patchPlaylistCard(id, tracks, ncmFirstLine(pl.value("description").toString()));
            });
        }
    });

    fetch("api/play-record/song/list", query({{"limit", "300"}}), true,
          [this](const QJsonDocument &doc, bool /*ok*/) {
        const QJsonObject record = body(doc).value("data").toObject();
        QVariantList tracks;
        for (const QJsonValue &x : arr(record, "list"))
            tracks.append(ncmTrack(x.toObject().value("data").toObject()));
        put("recent", QVariantMap{{"total", record.value("total").toDouble()},
                                  {"tracks", tracks}}, true);
    });

    // 云盘用 offset 分页：接口单页上限 200，逐页取到 count 或空页为止（最多 1000 首）
    fetchCloudPage(std::make_shared<CloudState>());
}


struct Store::CloudState {
    QVariantList tracks;
    double count = 0;
    double size = 0;
    double maxSize = 0;
    int offset = 0;
};

void Store::fetchCloudPage(const std::shared_ptr<CloudState> &state)
{
    fetch("api/v1/cloud/get",
          query({{"limit", "200"}, {"offset", QString::number(state->offset)}}), true,
          [this, state](const QJsonDocument &doc, bool ok) {
        const QJsonObject resp = body(doc);
        if (ok) {
            state->count = resp.value("count").toDouble();
            state->size = resp.value("size").toDouble();
            state->maxSize = std::floor(resp.value("maxSize").toDouble());
            const QJsonArray items = arr(resp, "data");
            for (const QJsonValue &x : items)
                state->tracks.append(ncmCloudTrack(x.toObject()));
            state->offset += items.size();
        }
        if (ok && !arr(resp, "data").isEmpty() && state->offset < state->count
            && state->offset < 1000) {
            fetchCloudPage(state);
            return;
        }
        put("cloud", QVariantMap{{"count", state->count},
                                 {"size", state->size},
                                 {"maxSize", state->maxSize},
                                 {"tracks", state->tracks}}, true);
    });
}
