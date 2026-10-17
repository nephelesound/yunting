// Store 的按需加载单元：request(key) 的三个 key（lyrics / playlist / search）与
// 相似歌曲种子（匿名热歌第一首 / 登录后红心第一首，谁先到谁做种子）。
// 首屏管线在 store.cpp；形状仍以 scripts/snapshot.py 为准。
#include "store.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QUrl>
#include <QUrlQuery>

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

void Store::fetchLyrics(double songId)
{
    fetch("api/song/lyric", query({{"id", num(songId)}, {"lv", "1"}, {"tv", "-1"}}), false,
          [this, songId](const QJsonDocument &doc, bool /*ok*/) {
        const QString lrc = body(doc).value("lrc").toObject().value("lyric").toString();
        put("lyricsTrackId", songId, false);
        put("lyrics", ncmParseLrc(lrc), false);
    }, QStringLiteral("lyrics"));
}

void Store::fetchPlaylistDetail(double playlistId)
{
    fetch("api/v6/playlist/detail", query({{"id", num(playlistId)}, {"n", "30"}}), false,
          [this, playlistId](const QJsonDocument &doc, bool /*ok*/) {
        const QJsonObject pl = body(doc).value("playlist").toObject();
        QVariantMap detail{{"tracks", ncmTracks(take(arr(pl, "tracks"), 30))},
                           {"description", ncmFirstLine(pl.value("description").toString())}};
        QVariantMap details = m_personal.value("playlistDetails").toMap();
        details.insert(num(playlistId), detail);
        put("playlistDetails", details, true);
    }, QStringLiteral("playlist"));
}

void Store::fetchSearch(const QString &raw, const QString &sharedKey)
{
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(raw));
    // 先给完整骨架：四类结果任何一类晚到都不会让 QML 读到 undefined
    put("search", QVariantMap{{"query", raw}, {"songs", QVariantList{}}, {"artists", QVariantList{}},
                              {"playlists", QVariantList{}}, {"albums", QVariantList{}}}, false);

    fetch("api/search/get", query({{"s", encoded}, {"type", "1"}, {"limit", "10"}}), false,
          [this, raw](const QJsonDocument &doc, bool ok) {
        if (ok) {
            QVariantMap search = m_data.value("search").toMap();
            search.insert("query", raw);
            search.insert("songs", ncmTracks(arr(body(doc).value("result").toObject(), "songs")));
            put("search", search, false);
        }
    }, sharedKey);
    fetch("api/search/get", query({{"s", encoded}, {"type", "100"}, {"limit", "6"}}), false,
          [this](const QJsonDocument &doc, bool ok) {
        QVariantList artists;
        if (ok)
            for (const QJsonValue &a : arr(body(doc).value("result").toObject(), "artists"))
                artists.append(ncmSearchArtist(a.toObject()));
        QVariantMap search = m_data.value("search").toMap();
        search.insert("artists", artists);
        put("search", search, false);
    }, sharedKey);
    fetch("api/search/get", query({{"s", encoded}, {"type", "1000"}, {"limit", "6"}}), false,
          [this](const QJsonDocument &doc, bool ok) {
        QVariantList playlists;
        if (ok)
            for (const QJsonValue &p : arr(body(doc).value("result").toObject(), "playlists"))
                playlists.append(ncmSearchPlaylist(p.toObject()));
        QVariantMap search = m_data.value("search").toMap();
        search.insert("playlists", playlists);
        put("search", search, false);
    }, sharedKey);
    fetch("api/search/get", query({{"s", encoded}, {"type", "10"}, {"limit", "6"}}), false,
          [this](const QJsonDocument &doc, bool ok) {
        QVariantList albums;
        if (ok)
            for (const QJsonValue &a : arr(body(doc).value("result").toObject(), "albums"))
                albums.append(ncmSearchAlbum(a.toObject()));
        QVariantMap search = m_data.value("search").toMap();
        search.insert("albums", albums);
        put("search", search, false);
    }, sharedKey);
}

void Store::request(const QString &key)
{
    if (m_inFlight.contains(key))
        return; // 同 key 在途去重（search 一次发四条，引用计数在 fetchSearch 落地时递减）
    if (key.startsWith(QLatin1String("lyrics:"))) {
        m_inFlight.insert(key, 1);
        fetchLyrics(key.mid(7).toDouble());
    } else if (key.startsWith(QLatin1String("playlist:"))) {
        m_inFlight.insert(key, 1);
        fetchPlaylistDetail(key.mid(9).toDouble());
    } else if (key.startsWith(QLatin1String("search:"))) {
        m_inFlight.insert(key, 4);
        fetchSearch(key.mid(7), key);
    } else {
        qWarning() << "unknown store key:" << key.section(u':', 0, 0);
    }
}
