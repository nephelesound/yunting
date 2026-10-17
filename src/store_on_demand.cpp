// Store 的按需加载单元：request(key) 的三个 key（lyrics / playlist / search）与相似歌曲种子。
// 取数一律经 QueryCache（stale-while-revalidate + 在途去重 + 落盘）：同一首歌的歌词、同一个歌单、
// 同一个搜索词，只在过期后才重新请求。首屏管线在 store.cpp；形状仍以 scripts/snapshot.py 为准。
#include "store.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QUrl>
#include <QUrlQuery>
#include <memory>

using namespace ncm;

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

void Store::request(const QString &key)
{
    const QString arg = key.section(u':', 1);
    if (key.startsWith(QLatin1String("lyrics:")))
        requestLyrics(key, arg.toDouble());
    else if (key.startsWith(QLatin1String("playlist:")))
        requestPlaylist(key, arg.toDouble());
    else if (key.startsWith(QLatin1String("search:")))
        requestSearch(key, arg);
    else
        qWarning() << "unknown store key:" << key.section(u':', 0, 0);
}

// 歌词：单槽（lyricsTrackId + lyrics），QML 只看当前这首；缓存按歌分开，切回来直接命中
void Store::requestLyrics(const QString &key, double songId)
{
    m_cache.query(key,
        [this, songId](QueryCache::Done done) {
            m_client->get("api/song/lyric", query({{"id", num(songId)}, {"lv", "1"}, {"tv", "-1"}}), false,
                          [done](const QJsonDocument &doc, bool ok) {
                const QString lrc = body(doc).value("lrc").toObject().value("lyric").toString();
                done(ncmParseLrc(lrc), ok);
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
            m_client->get("api/v6/playlist/detail", query({{"id", num(playlistId)}, {"n", "30"}}), true,
                          [done](const QJsonDocument &doc, bool ok) {
                const QJsonObject pl = body(doc).value("playlist").toObject();
                done(QVariantMap{{"tracks", ncmTracks(take(arr(pl, "tracks"), 30))},
                                 {"description", ncmFirstLine(pl.value("description").toString())}},
                     ok && !pl.isEmpty());
            });
        },
        [this, playlistId](const QVariant &detail) {
            QVariantMap details = m_personal.value("playlistDetails").toMap();
            details.insert(num(playlistId), detail);
            put("playlistDetails", details, true);
        });
}

// 搜索：四类结果并发取，全部回来后作为一个值交给缓存；页面先拿到同词的旧结果（若有）
void Store::requestSearch(const QString &key, const QString &raw)
{
    m_cache.query(key,
        [this, raw](QueryCache::Done done) {
            const QString s = QString::fromUtf8(QUrl::toPercentEncoding(raw));
            struct Pending {
                QVariantMap result;
                int left = 4;
                bool ok = true;
            };
            auto pending = std::make_shared<Pending>();
            pending->result = QVariantMap{{"query", raw}};
            const auto part = [this, s, pending, done](const char *type, const char *limit, const char *field,
                                                       std::function<QVariant(const QJsonObject &)> pick) {
                m_client->get("api/search/get", query({{"s", s}, {"type", type}, {"limit", limit}}), false,
                              [pending, done, field, pick](const QJsonDocument &doc, bool ok) {
                    pending->result.insert(QLatin1String(field), pick(body(doc).value("result").toObject()));
                    pending->ok = pending->ok && ok;
                    if (--pending->left == 0)
                        done(pending->result, pending->ok);
                });
            };
            const auto list = [](const QJsonObject &r, const char *k, QVariantMap (*fn)(const QJsonObject &)) {
                QVariantList out;
                for (const QJsonValue &v : arr(r, k))
                    out.append(fn(v.toObject()));
                return QVariant(out);
            };
            part("1", "10", "songs", [](const QJsonObject &r) { return QVariant(ncmTracks(arr(r, "songs"))); });
            part("100", "6", "artists", [list](const QJsonObject &r) { return list(r, "artists", ncmSearchArtist); });
            part("1000", "6", "playlists", [list](const QJsonObject &r) { return list(r, "playlists", ncmSearchPlaylist); });
            part("10", "6", "albums", [list](const QJsonObject &r) { return list(r, "albums", ncmSearchAlbum); });
        },
        [this](const QVariant &result) { put("search", result, false); });
}
