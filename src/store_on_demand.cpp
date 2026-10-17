// Store 的按需加载单元：request(key) 的四个 key（lyrics / playlist / search / comments）与相似歌曲种子。
// 取数一律经 QueryCache（stale-while-revalidate + 在途去重 + 落盘）：同一首歌的歌词、同一个歌单、
// 同一个搜索词，只在过期后才重新请求。首屏管线在 store.cpp；形状仍以 scripts/snapshot.py 为准。
#include "store.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QDateTime>
#include <QSet>
#include <QUrlQuery>
#include <memory>

using namespace ncm;

namespace {
// HTTP 200 不等于业务成功：网易用 code != 200（如 -460）表示限流/错误。
// 这种回包不能当成空结果交给缓存，否则会被当成「没有歌词/没有搜索结果」长期保留。
bool businessOk(const QJsonObject &resp)
{
    const QJsonValue code = resp.value("code");
    return code.isUndefined() || code.toInt() == 200;
}

constexpr int kCommentPageSize = 20;

// 全部评论按 offset 对外（comments:<id>:<offset>），v2 接口却按 cursor 翻页：cursor 是上一页
// 最后一条的时间。它单独存进 QueryCache，与页面同寿命，缓存命中时也能接着翻页。
QString commentCursorKey(double songId, int offset)
{
    return QStringLiteral("commentCursor:%1:%2").arg(qlonglong(songId)).arg(offset);
}

QUrlQuery commentPageQuery(double songId, int offset, const QString &cursor)
{
    QUrlQuery q = query({{"threadId", QStringLiteral("R_SO_4_%1").arg(qlonglong(songId))},
                         {"pageNo", QString::number(offset / kCommentPageSize + 1)},
                         {"pageSize", QString::number(kCommentPageSize)},
                         {"sortType", "3"}});
    if (!cursor.isEmpty())
        q.addQueryItem(QStringLiteral("cursor"), cursor);
    return q;
}
} // namespace

void Store::maybeFetchSimilar()
{
    if (m_similarFired || m_similarSeed.isEmpty())
        return;
    m_similarFired = true;
    fetch("api/v1/discovery/simiSong",
          query({{"songid", m_similarSeed.value("id").toString()}, {"limit", "20"}}), false,
          [this](const QJsonDocument &doc, bool /*ok*/) {
        QVariantMap similar{{"seed", m_similarSeed}, {"tracks", ncmTracks(arr(body(doc), "songs"))}};
        put("similar", similar, true);
    });
}

// 私人漫游续取：接口每次给一小批新歌，追加到 personal.fm.tracks（按 id 去重）。
// 每次都要新的一批，所以不经 QueryCache；失败时什么都不改，界面下次快放完时再要
void Store::requestMoreFm()
{
    fetch("api/v1/radio/get", {}, true, [this](const QJsonDocument &doc, bool ok) {
        if (!ok)
            return;
        QVariantList tracks = m_personal.value("fm").toMap().value("tracks").toList();
        QSet<double> seen;
        for (const QVariant &t : std::as_const(tracks))
            seen.insert(t.toMap().value("id").toDouble());
        for (const QVariant &t : ncmTracks(arr(body(doc), "data"))) {
            if (!seen.contains(t.toMap().value("id").toDouble()))
                tracks.append(t);
        }
        put("fm", QVariantMap{{"tracks", tracks}}, true);
    });
}

void Store::request(const QString &key)
{
    // 按 key 前缀分派。新增按需接口只在这里加一行（以及 store.h / 数据骨架 / 快照）。
    using Handler = std::function<void(Store &, const QString &)>;
    static const QList<QPair<QString, Handler>> routes = {
        {QStringLiteral("lyrics:"), [](Store &s, const QString &k) {
             s.requestLyrics(k, k.section(u':', 1).toDouble()); }},
        {QStringLiteral("playlist:"), [](Store &s, const QString &k) {
             s.requestPlaylist(k, k.section(u':', 1).toDouble()); }},
        {QStringLiteral("searchUsers:"), [](Store &s, const QString &k) {
             s.requestSearchUsers(k, k.section(u':', 1)); }},
        {QStringLiteral("searchRadios:"), [](Store &s, const QString &k) {
             s.requestSearchRadios(k, k.section(u':', 1)); }},
        {QStringLiteral("search:"), [](Store &s, const QString &k) {
             s.requestSearch(k, k.section(u':', 1)); }},
        {QStringLiteral("comments:"), [](Store &s, const QString &k) {
             const QStringList parts = k.split(u':');
             if (parts.size() >= 3)
                 s.requestCommentPage(k, parts.at(1).toDouble(), parts.at(2).toInt());
             else
                 s.requestComments(k, parts.value(1).toDouble()); }},
        {QStringLiteral("artistSongs:"), [](Store &s, const QString &k) {
             const QStringList parts = k.split(u':');
             s.requestArtistSongs(k, parts.value(1).toDouble(), parts.value(2).toInt()); }},
        {QStringLiteral("artist:"), [](Store &s, const QString &k) {
             s.requestArtist(k, k.section(u':', 1).toDouble()); }},
        {QStringLiteral("user:"), [](Store &s, const QString &k) {
             s.requestUser(k, k.section(u':', 1).toDouble()); }},
        {QStringLiteral("mv:"), [](Store &s, const QString &k) {
             s.requestMv(k, k.section(u':', 1).toDouble()); }},
        {QStringLiteral("album:"), [](Store &s, const QString &k) {
             s.requestAlbum(k, k.section(u':', 1).toDouble()); }},
        {QStringLiteral("inboxThread:"), [](Store &s, const QString &k) {
             const QStringList parts = k.split(u':');
             s.requestInboxThread(parts.value(1).toDouble(), parts.value(2).toLongLong()); }},
        {QStringLiteral("inbox:"), [](Store &s, const QString &k) {
             s.requestInbox(k.section(u':', 1).toInt()); }},
        {QStringLiteral("inbox"), [](Store &s, const QString &k) {
             if (k == QLatin1String("inbox")) s.requestInbox(0); }},
        {QStringLiteral("listen:total"), [](Store &s, const QString &k) {
             if (k == QLatin1String("listen:total")) s.requestListenTotal(); }},
        {QStringLiteral("listen:week"), [](Store &s, const QString &) {
             s.requestListenRealtime(QStringLiteral("week")); }},
        {QStringLiteral("listen:month"), [](Store &s, const QString &) {
             s.requestListenRealtime(QStringLiteral("month")); }},
        {QStringLiteral("listenRank:"), [](Store &s, const QString &k) {
             s.requestListenRank(k.section(u':', 1)); }},
        {QStringLiteral("notifications"), [](Store &s, const QString &k) {
             if (k == QLatin1String("notifications")) s.requestNotifications(); }},
        {QStringLiteral("userFollows:"), [](Store &s, const QString &k) {
             const QStringList parts = k.split(u':');
             s.requestUserFollows(parts.value(1).toDouble(), parts.value(2).toInt()); }},
        {QStringLiteral("userFolloweds:"), [](Store &s, const QString &k) {
             const QStringList parts = k.split(u':');
             s.requestUserFolloweds(parts.value(1).toDouble(), parts.value(2).toInt()); }},
        {QStringLiteral("userLevel"), [](Store &s, const QString &k) {
             if (k == QLatin1String("userLevel")) s.requestUserLevel(); }},
        {QStringLiteral("userSubcount"), [](Store &s, const QString &k) {
             if (k == QLatin1String("userSubcount")) s.requestUserSubcount(); }},
        {QStringLiteral("commentReplies:"), [](Store &s, const QString &k) {
             const QStringList parts = k.split(u':');
             s.requestCommentReplies(parts.value(1).toDouble(), parts.value(2).toDouble(),
                                     parts.value(3).toLongLong()); }},
        {QStringLiteral("lyricsNew:"), [](Store &s, const QString &k) {
             s.requestLyricsNew(k.section(u':', 1).toDouble()); }},
        {QStringLiteral("fm:next"), [](Store &s, const QString &k) {
             if (k == QLatin1String("fm:next")) s.requestMoreFm(); }},
    };

    for (const auto &route : routes) {
        if (key != route.first && !key.startsWith(route.first))
            continue;
        route.second(*this, key);
        return;
    }
    qWarning() << "unknown store key:" << key.section(u':', 0, 0);
}


// 歌词：单槽（lyricsTrackId + lyrics），QML 只看当前这首；缓存按歌分开，切回来直接命中
void Store::requestLyrics(const QString &key, double songId)
{
    m_cache.query(key,
        [this, songId](QueryCache::Done done) {
            m_client->get("api/song/lyric", query({{"id", num(songId)}, {"lv", "1"}, {"tv", "-1"}}), false,
                          [done](const QJsonDocument &doc, bool ok) {
                const QJsonObject resp = body(doc);
                const QString lrc = resp.value("lrc").toObject().value("lyric").toString();
                const QString tlyric = resp.value("tlyric").toObject().value("lyric").toString();
                done(ncmParseLrc(lrc, tlyric), ok && businessOk(resp));
            });
        },
        [this, songId](const QVariant &lines) {
            put("lyricsTrackId", songId, false);
            put("lyrics", lines, false);
        });
}

// 歌单曲目：累积在 personal.playlistDetails[<id>]
void Store::requestPlaylist(const QString &key, double playlistId)
{
    m_cache.query(key,
        [this, playlistId](QueryCache::Done done) {
            fetchPlaylistTracks(playlistId, true,
                                [done, playlistId](const QVariantList &tracks, const QJsonObject &pl, bool ok) {
                if (!ok || pl.isEmpty()) {
                    done({}, false);
                    return;
                }
                done(QVariantMap{{"tracks", tracks},
                                 {"description", ncmFirstLine(pl.value("description").toString())},
                                 {"name", pl.value("name").toString()},
                                 {"cover", ncmSized(pl.value("coverImgUrl"), 400)},
                                 {"count", pl.value("trackCount").toDouble()},
                                 {"id", playlistId}},
                     true);
            });
        },
        [this, playlistId](const QVariant &detail) {
            QVariantMap details = m_personal.value("playlistDetails").toMap();
            details.insert(num(playlistId), detail);
            put("playlistDetails", details, true);
            // 推荐歌单的卡片也换成完整曲目：从列表页返回后再点一次不用重新等
            patchPlaylistCard(playlistId, detail.toMap().value("tracks").toList(),
                              detail.toMap().value("description").toString());
        });
}

// 搜索：四类结果并发取，全部回来后作为一个值交给缓存；页面先拿到同词的旧结果（若有）
void Store::requestSearch(const QString &key, const QString &raw)
{
    const quint64 generation = ++m_searchGeneration;
    m_cache.query(key,
        [this, raw](QueryCache::Done done) {
            struct Pending {
                QVariantMap result;
                int left = 5;
                bool ok = true;
            };
            auto pending = std::make_shared<Pending>();
            pending->result = QVariantMap{{"query", raw}};
            const auto part = [this, raw, pending, done](const char *type, const char *limit, const char *field,
                                                          std::function<QVariant(const QJsonObject &)> pick) {
                m_client->get("api/search/get", query({{"s", raw}, {"type", type}, {"limit", limit}}), false,
                              [this, pending, done, field, pick](const QJsonDocument &doc, bool ok) {
                    const QJsonObject resp = body(doc);
                    pending->result.insert(QLatin1String(field), pick(resp.value("result").toObject()));
                    pending->ok = pending->ok && ok && businessOk(resp);
                    if (--pending->left != 0)
                        return;

                    // 搜索接口的单曲没有专辑封面（album.picUrl 为 null），用歌曲详情补齐封面、音质、MV。
                    // 详情失败时保留搜索接口的原值，搜索本身仍然可用。
                    const QVariantList songs = pending->result.value("songs").toList();
                    QList<double> ids;
                    ids.reserve(songs.size());
                    for (const QVariant &v : songs)
                        ids.append(v.toMap().value("id").toDouble());
                    fetchSongDetails(ids, false,
                                     [pending, done, songs](const QHash<double, QVariantMap> &details, bool detailsOk) {
                        if (detailsOk && !details.isEmpty()) {
                            QVariantList enriched;
                            enriched.reserve(songs.size());
                            for (const QVariant &v : songs) {
                                const QVariantMap original = v.toMap();
                                enriched.append(details.value(original.value("id").toDouble(), original));
                            }
                            pending->result.insert(QStringLiteral("songs"), enriched);
                        }
                        done(pending->result, pending->ok);
                    });
                });
            };
            const auto list = [](const QJsonObject &r, const char *k, QVariantMap (*fn)(const QJsonObject &)) {
                QVariantList out;
                for (const QJsonValue &v : arr(r, k))
                    out.append(fn(v.toObject()));
                return QVariant(out);
            };
            // 单曲给 100 首（页面自己按高度截）；艺人/歌单/专辑各 30 个，足够一屏与「更多」展开
            part("1", "100", "songs", [](const QJsonObject &r) { return QVariant(ncmTracks(arr(r, "songs"))); });
            part("100", "30", "artists", [list](const QJsonObject &r) { return list(r, "artists", ncmSearchArtist); });
            part("1000", "30", "playlists", [list](const QJsonObject &r) { return list(r, "playlists", ncmSearchPlaylist); });
            part("10", "30", "albums", [list](const QJsonObject &r) { return list(r, "albums", ncmSearchAlbum); });
            part("1004", "12", "mvs", [list](const QJsonObject &r) { return list(r, "mvs", ncmSearchMv); });
        },
        [this, generation](const QVariant &result) {
            // 用户已经输入了更新的搜索词：旧请求晚到也不能覆盖当前结果
            if (generation == m_searchGeneration)
                put("search", result, false);
        });
}

// 全部评论分页：v2 接口按时间倒序（sortType=3），对外仍是每页 20 条的 offset 分页，10 分钟缓存
void Store::requestCommentPage(const QString &key, double songId, int offset)
{
    m_cache.query(key,
        [this, songId, offset](QueryCache::Done done) {
            // 正常翻页时上一页的 cursor 已记录；冷启动直接要第 N 页时从头顺序补游标，
            // 不能把第一页当成第 N 页。走到目标页之前的回包只用于取 cursor。
            const auto fetch = std::make_shared<std::function<void(int, const QString &)>>();
            *fetch = [this, songId, offset, done, fetch](int pageOffset, const QString &cursor) {
                m_client->get(QStringLiteral("api/v2/resource/comments"),
                              commentPageQuery(songId, pageOffset, cursor), false,
                              [this, songId, offset, done, fetch, pageOffset](const QJsonDocument &doc, bool ok) {
                    const QJsonObject resp = body(doc);
                    if (!ok || resp.value("code").toInt() != 200) {
                        done({}, false);
                        return;
                    }
                    const QJsonObject data = resp.value("data").toObject();
                    const QJsonArray comments = data.value("comments").toArray();
                    if (pageOffset != offset) {
                        if (comments.isEmpty()) {
                            done({}, false);
                            return;
                        }
                        if (offset < pageOffset + int(comments.size())) {
                            // 目标页在这批评论中间：用前一条的时间当游标
                            const QJsonObject before = comments.at(offset - pageOffset - 1).toObject();
                            (*fetch)(offset, QString::number(qlonglong(before.value("time").toDouble())));
                            return;
                        }
                        const QString next = data.value("cursor").toString();
                        if (next.isEmpty()) {
                            done({}, false);
                            return;
                        }
                        (*fetch)(pageOffset + int(comments.size()), next);
                        return;
                    }
                    QVariantList items;
                    for (const QJsonValue &v : comments)
                        items.append(ncmComment(v.toObject()));
                    const double total = data.value("totalCount").toDouble();
                    const bool more = data.value("hasMore").toBool();
                    const QString next = data.value("cursor").toString();
                    if (more && !items.isEmpty() && !next.isEmpty())
                        m_cache.remember(commentCursorKey(songId, offset + int(items.size())), next);
                    done(QVariantMap{{"songId", songId},
                                     {"offset", offset},
                                     {"total", total},
                                     {"more", more},
                                     {"items", items}},
                         true);
                });
            };
            const QString cursor = offset > 0
                                       ? m_cache.peek(commentCursorKey(songId, offset)).toString()
                                       : QString();
            (*fetch)(cursor.isEmpty() ? 0 : offset, cursor);
        },
        [this](const QVariant &page) { put("commentPage", page, false); });
}

// 热门评论：v2 按热度排序，写回 data.comments（10 分钟新鲜期，落盘）；保持接口顺序，最多 10 条
void Store::requestComments(const QString &key, double songId)
{
    m_cache.query(key,
        [this, songId](QueryCache::Done done) {
            m_client->get(QStringLiteral("api/v2/resource/comments"),
                          query({{"threadId", QStringLiteral("R_SO_4_%1").arg(qlonglong(songId))},
                                 {"pageNo", "1"}, {"pageSize", "10"}, {"sortType", "2"}}),
                          false,
                          [done, songId](const QJsonDocument &doc, bool ok) {
                const QJsonObject resp = body(doc);
                done(ncmHotComments(resp, songId), ok && businessOk(resp));
            });
        },
        [this](const QVariant &comments) { put("comments", comments, false); });
}


// 歌手页：热歌 50 首 + 专辑列表（一次 200 张；普通歌手够用）。两个请求独立，
// 专辑失败仍返回歌手与热歌，页面不会整页空白。
void Store::requestArtist(const QString &key, double artistId)
{
    m_cache.query(key,
        [this, artistId](QueryCache::Done done) {
            struct State {
                QVariantMap artist;
                QVariantList albums;
                int left = 2;
                bool artistOk = false;
            };
            auto state = std::make_shared<State>();
            const auto finish = [state, done] {
                if (!state->artistOk) {
                    done({}, false);
                    return;
                }
                state->artist.insert("albums", state->albums);
                done(state->artist, true);
            };

            m_client->get(QStringLiteral("api/v1/artist/%1").arg(qlonglong(artistId)), {}, false,
                          [state, finish](const QJsonDocument &doc, bool ok) {
                const QJsonObject a = body(doc).value("artist").toObject();
                if (ok && !a.isEmpty()) {
                    QVariantList aliases;
                    for (const QJsonValue &v : arr(a, "alias"))
                        aliases.append(v.toString());
                    state->artist = QVariantMap{
                        {"id", a.value("id").toDouble()},
                        {"name", a.value("name").toString()},
                        {"cover", ncmSized(a.value("picUrl"), 400)},
                        {"alias", aliases.isEmpty() ? QString() : aliases.first().toString()},
                        {"description", a.value("briefDesc").toString()},
                        {"followed", a.value("followed").toBool()},
                        {"musicSize", a.value("musicSize").toDouble()},
                        {"albumSize", a.value("albumSize").toDouble()},
                        {"tracks", ncmTracks(arr(body(doc), "hotSongs"))},
                    };
                    state->artistOk = true;
                }
                if (--state->left == 0)
                    finish();
            });

            m_client->get(QStringLiteral("api/artist/albums/%1").arg(qlonglong(artistId)),
                          query({{"limit", "200"}, {"offset", "0"}}), false,
                          [state, finish](const QJsonDocument &doc, bool ok) {
                if (ok) {
                    for (const QJsonValue &v : arr(body(doc), "hotAlbums")) {
                        const QJsonObject a = v.toObject();
                        const double ms = a.value("publishTime").toDouble();
                        state->albums.append(QVariantMap{
                            {"id", a.value("id").toDouble()},
                            {"name", a.value("name").toString()},
                            {"cover", ncmSized(a.value("picUrl"), 300)},
                            {"year", ms > 0 ? QDateTime::fromSecsSinceEpoch(qlonglong(ms / 1000)).date().year() : 0},
                            {"count", a.value("size").toDouble()},
                        });
                    }
                }
                if (--state->left == 0)
                    finish();
            });
        },
        [this](const QVariant &artist) { put("artist", artist, false); });
}

// 专辑页：专辑信息 + 完整曲目（接口一次返回全部）
void Store::requestAlbum(const QString &key, double albumId)
{
    m_cache.query(key,
        [this, albumId](QueryCache::Done done) {
            m_client->get(QStringLiteral("api/v1/album/%1").arg(qlonglong(albumId)), {}, false,
                          [done](const QJsonDocument &doc, bool ok) {
                const QJsonObject resp = body(doc);
                const QJsonObject a = resp.value("album").toObject();
                if (!ok || a.isEmpty()) {
                    done({}, false);
                    return;
                }
                const QJsonArray artists = arr(a, "artists");
                const QJsonObject first = artists.isEmpty() ? a.value("artist").toObject()
                                                            : artists.first().toObject();
                done(QVariantMap{
                         {"id", a.value("id").toDouble()},
                         {"name", a.value("name").toString()},
                         {"artist", first.value("name").toString()},
                         {"artistId", first.value("id").toDouble()},
                         {"cover", ncmSized(a.value("picUrl"), 400)},
                         {"description", a.value("description").toString()},
                         {"company", a.value("company").toString()},
                         {"subscribed", a.value("isSub").toBool()},
                         {"publishTime", a.value("publishTime").toDouble()},
                         {"tracks", ncmTracks(arr(resp, "songs"))},
                     },
                     true);
            });
        },
        [this](const QVariant &album) { put("album", album, false); });
}


// 歌手全部歌曲分页：歌手页先用 artist:<id> 的热歌 50 首，往下翻时用这个 key。
// 排序固定按热度；接口一次 50 首，total/more 由响应给出。
void Store::requestArtistSongs(const QString &key, double artistId, int offset)
{
    m_cache.query(key,
        [this, artistId, offset](QueryCache::Done done) {
            m_client->get(QStringLiteral("api/v1/artist/songs"),
                          query({{"id", num(artistId)}, {"limit", "50"},
                                 {"offset", QString::number(offset)}, {"order", "hot"}}), false,
                          [done, artistId, offset](const QJsonDocument &doc, bool ok) {
                const QJsonObject resp = body(doc);
                const QVariantList tracks = ncmTracks(arr(resp, "songs"));
                const double total = resp.value("total").toDouble();
                done(QVariantMap{{"artistId", artistId},
                                 {"offset", offset},
                                 {"total", total},
                                 {"more", resp.value("more").toBool()
                                              || (total > 0 && offset + tracks.size() < total)},
                                 {"tracks", tracks}},
                     ok && resp.value("code").toInt() == 200);
            });
        },
        [this](const QVariant &page) { put("artistSongs", page, false); });
}


// MV：详情走缓存；播放地址是签名 URL，过期即失效，每次打开都重新取（不进缓存）
void Store::requestMv(const QString &key, double mvId)
{
    // 同一个 MV 再打开：先清掉上一次的地址，新地址回来前界面不能拿着死链
    const QVariantMap shown = m_data.value(QStringLiteral("mv")).toMap();
    if (shown.value("id").toDouble() == mvId && !shown.value("url").toString().isEmpty()) {
        QVariantMap cleared = shown;
        cleared.insert("url", QString{});
        cleared.insert("br", 0.0);
        cleared.insert("size", 0.0);
        cleared.insert("expires", 0.0);
        put("mv", cleared, false);
    }

    struct State {
        QVariantMap mv;
        QVariantMap url;
        bool detailOk = false;
        bool urlSettled = false;
    };
    auto state = std::make_shared<State>();
    const auto deliver = [this, state] {
        if (!state->detailOk || !state->urlSettled)
            return;
        QVariantMap mv = state->mv;
        mv.insert("url", state->url.value("url", QString{}));
        mv.insert("br", state->url.value("br", 0.0));
        mv.insert("size", state->url.value("size", 0.0));
        mv.insert("expires", state->url.value("expires", 0.0));
        put("mv", mv, false);
    };

    m_cache.query(key,
        [this, mvId](QueryCache::Done done) {
            m_client->postWeapi("v1/mv/detail", QJsonObject{{"id", num(mvId)}}, true,
                                [done](const QJsonDocument &doc, bool ok) {
                const QJsonObject root = body(doc);
                const QJsonObject d = root.value("data").toObject();
                if (!ok || root.value("code").toInt() != 200 || d.isEmpty()) {
                    done({}, false);
                    return;
                }
                done(QVariantMap{
                    {"id", d.value("id").toDouble()},
                    {"name", d.value("name").toString()},
                    {"cover", ncmSized(d.value("cover"), 400)},
                    {"artist", d.value("artistName").toString()},
                    {"artistId", d.value("artistId").toDouble()},
                    {"duration", std::floor(d.value("duration").toDouble() / 1000)},
                    {"playCount", d.value("playCount").toDouble()},
                    {"subCount", d.value("subCount").toDouble()},
                    {"shareCount", d.value("shareCount").toDouble()},
                    {"commentCount", d.value("commentCount").toDouble()},
                    {"publishTime", d.value("publishTime").toString()},
                    {"description", d.value("desc").toString()},
                    {"subscribed", root.value("subed").toBool()},
                }, true);
            });
        },
        [state, deliver](const QVariant &value) {
            state->mv = value.toMap();
            state->detailOk = true;
            deliver();
        });

    // 地址失败只让 url 为空，详情照常落地
    m_cache.refresh(QStringLiteral("mvUrl:%1").arg(num(mvId)),
        [this, mvId](QueryCache::Done done) {
            m_client->postWeapi("song/enhance/play/mv/url",
                                QJsonObject{{"id", num(mvId)}, {"r", 1080}}, true,
                                [done](const QJsonDocument &doc, bool ok) {
                const QJsonObject root = body(doc);
                const QJsonObject d = root.value("data").toObject();
                if (!ok || root.value("code").toInt() != 200
                    || d.value("code").toInt() != 200) {
                    done(QVariantMap{}, true);
                    return;
                }
                done(QVariantMap{{"url", d.value("url").toString()},
                                 {"br", d.value("r").toDouble()},
                                 {"size", d.value("size").toDouble()},
                                 {"expires", d.value("expi").toDouble()}}, true);
            });
        },
        [state, deliver](const QVariant &value) {
            state->url = value.toMap();
            state->urlSettled = true;
            deliver();
        });
}


// 用户搜索：Cloudsearch 的 userprofiles；与主搜索分开按需取，避免每次搜索多发请求
void Store::requestSearchUsers(const QString &key, const QString &raw)
{
    m_cache.query(key,
        [this, raw](QueryCache::Done done) {
            m_client->get("api/cloudsearch/pc",
                          query({{"s", raw}, {"type", "1002"}, {"limit", "20"}, {"offset", "0"}}), false,
                          [done, raw](const QJsonDocument &doc, bool ok) {
                const QJsonObject resp = body(doc);
                QVariantList items;
                for (const QJsonValue &v : arr(resp.value("result").toObject(), "userprofiles")) {
                    const QJsonObject u = v.toObject();
                    items.append(QVariantMap{
                        {"id", u.value("userId").toDouble()},
                        {"name", u.value("nickname").toString()},
                        {"cover", ncmSized(u.value("avatarUrl"), 300)},
                        {"followed", u.value("followed").toBool()},
                        {"fans", u.value("followeds").toDouble()},
                        {"follows", u.value("follows").toDouble()},
                        {"playlists", u.value("playlistCount").toDouble()},
                        {"signature", u.value("signature").toString()},
                    });
                }
                done(QVariantMap{{"query", raw}, {"items", items}},
                     ok && resp.value("code").toInt() == 200);
            });
        },
        [this](const QVariant &result) { put("searchUsers", result, false); });
}

// 电台搜索：Cloudsearch 的 djRadios
void Store::requestSearchRadios(const QString &key, const QString &raw)
{
    m_cache.query(key,
        [this, raw](QueryCache::Done done) {
            m_client->get("api/cloudsearch/pc",
                          query({{"s", raw}, {"type", "1009"}, {"limit", "20"}, {"offset", "0"}}), false,
                          [done, raw](const QJsonDocument &doc, bool ok) {
                const QJsonObject resp = body(doc);
                QVariantList items;
                for (const QJsonValue &v : arr(resp.value("result").toObject(), "djRadios")) {
                    const QJsonObject r = v.toObject();
                    items.append(QVariantMap{
                        {"id", r.value("id").toDouble()},
                        {"name", r.value("name").toString()},
                        {"cover", ncmSized(r.value("picUrl"), 300)},
                        {"description", r.value("desc").toString()},
                        {"host", r.value("dj").toObject().value("nickname").toString()},
                        {"programCount", r.value("programCount").toDouble()},
                        {"subCount", r.value("subCount").toDouble()},
                        {"playCount", r.value("playCount").toDouble()},
                    });
                }
                done(QVariantMap{{"query", raw}, {"items", items}},
                     ok && resp.value("code").toInt() == 200);
            });
        },
        [this](const QVariant &result) { put("searchRadios", result, false); });
}
