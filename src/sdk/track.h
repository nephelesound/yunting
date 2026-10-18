#pragma once
// Track / PlaylistCard — the one typed boundary for song and playlist-card JSON.
//
// Raw shapes read only here:
//   track:         ar[]/artists[] (id, name), al/album (id, name, picUrl),
//                  dt/duration (milliseconds), fee, alia/alias/tns/transNames,
//                  hr/hrMusic/sq/sqMusic, mv/mvid, privilege{st,cs,pl,fee,payed}
//   playlist card: id, name, picUrl or coverImgUrl, trackCount, description,
//                  playCount, creator{userId,nickname}, updateTime, subscribed,
//                  copywriter
//
// Parsing uses JsonReader and therefore rejects a present field of the wrong
// type with its JSON path; missing fields take the documented default. Ids are
// not required to be positive: list rows keep working even when the API returns
// an incomplete song (the legacy normalizer behaved the same way).
//
// Raw URLs stay raw in the structs. Only the legacy QML adapters at the bottom
// add the ?param= size suffix; new frontend code should use the structs.
#include "sdk/sdk_result.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace ncm::sdk {

struct TrackArtist {
    qint64 id = 0;   // ar[].id / artists[].id
    QString name;    // ar[].name / artists[].name
};

// The account-relative facts the API returns for this song. The parser takes an
// explicit parallel privileges[] row when one matched; otherwise it reads the
// embedded song.privilege. `present` stays false when neither exists, and then
// playability falls back to Track::fee.
struct TrackPrivilege {
    bool present = false; // a non-empty privilege object was supplied
    bool hasFee = false;  // privilege.fee exists (otherwise Track::fee applies)
    double fee = 0;       // privilege.fee
    double st = 0;        // privilege.st (negative = removed / 下架)
    bool cloud = false;   // truthy privilege.cs (cloud upload)
    double pl = 0;        // privilege.pl (>0 = playable for this account)
    bool payed = false;   // truthy privilege.payed (fee=4 album purchased)
};

struct Track {
    qint64 id = 0;              // id
    QString name;               // name
    QList<TrackArtist> artists; // ar[] / artists[], order preserved
    qint64 albumId = 0;         // al.id / album.id
    QString albumName;          // al.name / album.name
    QString cover;              // al.picUrl / album.picUrl (raw URL)
    qint64 duration = 0;        // dt / duration milliseconds -> seconds
    double fee = 0;             // fee
    QString alias;              // first item of alia / alias / tns / transNames
    QString quality;            // "hires" (hr|hrMusic) / "lossless" (sq|sqMusic) / ""
    qint64 mv = 0;              // mv, fallback mvid
    TrackPrivilege privilege;   // embedded or matched parallel privileges[] row
};

struct TrackPlayability {
    bool playable = true;
    QString blockedBy;          // "" / "vip" / "paid" / "unavailable"
};

// `privilege` is the parallel privileges[] row for this song; pass an empty
// object to use the embedded song.privilege. Song ids are not validated.
Result<Track> parseTrack(const QJsonObject &song,
                         const QJsonObject &privilege = QJsonObject());
// Matches privileges[] to songs[] by id. A song without a row uses its embedded
// privilege; without either one playability falls back to fee. A song that fails
// to parse is skipped (with a warning naming its JSON path); the list never fails
// because of one row. parsePlaylistCards does the same for cards.
Result<QList<Track>> parseTracks(const QJsonArray &songs,
                                 const QJsonArray &privileges = QJsonArray());

// The single playability rule. Privilege decides; fee is only consulted when
// the API sent no privilege at all. st<0 -> unavailable, cs/pl>0 -> playable,
// fee 1 -> vip, fee 4 without payed -> paid, everything else -> unavailable.
TrackPlayability playability(const Track &track);

struct PlaylistCard {
    qint64 id = 0;           // id
    QString name;            // name
    QString cover;           // picUrl (推荐) or coverImgUrl (账号/用户歌单); raw URL
    qint64 count = 0;        // trackCount
    QString description;     // description, first line only
    qint64 playCount = 0;    // playCount
    qint64 creatorId = 0;    // creator.userId
    QString creatorName;     // creator.nickname
    qint64 updateTime = 0;   // updateTime (milliseconds)
    bool subscribed = false; // subscribed
    QString reason;          // copywriter (推荐语)
};

// coverKey names the endpoint's cover field: "picUrl" for personalized/推荐
// cards, "coverImgUrl" for account and user-page cards.
Result<PlaylistCard> parsePlaylistCard(const QJsonObject &playlist, const char *coverKey);
Result<QList<PlaylistCard>> parsePlaylistCards(const QJsonArray &playlists,
                                               const char *coverKey);

// Legacy Store adapters. Keys and value types deliberately match the former
// normalize.cpp maps; update the golden fixture and docs/API-FRONTEND.md together
// if a field changes. coverPx=0 keeps the raw URL (the discovery/Program shape).
QVariantMap trackToVariantMap(const Track &track, const QString &reason = QString(),
                              int coverPx = 120);
QVariantList tracksToVariantList(const QList<Track> &tracks);
QVariantMap playlistCardToVariantMap(const PlaylistCard &card);

// Image-size suffix; one implementation for the SDK adapters and ncmSized.
QString sizedUrl(const QString &url, int px);

} // namespace ncm::sdk
