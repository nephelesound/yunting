// 匿名发现页的 Store 接缝：搜索联想、新歌/新碟/精品歌单/推荐 MV/节目、电台。
// SDK 负责 typed 解析；这里只做缓存、请求代号和 QML 兼容 map
//（时长秒、时间毫秒、图片原图 URL）。形状见 docs/API-FRONTEND.md「已实现的新增数据槽」。
#include "store.h"

#include "sdk/discovery.h"
#include "sdk/feed.h"
#include "sdk/radio.h"
#include "sdk/search_suggest.h"

#include <algorithm>

using namespace ncm;

namespace {
constexpr int kNewSongLimit = 20;
constexpr int kNewAlbumLimit = 30;
constexpr int kHighQualityLimit = 20;
constexpr int kRadioProgramLimit = 30;

struct Playability {
    bool playable;
    QString blockedBy;
};

// 与 ncmTrack 在没有 privilege 时的退回规则一致：只看 fee。
Playability playabilityForFee(int fee)
{
    if (fee == 4)
        return {false, QStringLiteral("paid")};
    if (fee == 1)
        return {false, QStringLiteral("vip")};
    return {true, QString()};
}

// 推荐新歌 → 队列曲目。字段含义与 ncmTrack 相同；cover 保持原图 URL。
QVariantMap newSongTrack(const sdk::NewSong &song)
{
    QStringList names;
    QVariantList artists;
    artists.reserve(song.artists.size());
    for (const sdk::NewSongArtist &artist : song.artists) {
        names.append(artist.name);
        artists.append(QVariantMap{{"id", static_cast<double>(artist.id)},
                                   {"name", artist.name}});
    }
    const Playability play = playabilityForFee(song.fee);
    return {
        {"id", static_cast<double>(song.songId)},
        {"name", song.name},
        {"artist", names.join(QStringLiteral(" / "))},
        {"artistId", artists.isEmpty() ? 0.0
                                       : static_cast<double>(song.artists.first().id)},
        {"artists", artists},
        {"albumId", static_cast<double>(song.albumId)},
        {"album", song.albumName},
        {"dur", static_cast<double>(song.duration)},
        {"fee", static_cast<double>(song.fee)},
        {"cover", song.cover},
        {"alias", song.alias},
        {"quality", song.quality},
        {"mv", static_cast<double>(song.mv)},
        {"playable", play.playable},
        {"blockedBy", play.blockedBy},
        {"reason", QString()},
    };
}

// 电台节目 → 队列曲目。id 是主歌 id（真正播放的那首），专辑/歌手位置换成电台/主播；
// 节目自己缺 radio 时用详情页的电台信息补。
QVariantMap programTrack(const sdk::RadioProgram &program, const QString &radioName,
                         const QString &djName, const QString &radioCover, qint64 radioId)
{
    const QString album = program.radio.name.isEmpty() ? radioName : program.radio.name;
    const QString host = program.radio.djName.isEmpty() ? djName : program.radio.djName;
    QString cover = program.cover;
    if (cover.isEmpty())
        cover = program.radio.cover;
    if (cover.isEmpty())
        cover = radioCover;
    const qint64 stationId = program.radio.id > 0 ? program.radio.id : radioId;
    const Playability play = playabilityForFee(program.fee);
    return {
        {"id", static_cast<double>(program.mainSongId)},
        {"name", program.name},
        {"artist", host},
        {"artistId", 0.0},
        {"artists", QVariantList{QVariantMap{{"id", 0.0}, {"name", host}}}},
        {"albumId", 0.0},
        {"album", album},
        {"dur", static_cast<double>(program.mainSongDuration)},
        {"fee", static_cast<double>(program.fee)},
        {"cover", cover},
        {"alias", QString()},
        {"quality", QString()},
        {"mv", 0.0},
        {"playable", play.playable},
        {"blockedBy", play.blockedBy},
        {"reason", QString()},
        {"programId", static_cast<double>(program.id)},
        {"radioId", static_cast<double>(stationId)},
        {"serial", static_cast<double>(program.serialNum)},
        {"createTime", static_cast<double>(program.createTime)},
        {"description", program.description},
    };
}

QVariantMap radioBase(const sdk::RadioStation &radio)
{
    return {
        {"id", static_cast<double>(radio.id)},
        {"name", radio.name},
        {"cover", radio.cover},
        {"description", radio.description},
        {"dj", QVariantMap{{"id", static_cast<double>(radio.djId)},
                           {"name", radio.djName},
                           {"avatar", radio.djAvatar}}},
        {"programCount", static_cast<double>(radio.programCount)},
        {"subCount", static_cast<double>(radio.subCount)},
        {"playCount", static_cast<double>(radio.playCount)},
    };
}

// 翻页时从旧槽里取回详情字段；节目页与 offset/more 由本次响应覆盖。
QVariantMap radioBase(const QVariantMap &slot)
{
    return {
        {"id", slot.value("id")},
        {"name", slot.value("name")},
        {"cover", slot.value("cover")},
        {"description", slot.value("description")},
        {"dj", slot.value("dj")},
        {"programCount", slot.value("programCount")},
        {"subCount", slot.value("subCount")},
        {"playCount", slot.value("playCount")},
    };
}

QVariantMap suggestionsMap(const sdk::SearchSuggestions &suggestions)
{
    QVariantList songs;
    for (const sdk::SearchSuggestSong &song : suggestions.songs) {
        songs.append(QVariantMap{{"id", static_cast<double>(song.id)},
                                 {"name", song.name},
                                 {"artist", song.artists.join(QStringLiteral(" / "))},
                                 {"album", song.albumName}});
    }
    QVariantList artists;
    for (const sdk::SearchSuggestArtist &artist : suggestions.artists) {
        artists.append(QVariantMap{{"id", static_cast<double>(artist.id)},
                                   {"name", artist.name},
                                   {"cover", artist.cover}});
    }
    QVariantList albums;
    for (const sdk::SearchSuggestAlbum &album : suggestions.albums) {
        albums.append(QVariantMap{{"id", static_cast<double>(album.id)},
                                  {"name", album.name},
                                  {"artist", album.artistName},
                                  {"artistId", static_cast<double>(album.artistId)}});
    }
    QVariantList playlists;
    for (const sdk::SearchSuggestPlaylist &playlist : suggestions.playlists) {
        playlists.append(QVariantMap{{"id", static_cast<double>(playlist.id)},
                                     {"name", playlist.name},
                                     {"cover", playlist.cover},
                                     {"count", static_cast<double>(playlist.trackCount)}});
    }
    QVariantList order;
    for (const QString &category : suggestions.order)
        order.append(category);
    return {{"query", suggestions.query},
            {"order", order},
            {"songs", songs},
            {"artists", artists},
            {"albums", albums},
            {"playlists", playlists}};
}
} // namespace

void Store::requestSearchSuggest(const QString &key, const QString &query)
{
    // 纯空白查询不是一次搜索：不发请求，也不推进请求代号
    if (query.trimmed().isEmpty())
        return;
    const quint64 token = m_requests.begin(QStringLiteral("searchSuggest"));
    m_cache.query(
        key,
        [this, query](QueryCache::Done done) {
            sdk::fetchSearchSuggestions(*m_client, query, this,
                                        [done](sdk::Result<sdk::SearchSuggestions> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                done(suggestionsMap(*result.value), true);
            });
        },
        [this, token](const QVariant &value) {
            // 用户已经输入了更新的词：旧查询晚到的结果（含缓存值）不能覆盖当前联想
            if (m_requests.current(QStringLiteral("searchSuggest"), token))
                put("searchSuggest", value, false);
        });
}

void Store::requestNewSongs()
{
    const quint64 token = m_requests.begin(QStringLiteral("newSongs"));
    m_cache.query(
        QStringLiteral("discover:newSongs"),
        [this](QueryCache::Done done) {
            sdk::fetchNewSongs(*m_client, kNewSongLimit, this,
                               [done](sdk::Result<QList<sdk::NewSong>> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                QVariantList tracks;
                tracks.reserve(result.value->size());
                for (const sdk::NewSong &song : *result.value)
                    tracks.append(newSongTrack(song));
                done(tracks, true);
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("newSongs"), token))
                put("newSongs", value, false);
        });
}

void Store::requestNewAlbums()
{
    const quint64 token = m_requests.begin(QStringLiteral("newAlbums"));
    m_cache.query(
        QStringLiteral("discover:newAlbums"),
        [this](QueryCache::Done done) {
            sdk::fetchNewAlbums(*m_client, kNewAlbumLimit, 0, QString(), this,
                                [done](sdk::Result<QList<sdk::NewAlbum>> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                QVariantList albums;
                albums.reserve(result.value->size());
                for (const sdk::NewAlbum &album : *result.value) {
                    albums.append(QVariantMap{
                        {"id", static_cast<double>(album.id)},
                        {"name", album.name},
                        {"cover", album.cover},
                        {"artist", album.artistName},
                        {"artistId", static_cast<double>(album.artistId)},
                        {"publishTime", static_cast<double>(album.publishTime)},
                        {"size", static_cast<double>(album.size)},
                    });
                }
                done(albums, true);
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("newAlbums"), token))
                put("newAlbums", value, false);
        });
}

void Store::requestHighQualityTags()
{
    const quint64 token = m_requests.begin(QStringLiteral("highqualityTags"));
    m_cache.query(
        QStringLiteral("discover:highqualityTags"),
        [this](QueryCache::Done done) {
            sdk::fetchHighQualityTags(*m_client, this,
                                      [done](sdk::Result<QList<sdk::HighQualityTag>> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                QVariantList tags;
                tags.reserve(result.value->size());
                for (const sdk::HighQualityTag &tag : *result.value)
                    tags.append(QVariantMap{{"name", tag.name}, {"hot", tag.hot}});
                done(tags, true);
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("highqualityTags"), token))
                put("highqualityTags", value, false);
        });
}

void Store::requestHighQuality(const QString &cat, qint64 before)
{
    // 空分类就是「全部」；游标 0/负数都是第一页
    const QString category = cat.trimmed().isEmpty() ? QStringLiteral("全部") : cat;
    const qint64 cursor = std::max<qint64>(0, before);
    const quint64 token = cursor > 0 ? m_requests.token(QStringLiteral("highquality"))
                                     : m_requests.begin(QStringLiteral("highquality"));
    QString key = QStringLiteral("discover:highquality:%1").arg(category);
    if (cursor > 0)
        key += QStringLiteral(":%1").arg(cursor);

    m_cache.query(
        key,
        [this, category, cursor](QueryCache::Done done) {
            sdk::fetchHighQualityPlaylists(*m_client, category, kHighQualityLimit, cursor, this,
                                           [done, category, cursor]
                                           (sdk::Result<sdk::HighQualityPlaylistPage> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                QVariantList items;
                items.reserve(result.value->items.size());
                for (const sdk::HighQualityPlaylist &playlist : result.value->items) {
                    items.append(QVariantMap{
                        {"id", static_cast<double>(playlist.id)},
                        {"name", playlist.name},
                        {"cover", playlist.cover},
                        {"count", static_cast<double>(playlist.trackCount)},
                        {"plays", static_cast<double>(playlist.playCount)},
                        {"description", playlist.description},
                        {"creator", playlist.creatorName},
                    });
                }
                done(QVariantMap{{"cat", category},
                                 {"before", static_cast<double>(cursor)},
                                 {"nextBefore", static_cast<double>(result.value->lasttime)},
                                 {"more", result.value->more},
                                 {"items", items}},
                     true);
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("highquality"), token))
                put("highquality", value, false);
        });
}

void Store::requestRecommendedMvs()
{
    const quint64 token = m_requests.begin(QStringLiteral("recommendedMvs"));
    m_cache.query(
        QStringLiteral("discover:mvs"),
        [this](QueryCache::Done done) {
            sdk::fetchPersonalizedMvs(*m_client, this,
                                      [done](sdk::Result<QList<sdk::PersonalizedMv>> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                QVariantList mvs;
                mvs.reserve(result.value->size());
                for (const sdk::PersonalizedMv &mv : *result.value) {
                    mvs.append(QVariantMap{
                        {"id", static_cast<double>(mv.id)},
                        {"name", mv.name},
                        {"cover", mv.cover},
                        {"artist", mv.artistName},
                        {"artistId", static_cast<double>(mv.artistId)},
                        {"duration", static_cast<double>(mv.duration)},
                        {"playCount", static_cast<double>(mv.playCount)},
                        {"reason", mv.copywriter},
                    });
                }
                done(mvs, true);
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("recommendedMvs"), token))
                put("recommendedMvs", value, false);
        });
}

void Store::requestRecommendedPrograms()
{
    const quint64 token = m_requests.begin(QStringLiteral("recommendedPrograms"));
    m_cache.query(
        QStringLiteral("discover:programs"),
        [this](QueryCache::Done done) {
            sdk::fetchRecommendedPrograms(*m_client, this,
                                          [done](sdk::Result<QList<sdk::RadioProgram>> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                QVariantList programs;
                for (const sdk::RadioProgram &program : *result.value) {
                    if (program.mainSongId <= 0)
                        continue;
                    programs.append(programTrack(program, {}, {}, {}, 0));
                }
                done(programs, true);
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("recommendedPrograms"), token))
                put("recommendedPrograms", value, false);
        });
}

void Store::requestRadio(double radioId, int offset)
{
    if (!(radioId > 0))
        return;
    const int start = std::max(0, offset);
    const quint64 token = start > 0 ? m_requests.token(QStringLiteral("radio"))
                                    : m_requests.begin(QStringLiteral("radio"));
    const QString key = start > 0
        ? QStringLiteral("radio:%1:%2").arg(qlonglong(radioId)).arg(start)
        : QStringLiteral("radio:%1").arg(qlonglong(radioId));

    m_cache.query(
        key,
        [this, radioId, start](QueryCache::Done done) {
            struct State {
                QVariantMap detail;
                QList<sdk::RadioProgram> programs;
                bool detailReady = false;
                bool programsReady = false;
                bool more = false;
                bool failed = false;
            };
            auto state = std::make_shared<State>();
            const auto fail = [state, done] {
                if (state->failed)
                    return;
                state->failed = true;
                done({}, false);
            };
            const auto finish = [state, done, radioId, start] {
                if (state->failed || !state->detailReady || !state->programsReady)
                    return;
                const QVariantMap dj = state->detail.value("dj").toMap();
                QVariantList programs;
                for (const sdk::RadioProgram &program : state->programs) {
                    if (program.mainSongId <= 0)
                        continue;
                    programs.append(programTrack(program,
                                                 state->detail.value("name").toString(),
                                                 dj.value("name").toString(),
                                                 state->detail.value("cover").toString(),
                                                 static_cast<qint64>(radioId)));
                }
                QVariantMap page = state->detail;
                page.insert("offset", start);
                page.insert("more", state->more);
                page.insert("programs", programs);
                done(page, true);
            };

            // 翻页时详情已经在这个槽里（同一 id）：只补节目页，不再请求详情
            const QVariantMap shown = m_data.value(QStringLiteral("radio")).toMap();
            if (start > 0 && shown.value("id").toDouble() == radioId) {
                state->detail = radioBase(shown);
                state->detailReady = true;
            } else {
                sdk::fetchRadio(*m_client, static_cast<qint64>(radioId), this,
                                [state, fail, finish](sdk::Result<sdk::RadioStation> result) {
                    if (!result) {
                        fail();
                        return;
                    }
                    state->detail = radioBase(*result.value);
                    state->detailReady = true;
                    finish();
                });
            }

            sdk::fetchRadioPrograms(*m_client, static_cast<qint64>(radioId), kRadioProgramLimit,
                                    start, false, this,
                                    [state, fail, finish](sdk::Result<sdk::RadioProgramPage> result) {
                if (!result) {
                    fail();
                    return;
                }
                state->programs = result.value->items;
                state->more = result.value->more;
                state->programsReady = true;
                finish();
            });
        },
        [this, token](const QVariant &value) {
            if (m_requests.current(QStringLiteral("radio"), token))
                put("radio", value, false);
        });
}
