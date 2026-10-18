#pragma once
// Library SDK — typed models for the signed-in user's subscriptions.
//
// Raw endpoints (all personal weapi POST; the stored MUSIC_U cookie is required):
//   POST /weapi/artist/sublist              subscribed artists
//   POST /weapi/album/sublist               subscribed albums
//   POST /weapi/cloudvideo/allvideo/sublist subscribed MVs / cloud videos
// Error::endpoint carries the weapi-relative path (for example "artist/sublist");
// the shared transport adds the /weapi/ prefix.
//
// Request contract: `limit` is clamped to at least 1 and `offset` to at least 0
// (the web defaults are 25 / 0). The service also sends `total: true` so the
// server includes the total subscription count. Responses do not echo `offset`;
// fetch fills the page's `offset` with the value that was requested.
//
// Response envelope for all three endpoints:
//   data[]     this page's items (the only accepted list field)
//   count      total number of subscriptions; 0 when the server omits it
//   hasMore    whether another page exists; false when omitted
// An absent `data` key is a Parse error. An explicitly empty `data` array (or a
// null `data` value) is a valid empty page (count/hasMore still parsed).
// Unknown fields are ignored.
//
// Field comments name the raw JSON path. Timestamps are milliseconds and
// durations are seconds. URLs are raw; the frontend owns image sizing.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class NcmClient;

namespace ncm::sdk {

// data[] of /weapi/artist/sublist
struct SubscribedArtist {
    qint64 id = 0;          // data[].id
    QString name;           // data[].name
    QString cover;          // data[].picUrl, fallback data[].img1v1Url
    QString trans;          // data[].trans (translated name, may be empty)
    QStringList alias;      // data[].alias[]
    qint64 albumSize = 0;   // data[].albumSize
    qint64 mvSize = 0;      // data[].mvSize
    QString latestInfo;     // data[].info (upstream new-release notice, may be empty)
};

// data[] of /weapi/album/sublist
struct SubscribedAlbum {
    qint64 id = 0;          // data[].id
    QString name;           // data[].name
    QString cover;          // data[].picUrl
    qint64 artistId = 0;    // data[].artists[0].id
    QString artistName;     // data[].artists[0].name
    QStringList artists;    // data[].artists[].name
    qint64 size = 0;        // data[].size (track count)
    qint64 subTime = 0;     // data[].subTime (milliseconds)
    QStringList alias;      // data[].alias[]
};

// data[] of /weapi/cloudvideo/allvideo/sublist
struct SubscribedMv {
    QString id;             // data[].vid (string; decimal or 32-char hex cloud id)
    int type = 0;           // data[].type (0 = MV, 1 = video)
    QString name;           // data[].title
    QString cover;          // data[].coverUrl
    qint64 duration = 0;    // data[].durationms / 1000 (seconds)
    qint64 playCount = 0;   // data[].playTime
    qint64 creatorId = 0;   // data[].creator[0].userId
    QString creatorName;    // data[].creator[0].userName
};

struct SubscribedArtistPage {
    QList<SubscribedArtist> items; // data[]
    qint64 count = 0;              // count (total subscriptions; 0 when omitted)
    bool hasMore = false;          // hasMore
    int offset = 0;                // requested offset, filled by the fetch service
};

struct SubscribedAlbumPage {
    QList<SubscribedAlbum> items; // data[]
    qint64 count = 0;             // count (total subscriptions; 0 when omitted)
    qint64 paidCount = 0;         // paidCount
    bool hasMore = false;         // hasMore
    int offset = 0;               // requested offset, filled by the fetch service
};

struct SubscribedMvPage {
    QList<SubscribedMv> items; // data[]
    qint64 count = 0;          // count (total subscriptions; 0 when omitted)
    bool hasMore = false;      // hasMore
    int offset = 0;            // requested offset, filled by the fetch service
};

using SubscribedArtistsCallback = std::function<void(Result<SubscribedArtistPage>)>;
using SubscribedAlbumsCallback = std::function<void(Result<SubscribedAlbumPage>)>;
using SubscribedMvsCallback = std::function<void(Result<SubscribedMvPage>)>;

// All three calls attach the stored login cookie (personal=true). Without a
// signed-in session the server answers business code 301, surfaced as an
// ErrorKind::Api failure with code 301.
void fetchSubscribedArtists(NcmClient &client, int limit, int offset,
                            QObject *context, SubscribedArtistsCallback callback);
void fetchSubscribedAlbums(NcmClient &client, int limit, int offset,
                           QObject *context, SubscribedAlbumsCallback callback);
void fetchSubscribedMvs(NcmClient &client, int limit, int offset,
                        QObject *context, SubscribedMvsCallback callback);

// Pure parsers, exposed for tests and callers that already hold a response.
Result<SubscribedArtistPage> parseSubscribedArtists(const QJsonObject &root);
Result<SubscribedAlbumPage> parseSubscribedAlbums(const QJsonObject &root);
Result<SubscribedMvPage> parseSubscribedMvs(const QJsonObject &root);

} // namespace ncm::sdk
