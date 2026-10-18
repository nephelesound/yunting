#pragma once
// Notification SDK — typed models for the signed-in message center.
//
// Raw endpoints (all personal weapi POST; the stored MUSIC_U cookie is required):
//   POST /weapi/msg/notices                system notices
//   POST /weapi/v1/user/comments/<uid>     comments/replies addressed to me
//        (legacy docs advertise /msg/comments; the enhanced module uses v1)
//   POST /weapi/forwards/get               @me / forwards
//
// Error::endpoint carries the weapi-relative path (`msg/notices`,
// `v1/user/comments/`, `forwards/get`), because the shared transport prepends
// `weapi/`. The reference modules' `/api/...` prefix is intentionally not
// passed through.
//
// Each fetch passes personal=true. Without a signed-in session the server answers
// a business code (typically 301); postWeapiJson surfaces that as
// ErrorKind::Api with Error::code set. These reads are not retried.
//
// Pagination/cursor contract:
//   notices   request `time` (initial -1) and `limit`; response `more`. The next
//             cursor is the last item's `time` (milliseconds), exposed as
//             SystemNoticePage::nextTime.
//   comments  request `beforeTime` (initial -1) and `limit`; response `more` and
//             `total`. The next cursor is the last item's `time` (milliseconds),
//             exposed as CommentNotificationPage::nextBeforeTime. The upstream
//             docs call this pivot `updateTime`; `time` is the timestamp present
//             in the observed comment objects.
//   forwards  request `offset`/`limit`; response `more`, `newCount` and
//             `lasttime`. ForwardNotificationPage::offset is the requested
//             offset; the next page offset is offset + items.size(). `lasttime`
//             is exposed as observed but offset is the pagination contract used
//             by the reference module.
//
// Shape evidence and uncertainty:
// - Field comments name raw JSON paths. Where a field lives inside an embedded
//   JSON string, the comment shows the decode step, e.g.
//   `notices[].notice -> .generalNotice.actionDesc`.
// - `notices[].id` and `notices[].userId` are strings in captured responses but
//   numbers in other recordings; both shapes are accepted. Forward ids are
//   numbers in the reference client. A non-numeric id (for example some video
//   `vid`s) becomes 0; the raw string is not retained.
// - `notices[].notice`, `forwards[].json` and the nested `event.json` are
//   decoded best-effort. Missing, empty, non-string or malformed embedded JSON
//   leaves the outer row usable and sets payloadValid=false. Type errors inside
//   optional embedded objects are ignored (they default to empty/0).
// - `notices[].pushMsg` is null in observed samples and its non-null shape is
//   undocumented, so it is deliberately not modeled.
// - Comment notifications do not expose an explicit resource id in the observed
//   model. `combinedId` keeps the server's opaque `combindId`; do not split it
//   into resource ids/URLs without confirming the format.
// - Forward resources are normalized from the shared object S:
//   `forwards[].json` decodes to E { user, type, json };
//   E.json decodes to S { msg, event, song, playlist, album, program, video };
//   S.event.json decodes to T with the same resource keys. The parser prefers
//   T's resource, then S.event as a generic event; S's direct resource is used
//   only when S.event is absent. `resourceKind` is derived from which object
//   was found, not a server field.
// - No authenticated live request was made while writing this SDK. Shapes come
//   from docs/reference/ncm-api-enhanced/module/msg_*.js and the typed response
//   models in the open-source client zion223/NeteaseCloudMusic-MVVM
//   (PrivateNoticeBean, PrivateCommentBean, ForwardsMeBean, ForwardsEventBean).
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

// Shared user subset. Paths are relative to the enclosing raw object.
struct NotificationUser {
    qint64 id = 0;      // userId (number or numeric string)
    QString name;       // nickname
    QString avatar;     // avatarUrl
};

// notices[].notice after JSON-decoding. Which members are meaningful depends on
// `.type` and on which sub-object is present; all are optional.
struct SystemNoticePayload {
    int type = 0;                  // .type
    NotificationUser user;         // .user
    qint64 commentId = 0;          // .comment.commentId
    QString commentContent;        // .comment.content
    qint64 commentTime = 0;        // .comment.time (milliseconds)
    qint64 commentParentId = 0;    // .comment.parentCommentId
    qint64 playlistId = 0;         // .playlist.id
    QString playlistName;          // .playlist.name
    QString playlistCover;         // .playlist.coverImgUrl
    QString actionDesc;            // .generalNotice.actionDesc
    QString content;               // .generalNotice.content
    QString webUrl;                // .generalNotice.webUrl
    QString nativeUrl;             // .generalNotice.nativeUrl
    int generalType = 0;           // .generalNotice.type
    qint64 resourceId = 0;         // .generalNotice.resourceId
    int specialResourceType = 0;   // .generalNotice.specialResourceType
                                   // (0 when null/non-numeric/omitted)
};

// notices[] of /weapi/msg/notices
struct SystemNotice {
    qint64 id = 0;             // notices[].id (number or numeric string)
    qint64 userId = 0;         // notices[].userId (number or numeric string)
    qint64 time = 0;           // notices[].time (milliseconds)
    bool queued = false;       // notices[].queued
    int type = 0;              // notices[].type
    bool payloadValid = false; // notices[].notice decoded into a JSON object
    SystemNoticePayload payload;
};

struct SystemNoticePage {
    QList<SystemNotice> items; // notices[]
    bool more = false;         // more
    qint64 nextTime = -1;      // items.last().time; feed as `lasttime`; -1 when empty
};

// comments[] of /weapi/v1/user/comments/<uid>
struct CommentNotification {
    NotificationUser user;              // comments[].user
    NotificationUser repliedUser;       // comments[].beRepliedUser (optional)
    QString repliedContent;             // comments[].beRepliedContent (optional)
    QString content;                    // comments[].content
    qint64 time = 0;                    // comments[].time (milliseconds)
    qint64 parentCommentId = 0;         // comments[].parentCommentId
    int resourceType = 0;               // comments[].resourceType
    int commentLocationType = 0;        // comments[].commentLocationType
    QString combinedId;                 // comments[].combindId (opaque server identifier)
    QString expressionUrl;              // comments[].expressionUrl (null/string; else empty)
    QString repliedExpressionUrl;       // comments[].beRepliedExpressionUrl
                                        // (null/string; else empty)
};

struct CommentNotificationPage {
    QList<CommentNotification> items; // comments[]
    bool more = false;                // more
    qint64 total = 0;                 // total
    qint64 nextBeforeTime = -1;       // items.last().time; use as the next
                                      // beforeTime; -1 when empty
};

// forwards[] of /weapi/forwards/get
struct ForwardNotification {
    qint64 id = 0;             // forwards[].id
    qint64 userId = 0;         // forwards[].userId
    qint64 time = 0;           // forwards[].time (milliseconds)
    int type = 0;              // forwards[].type
    bool payloadValid = false; // forwards[].json decoded into a JSON object (E)
    QString message;           // E.json -> .msg (fallback E.json.event.json -> .msg)
    NotificationUser user;     // E.user (fallback E.json.event.user)
    int eventType = 0;         // E.type (fallback E.json.event.type)
    QString resourceKind;      // derived: song/playlist/album/program/video/event/empty
    qint64 resourceId = 0;     // .song.id / .playlist.id / .album.id / .program.id /
                               // .video.videoId / .event.id
    QString resourceTitle;     // .song.name / .playlist.name / .album.name /
                               // .program.name / .video.title /
                               // .event.info.title / .event.info.resourceTitle
    QString resourceCover;     // .song.album.picUrl / .playlist.coverImgUrl /
                               // .album.picUrl / .program.coverUrl /
                               // .video.coverUrl / .event.info.commentThread.resourceInfo.imgUrl
    QString resourceArtist;    // .song.artists[0].name / .album.artist.name /
                               // .playlist.creator.nickname / .program.radio.name /
                               // .video.creator.nickname / .event.user.nickname
};

struct ForwardNotificationPage {
    QList<ForwardNotification> items; // forwards[]
    bool more = false;                // more
    int newCount = 0;                 // newCount
    qint64 lasttime = 0;              // lasttime (milliseconds; observed, not used
                                      // for offset paging)
    int offset = 0;                   // requested offset, filled by fetchForwardNotifications
};

using SystemNoticesCallback = std::function<void(Result<SystemNoticePage>)>;
using CommentNotificationsCallback = std::function<void(Result<CommentNotificationPage>)>;
using ForwardNotificationsCallback = std::function<void(Result<ForwardNotificationPage>)>;

// System notices. `lasttime <= 0` uses the initial cursor (-1); `limit` is
// clamped to at least 1.
void fetchSystemNotices(NcmClient &client, int limit, qint64 lasttime,
                        QObject *context, SystemNoticesCallback callback);

// Comments/replies addressed to the signed-in user. `uid` must be positive and
// match the signed-in account; `beforeTime <= 0` uses the initial cursor (-1).
void fetchCommentNotifications(NcmClient &client, qint64 uid, int limit,
                               qint64 beforeTime, QObject *context,
                               CommentNotificationsCallback callback);

// @me / forwards. `offset` is clamped to at least 0; `limit` to at least 1.
void fetchForwardNotifications(NcmClient &client, int limit, int offset,
                               QObject *context, ForwardNotificationsCallback callback);

// Pure parsers, exposed for tests and callers that already hold a response.
// The comment parser's errors carry the endpoint prefix `v1/user/comments/`
// because the uid is a request argument, not part of the response.
Result<SystemNoticePage> parseSystemNotices(const QJsonObject &root);
Result<CommentNotificationPage> parseCommentNotifications(const QJsonObject &root);
Result<ForwardNotificationPage> parseForwardNotifications(const QJsonObject &root);

} // namespace ncm::sdk
