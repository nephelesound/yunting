#pragma once
// Listen footprint SDK — typed read models for the listening statistics page.
//
// Raw endpoints (GET, personal MUSIC_U attached by the Store):
//   api/content/activity/listen/data/total                  data.totalDuration
//   api/content/activity/listen/data/realtime/report?type=  data.startTime/endTime/...
//   api/content/activity/listen/data/song/play/rank?type=   data.songItems[]
//
// A missing, null or empty `data` object is a Parse error: the endpoint has no
// statistics to show. Missing fields inside a present `data` keep their defaults.
//
// The three realtime UI blocks (`listenTimeDistributionBlock`,
// `weekFriendsListenBlock`, `weekTodayListenBlock`) are kept verbatim: their
// nested shape is rendered by the frontend and the client does not interpret it.
// Cover URLs stay raw; sizing is a frontend decision.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace ncm::sdk {

// data of /api/content/activity/listen/data/total.
struct ListenTotal {
    qint64 seconds = 0; // data.totalDuration (already seconds)
};

// One artist row of data.songItems[].artists[].
struct ListenRankArtist {
    qint64 id = 0;  // artistId
    QString name;   // artistName
};

// One row of data.songItems[].
struct ListenRankSong {
    qint64 id = 0;        // songId
    QString name;         // songName
    QString album;        // albumName
    QString cover;        // picUrl (raw URL)
    qint64 playCount = 0; // playCount
    QList<ListenRankArtist> artists; // artists[]
};

// data of /api/content/activity/listen/data/realtime/report.
struct ListenRealtime {
    qint64 startTime = 0; // data.startTime (epoch milliseconds)
    qint64 endTime = 0;   // data.endTime
    QString userNickName; // data.userNickName
    QJsonObject listenTimeDistribution; // data.listenTimeDistributionBlock
    QJsonObject weekFriends;            // data.weekFriendsListenBlock
    QJsonObject weekToday;              // data.weekTodayListenBlock
};

// data of /api/content/activity/listen/data/song/play/rank.
struct ListenRank {
    qint64 startTime = 0; // data.startTime (epoch milliseconds)
    qint64 endTime = 0;   // data.endTime
    qint64 songCount = 0; // data.songCount
    QList<ListenRankSong> items; // data.songItems[], in server order
};

// Pure parsers, exposed for tests and callers that already hold a response.
Result<ListenTotal> parseListenTotal(const QJsonObject &root);
Result<ListenRealtime> parseListenRealtime(const QJsonObject &root);
Result<ListenRank> parseListenRank(const QJsonObject &root);

} // namespace ncm::sdk
