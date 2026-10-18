#pragma once
// Video SDK — typed read models for the NetEase cloud-video surfaces.
//
// Raw endpoints (POST, weapi transport):
//   /api/videotimeline/get                    recommended video timeline
//   /api/cloudvideo/v1/video/detail           one video's detail
//   /api/cloudvideo/playurl                   signed playable URL(s)
//
// Error::endpoint carries the weapi-relative path (for example
// "cloudvideo/v1/video/detail"); the shared transport adds the /weapi/ prefix.
// The recommended timeline requires a signed-in client; detail and playurl also
// work anonymously for public videos.
//
// Field comments name the raw JSON path. Durations are normalized to whole
// seconds (`durationms` / 1000, floored); publishTime stays in milliseconds.
// Video/image URLs are raw, including signed query strings — the frontend owns
// any rewriting. Playback URLs are short-lived; `validityTime` is the server's
// validity window in seconds.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

// data.resolutions[] (timeline and detail)
struct VideoResolution {
    int resolution = 0; // resolutions[].resolution: vertical pixels (240/480/720/1080)
    qint64 size = 0;    // resolutions[].size: bytes
};

// data.creator (timeline and detail)
struct VideoCreator {
    qint64 userId = 0;   // creator.userId
    QString nickname;    // creator.nickname
    QString avatarUrl;   // creator.avatarUrl (raw)
    QString signature;   // creator.signature
};

// data.videoGroup[]
struct VideoGroup {
    qint64 id = 0;  // videoGroup[].id
    QString name;   // videoGroup[].name
};

// One signed media URL: data.urlInfo in the timeline, or urls[] of playurl.
struct VideoPlayUrl {
    QString vid;             // urls[].id / data.urlInfo.id
    QString url;             // urls[].url / data.urlInfo.url (raw; empty = unavailable)
    qint64 size = 0;         // urls[].size / data.urlInfo.size: bytes
    qint64 validityTime = 0; // urls[].validityTime / data.urlInfo.validityTime: seconds
    bool needPay = false;    // urls[].needPay / data.urlInfo.needPay
    int resolution = 0;      // urls[].r / data.urlInfo.r: vertical pixels
};

// A video card from datas[].data (timeline) or the data object (detail).
struct Video {
    QString vid;                 // data.vid: opaque hexadecimal id, never numeric
    int type = 0;                // datas[].type (timeline only; detail leaves 0)
    QString title;               // data.title
    QString description;         // data.description
    QString cover;               // data.coverUrl (raw)
    QString threadId;            // data.threadId: comment thread id (R_VI_62_<vid>)
    qint64 duration = 0;         // data.durationms / 1000 (seconds)
    qint64 previewDuration = 0;  // data.previewDurationms / 1000 (seconds)
    qint64 publishTime = 0;      // data.publishTime (milliseconds; detail only)
    qint64 playCount = 0;        // data.playTime: number of plays
    qint64 praisedCount = 0;     // data.praisedCount
    qint64 commentCount = 0;     // data.commentCount
    qint64 shareCount = 0;       // data.shareCount
    qint64 subscribeCount = 0;   // data.subscribeCount (detail only)
    bool praised = false;        // data.praised (timeline)
    bool subscribed = false;     // data.subscribed (timeline)
    bool hasRelatedGameAd = false; // data.hasRelatedGameAd
    int authType = 0;            // data.authType (detail only)
    int width = 0;               // data.width: pixels
    int height = 0;              // data.height: pixels
    QString previewUrl;          // data.previewUrl (raw; may be empty)
    VideoCreator creator;        // data.creator
    QList<VideoGroup> groups;    // data.videoGroup[]
    QList<VideoResolution> resolutions; // data.resolutions[]
    VideoPlayUrl urlInfo;        // data.urlInfo: inline signed URL (timeline; empty url when absent)
};

// /api/videotimeline/get
struct VideoTimelinePage {
    QList<Video> items; // datas[].data
    bool more = false;  // hasmore
    int offset = 0;     // request offset; the response does not echo it
};

using VideoTimelineCallback = std::function<void(Result<VideoTimelinePage>)>;
using VideoDetailCallback = std::function<void(Result<Video>)>;
using VideoPlayUrlsCallback = std::function<void(Result<QList<VideoPlayUrl>>)>;

// Recommended timeline. `offset` is clamped to >= 0; the endpoint returns one
// page and signals continuation through VideoTimelinePage::more.
void fetchRecommendedVideos(NcmClient &client, int offset, QObject *context,
                            VideoTimelineCallback callback);

void fetchVideoDetail(NcmClient &client, QString vid, QObject *context,
                      VideoDetailCallback callback);

// Requests playable URLs for one opaque video id at `resolution` (vertical
// pixels, e.g. 480/720/1080). An empty result list means no URL is available.
void fetchVideoUrls(NcmClient &client, QString vid, int resolution, QObject *context,
                    VideoPlayUrlsCallback callback);

// Pure parsers, exposed for tests and callers that already hold a response.
Result<VideoTimelinePage> parseRecommendedVideos(const QJsonObject &root);
Result<Video> parseVideoDetail(const QJsonObject &root);
Result<QList<VideoPlayUrl>> parseVideoUrls(const QJsonObject &root);

} // namespace ncm::sdk
