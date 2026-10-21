// Store 的首屏账号链：匿名首页、账号、每日推荐、红心列表、心动、私人雷达、最近播放、云盘分页。
#include "store.h"

#include "json_util.h"
#include "normalize.h"
#include "sdk/account_feed.h"
#include "sdk/track.h"

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
            const auto parsed = sdk::parseToplists(body(doc));
            if (parsed) {
                toplists.reserve(parsed.value->size());
                for (const sdk::Toplist &toplist : *parsed.value) {
                    QVariantList preview;
                    preview.reserve(toplist.preview.size());
                    for (const sdk::ToplistPreview &item : toplist.preview)
                        preview.append(QVariantMap{{"name", item.name},
                                                   {"artist", item.artist}});
                    toplists.append(QVariantMap{
                        {"id", toplist.id},
                        {"name", toplist.name},
                        {"cover", ncmSized(toplist.cover, 300)},
                        {"update", toplist.update},
                        {"count", toplist.count},
                        {"preview", preview},
                    });
                }
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
            const auto parsed = sdk::parseHotSearches(body(doc));
            if (parsed) {
                hot.reserve(parsed.value->size());
                for (const sdk::HotSearch &item : *parsed.value)
                    hot.append(QVariantMap{{"word", item.word},
                                           {"note", item.note},
                                           {"score", item.score}});
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
        // 匿名 -> 登录：匿名期间缓存里的评论 liked 全是 false，不能继续喂给已登录的界面。
        const bool accountAppeared = ok && uid > 0 && m_accountUid <= 0;
        const bool accountGone = ok && uid <= 0
            && (m_accountUid > 0
                || (m_personal.value("account").isValid() && !m_personal.value("account").isNull()));
        if (accountChanged || accountAppeared || accountGone) {
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
            const QVariantMap oldAccount = m_personal.value("account").toMap();
            const bool sameAccount = oldAccount.value("id").toDouble() == uid;
            const QString oldPendant =
                sameAccount ? oldAccount.value("avatarPendant").toString() : QString();
            QVariantMap account = ncmProfile(profile, 96, 1200);
            account.insert("vip", profile.value("vipType").toDouble() > 0);
            // Keep the visible pendant across an account refresh; the async
            // pendant request will replace or clear it a moment later.
            if (!oldPendant.isEmpty() && account.value("avatarPendant").toString().isEmpty())
                account.insert("avatarPendant", oldPendant);
            put("account", account, true);
            requestAvatarPendant();
            requestFriendListening();
            // 全部红心曲目 id（播放条与行内小红心）；没有红心时接口也可能回 null。
            // 快照是在请求前算的：期间点过红心的歌用本地状态覆盖，别让旧快照把心点回去。
            const quint64 likeGeneration = m_likeGeneration;
            fetch("api/song/like/get", query({{"uid", num(uid)}}), true,
                  [this, likeGeneration](const QJsonDocument &doc, bool /*ok*/) {
                QVariantList ids;
                QSet<double> serverIds;
                const auto parsed = sdk::parseLikedIds(body(doc));
                if (parsed) {
                    ids.reserve(parsed.value->ids.size());
                    for (double songId : parsed.value->ids) {
                        ids.append(songId);
                        serverIds.insert(songId);
                    }
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
        const auto parsed = sdk::parseTracks(arr(body(doc), "data"));
        if (!parsed)
            return;
        m_requests.begin(QStringLiteral("fm"));   // 整批换：在途的续取不再属于这条流
        put("fm", QVariantMap{{"tracks", sdk::tracksToVariantList(*parsed.value)}}, true);
    });
}

void Store::fetchAnonymousPlaylists()
{
    // snapshot.py 匿名分支：公共推荐歌单，曲目与简介留空，点开再取
    fetch("api/personalized/playlist", query({{"limit", "12"}}), false, [this](const QJsonDocument &doc, bool /*ok*/) {
        const auto parsed = sdk::parsePlaylistCards(arr(body(doc), "result"), "picUrl");
        if (!parsed)
            return;
        QVariantList cards;
        cards.reserve(parsed.value->size());
        for (const sdk::PlaylistCard &parsedCard : *parsed.value) {
            QVariantMap card = sdk::playlistCardToVariantMap(parsedCard);
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
    // 6:00 前取到的仍是前一天的列表：日期标签跟着服务日走，不跟回包时的墙钟。
    const QDateTime now = m_clock();
    const QDate day = dailyServiceDay(now);
    m_dailyTriedAt = now;
    // 历史日推看过（或从缓存恢复了）：日期表跟着重取。换日或隔天重启后，表里才有前一天
    if (!m_personal.value("dailyHistory").isNull())
        requestDailyHistory();
    const quint64 token = m_requests.begin(QStringLiteral("daily"));
    fetchAlways("api/v3/discovery/recommend/songs", {}, true,
                [this, day, token](const QJsonDocument &doc, bool ok) {
        // 换日前后各有一个请求：旧服务日的回包不能盖掉新的
        if (!m_requests.current(QStringLiteral("daily"), token))
            return;
        // 没取到：旧列表留着，m_dailyDay 不动，checkDailyRollover 过一阵再试
        const QJsonValue code = doc.object().value("code");
        if (ok && (code.isUndefined() || code.toInt() == 200)) {
            // Python 的 strftime("%m月%d日").lstrip("0")：去掉月份的补零
            QString date = day.toString(QStringLiteral("MM月dd日"));
            if (date.startsWith(u'0'))
                date.remove(0, 1);
            const QJsonObject data = body(doc).value("data").toObject();
            // 推荐理由按 songId 对上：data.recommendReasons 里每条 { songId, reason }
            QHash<double, QString> reasons;
            for (const QJsonValue &r : arr(data, "recommendReasons")) {
                const QJsonObject o = r.toObject();
                reasons.insert(o.value("songId").toDouble(), o.value("reason").toString());
            }
            // 每日推荐的 privileges 与 dailySongs 分开发（data 或顶层），按 id 对上后
            // 再把推荐理由补进同一条曲目
            QJsonArray privileges = arr(data, "privileges");
            if (privileges.isEmpty())
                privileges = arr(body(doc), "privileges");
            const auto parsed = sdk::parseTracks(take(arr(data, "dailySongs"), 30), privileges);
            if (!parsed)
                return;
            m_dailyDay = day;
            QVariantList tracks;
            tracks.reserve(parsed.value->size());
            for (const sdk::Track &track : *parsed.value)
                tracks.append(sdk::trackToVariantMap(
                    track, reasons.value(static_cast<double>(track.id))));
            put("daily", QVariantMap{
                             {"date", date},
                             {"day", QString::number(day.day())},
                             {"tracks", tracks},
                         }, true);
        }
    });
}

void Store::requestDailyHistory()
{
    const quint64 token = m_requests.begin(QStringLiteral("dailyHistory"));
    fetchAlways("api/discovery/recommend/songs/history/recent", {}, true,
                [this, token](const QJsonDocument &doc, bool ok) {
        if (!m_requests.current(QStringLiteral("dailyHistory"), token))
            return;
        const QJsonValue code = doc.object().value("code");
        if (!ok || (!code.isUndefined() && code.toInt() != 200)) {
            emit requestFailed(QStringLiteral("dailyHistory"), QStringLiteral("暂时取不到历史日推"));
            return;
        }
        const QJsonObject data = body(doc).value("data").toObject();
        QVariantList dates;
        for (const QJsonValue &value : arr(data, "dates"))
            dates.append(value.toString());
        // 换日期表与说明；已经取回的某天曲目留着，掉出日期表的那些丢掉（它们也存在缓存里，不丢会越积越多）
        QVariantMap history = m_personal.value("dailyHistory").toMap();
        QVariantMap byDate;
        const QVariantMap fetched = history.value("byDate").toMap();
        for (const QVariant &date : std::as_const(dates))
            if (fetched.contains(date.toString()))
                byDate.insert(date.toString(), fetched.value(date.toString()));
        history.insert("dates", dates);
        history.insert("description", data.value("description").toString());
        history.insert("noHistoryMessage", data.value("noHistoryMessage").toString());
        history.insert("byDate", byDate);
        put("dailyHistory", history, true);
    });
}

void Store::requestDailyHistoryDate(const QString &date)
{
    if (date.isEmpty())
        return;
    const QString key = QStringLiteral("dailyHistory:") + date;
    const quint64 token = m_requests.begin(key);
    fetchAlways("api/discovery/recommend/songs/history/detail", query({{"date", date}}), true,
                [this, date, key, token](const QJsonDocument &doc, bool ok) {
        if (!m_requests.current(key, token))
            return;
        const QJsonValue code = doc.object().value("code");
        if (!ok || (!code.isUndefined() && code.toInt() != 200)) {
            emit requestFailed(key, QStringLiteral("暂时取不到这一天的推荐"));
            return;
        }
        // 网易偶尔回 200 却没有歌：当失败（可重试），不存成「这一天没有推荐」
        const auto parsed = sdk::parseTracks(arr(body(doc).value("data").toObject(), "songs"), {});
        if (!parsed || parsed.value->empty()) {
            emit requestFailed(key, QStringLiteral("暂时取不到这一天的推荐"));
            return;
        }
        // 只放这一天；日期表没取过就不写 dates，界面据此还会去要日期表
        QVariantMap history = m_personal.value("dailyHistory").toMap();
        QVariantMap byDate = history.value("byDate").toMap();
        byDate.insert(date, QVariantMap{{"tracks", sdk::tracksToVariantList(*parsed.value)}});
        history.insert("byDate", byDate);
        put("dailyHistory", history, true);
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
            const auto parsedCard = sdk::parsePlaylistCard(lists.at(i).toObject(), "coverImgUrl");
            if (!parsedCard)
                continue;
            const bool owned = static_cast<double>(parsedCard.value->creatorId) == uid;
            (owned ? &mine : &subscribed)->append(
                sdk::playlistCardToVariantMap(*parsedCard.value));
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
        putLiked(QVariantMap{{"id", liked.value("id").toDouble()},
                             {"count", liked.value("trackCount").toDouble()},
                             {"tracks", tracks}});
    });
}

// snapshot.py：心动模式以红心第一首为种子。红心列表只从这里写，首位一变（新点了红心、
// 取消了第一首）心动模式就换种子重取；红心为空就没有这一块。
void Store::putLiked(const QVariantMap &liked)
{
    put("liked", liked, true);
    const QVariantList tracks = liked.value("tracks").toList();
    const QVariantMap seed = tracks.isEmpty() ? QVariantMap{} : tracks.first().toMap();
    const double seedId = seed.value("id").toDouble();
    if (seedId == m_heartSeedId)
        return;
    m_heartSeedId = seedId;
    if (seed.isEmpty()) {
        m_requests.begin(QStringLiteral("heart"));
        put("heart", QVariant::fromValue(nullptr), true);
    } else {
        fetchHeart(seed, liked.value("id").toDouble());
    }
}

void Store::fetchHeart(const QVariantMap &seed, double likedId)
{
    m_similarSeed = seed;
    maybeFetchSimilar();
    fetchHeartBatch(seed.value("id").toDouble(), likedId, [this, seed](const QVariantList &tracks, bool ok) {
        if (seed.value("id").toDouble() != m_heartSeedId)
            return;   // 回包前红心首位又换了：等新种子那一次
        if (!ok) {
            m_heartSeedId = 0;   // 首批没取到：不记这个种子，下次红心列表写入时再取
            return;
        }
        m_requests.begin(QStringLiteral("heart"));   // 整批换：在途的续取不再属于这条流
        put("heart", QVariantMap{{"seed", seed}, {"tracks", tracks.mid(0, 30)}}, true);
        // radar / tail 由 fetchAccount 独立发起，不挂在这条链尾
    });
}

// 心动模式续取：从正在放的这首接着推荐，种子不变
void Store::requestMoreHeart(double songId)
{
    const double likedId = m_personal.value("liked").toMap().value("id").toDouble();
    if (songId <= 0 || likedId <= 0 || !beginRefill("heart"))
        return;
    const quint64 generation = m_requests.token(QStringLiteral("heart"));
    fetchHeartBatch(songId, likedId, [this, generation](const QVariantList &batch, bool /*ok*/) {
        endRefill("heart", generation, batch);
    });
}

// 私人漫游与心动模式是流：personal.<stream>.tracks 有两种写法——整批换（刷新、换种子，
// 换时 m_requests.begin(stream) 开新一代）与续取追加。续取只接在发出时的那一代后面，
// 同一条流同时只续取一次；回包（成功或失败）一定结束这次续取。
bool Store::beginRefill(const char *stream)
{
    if (m_refilling.contains(QLatin1String(stream)))
        return false;
    m_refilling.insert(QLatin1String(stream));
    return true;
}

void Store::endRefill(const char *stream, quint64 generation, const QVariantList &batch)
{
    m_refilling.remove(QLatin1String(stream));
    if (!m_requests.current(QLatin1String(stream), generation))
        return;
    QVariantMap current = m_personal.value(QLatin1String(stream)).toMap();
    if (current.isEmpty())
        return;   // 期间退出登录或红心清空：没有可以接上的流
    QVariantList tracks = current.value("tracks").toList();
    QSet<double> seen;
    for (const QVariant &t : std::as_const(tracks))
        seen.insert(t.toMap().value("id").toDouble());
    for (const QVariant &t : batch) {
        if (!seen.contains(t.toMap().value("id").toDouble()))
            tracks.append(t);
    }
    if (tracks.size() == current.value("tracks").toList().size())
        return;
    current.insert("tracks", tracks);
    put(stream, current, true);
}

// 网易云的心动模式接口：从 songId 出发、混合红心歌单（likedId）与相似推荐的一批歌。失败时 ok = false
void Store::fetchHeartBatch(double songId, double likedId,
                            std::function<void(const QVariantList &, bool)> done)
{
    // fetchAlways：失败也要回调（ok = false），续取才能结束、首批失败才能重来
    fetchAlways("api/playmode/intelligence/list",
          query({{"songId", num(songId)},
                 {"type", "fromPlayAll"},
                 {"playlistId", num(likedId)},
                 {"startMusicId", num(songId)},
                 {"count", "1"}}), true,
          [this, done = std::move(done)](const QJsonDocument &doc, bool ok) {
        if (!requestFailure(doc, ok).isEmpty()) {
            done({}, false);
            return;
        }
        const QJsonObject resp = body(doc);
        QJsonArray songs;
        for (const QJsonValue &x : arr(resp, "data"))
            songs.append(x.toObject().value("songInfo").toObject());
        const auto parsed = sdk::parseTracks(songs, arr(resp, "privileges"));
        done(parsed ? sdk::tracksToVariantList(*parsed.value) : QVariantList{}, bool(parsed));
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
        const auto parsed = sdk::parsePlaylistCards(arr(body(doc), "recommend"), "picUrl");
        if (!parsed)
            return;
        QVariantList cards;
        cards.reserve(parsed.value->size());
        for (const sdk::PlaylistCard &parsedCard : *parsed.value) {
            QVariantMap card = sdk::playlistCardToVariantMap(parsedCard);
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
        const QJsonObject resp = body(doc);
        const QJsonObject record = resp.value("data").toObject();
        QJsonArray privilegeList = arr(record, "privileges");
        if (privilegeList.isEmpty())
            privilegeList = arr(resp, "privileges");
        QJsonArray songs;
        for (const QJsonValue &x : arr(record, "list"))
            songs.append(x.toObject().value("data").toObject());
        const auto parsed = sdk::parseTracks(songs, privilegeList);
        if (!parsed)
            return;
        put("recent", QVariantMap{{"total", record.value("total").toDouble()},
                                  {"tracks", sdk::tracksToVariantList(*parsed.value)}}, true);
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

void Store::refreshCloud()
{
    fetchCloudPage(std::make_shared<CloudState>());
}

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
