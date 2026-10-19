#pragma once
// Discovery SDK — typed models for the "find new music" surfaces.
//
// Raw endpoints (all read-only, plaintext /api in this client):
//   POST /api/personalized/newsong          new/recommended songs
//   POST /api/album/new                     new albums
//   POST /api/playlist/highquality/list     high-quality playlists
//   POST /api/playlist/highquality/tags     high-quality playlist tags
//
// Every field comment names its raw JSON path. Timestamps are milliseconds;
// durations are seconds. URLs are raw; frontend owns image sizing.
#include "sdk/sdk_result.h"
#include "sdk/track.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct NewSongArtist {
    qint64 id = 0;          // result[].song.artists[].id
    QString name;           // result[].song.artists[].name
};

struct NewSong {
    qint64 songId = 0;      // result[].song.id
    QString name;           // result[].song.name
    QList<NewSongArtist> artists; // result[].song.artists[]（保留 id，队列里每位歌手要可点）
    qint64 albumId = 0;     // result[].song.album.id
    QString albumName;      // result[].song.album.name
    QString cover;          // result[].song.album.picUrl (fallback result[].picUrl)
    qint64 duration = 0;    // result[].song.duration / 1000
    int fee = 0;            // result[].song.fee
    QString alias;          // song.alia[0] / alias / tns / transNames
    QString quality;        // "hires" (hr|hrMusic) / "lossless" (sq|sqMusic) / ""
    qint64 mv = 0;          // song.mv (fallback song.mvid)
};

// NewSong → the shared queue track. Cover stays raw (the discovery map keeps the
// original URL); playability falls back to fee because this endpoint sends no privilege.
Track trackFromNewSong(const NewSong &song);

struct NewAlbum {
    qint64 id = 0;
    QString name;
    QString cover;          // picUrl
    qint64 publishTime = 0; // milliseconds
    qint64 artistId = 0;    // artist.id
    QString artistName;     // artist.name
    QStringList artists;    // artists[].name
    qint64 size = 0;        // track count
};

struct HighQualityPlaylist {
    qint64 id = 0;
    QString name;
    QString cover;          // coverImgUrl
    QString description;
    qint64 trackCount = 0;
    qint64 playCount = 0;
    qint64 subscribedCount = 0;
    qint64 creatorId = 0;   // creator.userId
    QString creatorName;    // creator.nickname
    qint64 updateTime = 0;  // milliseconds
};

struct Banner {
    QString typeTitle;     // banners[].typeTitle
    QString imageUrl;      // banners[].imageUrl
    QString url;           // banners[].url (https or orpheus://)
    int targetType = 0;    // banners[].targetType
    qint64 targetId = 0;   // banners[].targetId
};

struct HighQualityTag {
    qint64 id = 0;
    QString name;
    int type = 0;
    int category = 0;
    bool hot = false;
};

struct PlaylistCatalogueTag {
    QString name;           // sub[].name
    int category = 0;       // sub[].category
    qint64 count = 0;       // sub[].resourceCount
    bool hot = false;       // sub[].hot
};

struct PlaylistCatalogue {
    QString allName;        // all.name
    QList<PlaylistCatalogueTag> tags; // sub[]
};

struct HighQualityPlaylistPage {
    QList<HighQualityPlaylist> items;
    bool more = false;      // top-level more
    qint64 lasttime = 0;    // top-level lasttime, for the next page
};

using NewSongsCallback = std::function<void(Result<QList<NewSong>>)>;
using NewAlbumsCallback = std::function<void(Result<QList<NewAlbum>>)>;
using HighQualityPlaylistsCallback = std::function<void(Result<HighQualityPlaylistPage>)>;
using HighQualityTagsCallback = std::function<void(Result<QList<HighQualityTag>>)>;
using BannersCallback = std::function<void(Result<QList<Banner>>)>;
using PlaylistCatalogueCallback = std::function<void(Result<PlaylistCatalogue>)>;

void fetchNewSongs(NcmClient &client, int limit, QObject *context,
                   NewSongsCallback callback);
void fetchNewAlbums(NcmClient &client, int limit, int offset, QString area,
                    QObject *context, NewAlbumsCallback callback);
void fetchHighQualityPlaylists(NcmClient &client, QString cat, int limit, qint64 before,
                               QObject *context, HighQualityPlaylistsCallback callback);
void fetchHighQualityTags(NcmClient &client, QObject *context,
                          HighQualityTagsCallback callback);
void fetchBanners(NcmClient &client, QObject *context, BannersCallback callback);
void fetchPlaylistCatalogue(NcmClient &client, QObject *context,
                            PlaylistCatalogueCallback callback);

// Pure parsers, exposed for tests and for callers that already hold a response.
Result<QList<NewSong>> parseNewSongs(const QJsonObject &root);
Result<QList<NewAlbum>> parseNewAlbums(const QJsonObject &root);
Result<HighQualityPlaylistPage> parseHighQualityPlaylists(const QJsonObject &root);
Result<QList<HighQualityTag>> parseHighQualityTags(const QJsonObject &root);
Result<QList<Banner>> parseBanners(const QJsonObject &root);
Result<PlaylistCatalogue> parsePlaylistCatalogue(const QJsonObject &root);

} // namespace ncm::sdk
