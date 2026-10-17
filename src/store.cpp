#include "store.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QDate>
#include <QEventLoop>
#include <QNetworkInformation>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>

using namespace ncm;

namespace {
constexpr double HotChart = 3778678;
constexpr double Radar = 3136952023;

QVariant nullVariant() { return QVariant::fromValue(nullptr); }
} // namespace

Store::Store(NcmClient *client, QObject *parent)
    : QObject(parent), m_client(client),
      m_cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/queries.json")
{
    // 骨架先就位：形状永远完整，QML 绑定不会踩到缺键（值本身仍可为 null / 空表）
    m_data = QVariantMap{
        {"toplists", QVariantList{}},
        {"playlist", QVariantMap{{"id", 0.0}, {"name", QString{}}, {"cover", QString{}},
                                 {"description", QString{}}, {"count", 0.0}, {"plays", 0.0}}},
        {"tracks", QVariantList{}},
        {"lyricsTrackId", 0.0},
        {"lyrics", QVariantList{}},
        {"search", QVariantMap{{"query", QString{}}, {"songs", QVariantList{}},
                               {"artists", QVariantList{}}, {"playlists", QVariantList{}},
                               {"albums", QVariantList{}}}},
        {"hotSearches", QVariantList{}},
    };
    m_personal = QVariantMap{
        {"account", nullVariant()},
        {"daily", nullVariant()},
        {"heart", nullVariant()},
        {"radar", nullVariant()},
        {"liked", nullVariant()},
        {"mine", QVariantList{}},
        {"recent", nullVariant()},
        {"cloud", nullVariant()},
        {"subscribed", QVariantList{}},
        {"playlists", QVariantList{}},
        {"fm", QVariantMap{{"tracks", QVariantList{}}}},
        {"similar", QVariantMap{{"seed", nullVariant()}, {"tracks", QVariantList{}}}},
        {"playlistDetails", QVariantMap{}},
    };
    m_emptyPersonal = m_personal;
    restoreScreen();

    connect(m_client, &NcmClient::requestFinished, this, &Store::requestFinished);
    // 系统说网络恢复（离线→在线沿）：还上离线期间欠的那次刷新
    if (QNetworkInformation::loadDefaultBackend()) {
        m_netInfo = QNetworkInformation::instance();
        connect(m_netInfo, &QNetworkInformation::reachabilityChanged, this, &Store::connectionRegained);
        if (m_netInfo->reachability() == QNetworkInformation::Reachability::Disconnected)
            setOnline(false);
    }
}

// 离线→在线：QNetworkInformation 报告恢复，或（后端不可用时）下一个请求成功。
// m_refreshOwed 保证每次离线期间只补拉一次，两个来源谁先到谁还，不循环刷新。
void Store::connectionRegained()
{
    if (!m_netInfo || m_netInfo->reachability() != QNetworkInformation::Reachability::Online)
        return;
    if (m_online) {
        // 迟到的 Online 沿（请求成功已把 online 翻回 true）：按欠账处理，不重复刷
        if (m_refreshOwed) {
            m_refreshOwed = false;
            refresh();
        }
        return;
    }
    setOnline(true);
    m_refreshOwed = false;
    refresh();
}

// 上次的首屏值先放上（只放骨架里有的键，旧版本缓存里多出来的字段不带进来）。
// 个人部分按会话分开存：登录与匿名各一份，互不覆盖；换了账号由 fetchAccount 发现并清掉
QString Store::personalKey() const
{
    return m_client->hasMusicU() ? QStringLiteral("screen:personal") : QStringLiteral("screen:personal:anon");
}

void Store::restoreScreen()
{
    const auto overlay = [](QVariantMap &into, const QVariantMap &cached) {
        for (auto it = cached.constBegin(); it != cached.constEnd(); ++it)
            if (into.contains(it.key()))
                into.insert(it.key(), it.value());
    };
    overlay(m_data, m_cache.peek("screen:data").toMap());
    overlay(m_personal, m_cache.peek(personalKey()).toMap());
}

void Store::put(const char *key, const QVariant &value, bool personalSide)
{
    if (personalSide) {
        m_personal.insert(QLatin1String(key), value);
        m_cache.remember(personalKey(), m_personal);
        emit personalChanged();
    } else {
        m_data.insert(QLatin1String(key), value);
        m_cache.remember("screen:data", m_data);
        emit dataChanged();
    }
}

void Store::refresh()
{
    // 种子可能换人（匿名热歌第一首 → 登录后红心第一首），重新竞速一次
    m_similarFired = false;
    m_similarSeed.clear();
    fetchAnonymous();
    fetchPublicPersonal();   // 漫游是公共接口：匿名也取（带 cookie 时是个人的）
    if (m_client->hasMusicU())
        fetchAccount();      // 账号有效就走个人链；无效时在回调里退回匿名推荐歌单
    else
        fetchAnonymousPlaylists();
}

// 连接状态与会话过期的唯一写入口：请求落地的真相在这里收敛
void Store::requestFinished(bool ok, bool networkFailure)
{
    // 只要服务器回了话（成功、4xx、业务码、坏 JSON 都算），网络就是通的；只有连不上才算离线
    if (networkFailure) {
        setOnline(false);
        m_refreshOwed = true;
        return;
    }
    setOnline(true);
    // 后端不可用时的兜底：网络通了本身就是恢复信号，还掉欠账
    if (ok && m_refreshOwed) {
        m_refreshOwed = false;
        refresh();
    }
}

void Store::setOnline(bool online)
{
    if (m_online == online)
        return;
    m_online = online;
    emit onlineChanged();
}

void Store::setSessionExpired(bool expired)
{
    if (m_sessionExpired == expired)
        return;
    m_sessionExpired = expired;
    emit sessionExpiredChanged();
}

void Store::fetch(const QString &path, const QUrlQuery &q, bool personal, NcmClient::GetCallback callback,
                  bool acceptNotLoggedIn)
{
    ++m_pending;
    m_client->get(path, q, personal, [this, callback, acceptNotLoggedIn](const QJsonDocument &doc, bool ok) {
        // stale-while-revalidate 的另一半：失败（断网、超时、接口回 301 未登录等）一律不回调，
        // 已经恢复或取到的旧值原样保留，绝不用空结果覆盖。
        // 例外：账号接口要看到 301，才能判断「登录已过期」（acceptNotLoggedIn）
        const QJsonValue code = doc.object().value("code");
        const bool success = code.isUndefined() || code.toInt() == 200;
        if (ok && (success || (acceptNotLoggedIn && code.toInt() == 301)))
            callback(doc, ok);
        if (--m_pending == 0)
            emit settled();
    });
}

void Store::waitForInitial()
{
    refresh();
    if (m_pending > 0) {
        QEventLoop loop;
        // 兜底：网络挂了也要退出，不悬挂 --dump-store
        QTimer::singleShot(30 * 1000, &loop, &QEventLoop::quit);
        connect(this, &Store::settled, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

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

    fetch("api/v6/playlist/detail", query({{"id", num(HotChart)}, {"n", "40"}}), false,
          [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject hot = body(doc).value("playlist").toObject();
        const QVariantList tracks = ncmTracks(take(arr(hot, "tracks"), 40));
        put("tracks", tracks, false);
        if (ok) {
            put("playlist", QVariantMap{
                                {"id", hot.value("id").toDouble()},
                                {"name", hot.value("name").toString()},
                                {"cover", ncmSized(hot.value("coverImgUrl"), 400)},
                                {"description", ncmFirstLine(hot.value("description").toString())},
                                {"count", hot.value("trackCount").toDouble()},
                                {"plays", hot.value("playCount").toDouble()},
                            }, false);
        }
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
    // 账号是个人链的根：daily / liked→heart→radar→推荐歌单→最近/云盘 都挂在它后面
    fetch("api/nuser/account/get", {}, true, [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject profile = body(doc).value("profile").toObject();
        const double uid = profile.value("userId").toDouble();
        // 会话已死（200 且无 profile，或回 301）：个人字段保持匿名替身的形状
        setSessionExpired(ok && uid <= 0);
        // 恢复出来的个人缓存属于另一个账号（或会话已失效）：先清回空骨架，不让旧账号的数据停留。
        // 请求本身失败（ok=false）时不清：失败保留已有的值
        const QString cachedName = m_personal.value("account").toMap().value("name").toString();
        if (ok && !cachedName.isEmpty()
            && (uid <= 0 || cachedName != profile.value("nickname").toString())) {
            m_personal = m_emptyPersonal;
            m_cache.remember(personalKey(), m_personal);
            emit personalChanged();
        }
        if (uid > 0) {
            put("account", QVariantMap{
                               {"name", profile.value("nickname").toString()},
                               {"vip", profile.value("vipType").toDouble() > 0},
                               {"avatar", ncmSized(profile.value("avatarUrl"), 96)},
                           }, true);
            fetchDaily();
            fetchUserLists(uid);
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
            put("daily", QVariantMap{
                             {"date", date},
                             {"day", QString::number(today.day())},
                             {"tracks", ncmTracks(take(arr(body(doc).value("data").toObject(),
                                                          "dailySongs"), 30))},
                         }, true);
        }
    });
}

void Store::fetchUserLists(double uid)
{
    fetch("api/user/playlist", query({{"uid", num(uid)}, {"limit", "30"}, {"offset", "0"}}), true,
          [this, uid](const QJsonDocument &doc, bool ok) {
        const QJsonArray lists = arr(body(doc), "playlist");
        if (!ok || lists.isEmpty())
            return;
        // 第一个固定是「我喜欢的音乐」；创建的在前、收藏的在后（各留 12）
        QVariantList mine, subscribed;
        for (int i = 1; i < lists.size(); ++i) {
            const QJsonObject p = lists.at(i).toObject();
            QVariantMap card = ncmPlaylistCard(p, "coverImgUrl");
            const bool owned = p.value("creator").toObject().value("userId").toDouble() == uid;
            (owned ? &mine : &subscribed)->append(card);
            if (mine.size() >= 12 && subscribed.size() >= 12)
                break;
        }
        put("mine", mine, true);
        put("subscribed", subscribed, true);
        fetchLiked(lists.at(0).toObject());
    });
}

void Store::fetchLiked(const QJsonObject &likedList)
{
    fetch("api/v6/playlist/detail",
          query({{"id", num(likedList.value("id").toDouble())}, {"n", "30"}}), true,
          [this, likedList](const QJsonDocument &doc, bool ok) {
        const QJsonObject liked = body(doc).value("playlist").toObject();
        const QVariantList tracks = ncmTracks(take(arr(liked, "tracks"), 30));
        if (ok)
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
        fetchRadar(seed);
    });
}

void Store::fetchRadar(const QVariantMap &seed)
{
    fetch("api/v6/playlist/detail", query({{"id", num(Radar)}, {"n", "30"}}), true,
          [this, seed](const QJsonDocument &doc, bool ok) {
        const QJsonObject radar = body(doc).value("playlist").toObject();
        if (ok)
            put("radar", QVariantMap{{"id", radar.value("id").toDouble()},
                                     {"name", QStringLiteral("私人雷达")},
                                     {"description", radar.value("name").toString()},
                                     {"cover", ncmSized(radar.value("coverImgUrl"), 300)},
                                     {"tracks", ncmTracks(take(arr(radar, "tracks"), 30))}}, true);
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
        // 前 6 个补 30 首试听曲目和一句简介；其余保持空，点开再取
        for (int i = 0; i < cards.size() && i < 6; ++i) {
            const double id = cards.at(i).toMap().value("id").toDouble();
            fetch("api/v6/playlist/detail", query({{"id", num(id)}, {"n", "30"}}), true,
                  [this, id](const QJsonDocument &detailDoc, bool detailOk) {
                if (!detailOk)
                    return;
                const QJsonObject pl = body(detailDoc).value("playlist").toObject();
                patchPlaylistCard(id, ncmTracks(take(arr(pl, "tracks"), 30)),
                                  ncmFirstLine(pl.value("description").toString()));
            });
        }
    });

    fetch("api/play-record/song/list", query({{"limit", "30"}}), true,
          [this](const QJsonDocument &doc, bool /*ok*/) {
        const QJsonObject record = body(doc).value("data").toObject();
        QVariantList tracks;
        for (const QJsonValue &x : arr(record, "list"))
            tracks.append(ncmTrack(x.toObject().value("data").toObject()));
        put("recent", QVariantMap{{"total", record.value("total").toDouble()},
                                  {"tracks", tracks.mid(0, 30)}}, true);
    });

    fetch("api/v1/cloud/get", query({{"limit", "30"}, {"offset", "0"}}), true,
          [this](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList tracks;
        for (const QJsonValue &x : arr(body(doc), "data"))
            tracks.append(ncmCloudTrack(x.toObject()));
        put("cloud", QVariantMap{{"count", body(doc).value("count").toDouble()},
                                 {"size", body(doc).value("size").toDouble()},
                                 {"maxSize", std::floor(body(doc).value("maxSize").toDouble())},
                                 {"tracks", tracks.mid(0, 30)}}, true);
    });
}
