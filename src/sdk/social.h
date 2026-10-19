#pragma once
// Social SDK — typed read model for a user's activity feed, plus follow and
// event write actions.
//
// Raw endpoints:
//   POST /api/event/get/<uid>      form: getcounts,time,limit,total,fromRN
//   POST /api/user/follow/<uid>    form: t=1|2
//   POST /api/event/forward        form: forwards,id,eventUserId
//   POST /api/event/delete         weapi: id
//   POST /api/event/privacy/op     form: eventId,privacy
//
// The upstream event object is very wide and UI-specific. This SDK keeps the
// stable, renderable subset and ignores unknown fields.
#include "sdk/sdk_result.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct EventUser {
    qint64 id = 0;         // user.userId
    QString name;          // user.nickname
    QString avatar;        // user.avatarUrl
    QString pendant;       // pendantData.imageUrl
    QString identityIcon;  // avatarDetail.identityIconUrl
};

struct EventResource {
    qint64 id = 0;         // info.commentThread.resourceInfo.id
    QString name;          // resourceInfo.name
    QString cover;         // resourceInfo.imgUrl
    QString webUrl;        // resourceInfo.webUrl
    int resourceType = 0;  // info.commentThread.resourceType
    int eventType = 0;     // resourceInfo.eventType
};

struct UserEvent {
    qint64 id = 0;         // events[].id
    QString actionName;    // events[].actName
    QString title;         // info.title, fallback resourceInfo.name
    QString description;   // info.description
    EventUser user;        // events[].user
    EventResource resource;
    qint64 likeCount = 0;  // info.likedCount
    qint64 commentCount = 0; // info.commentCount
    qint64 shareCount = 0; // info.shareCount
    qint64 eventTime = 0;  // events[].eventTime (milliseconds)
};

struct UserFollowState {
    qint64 userId = 0;
    bool followed = false;
};

struct EventPage {
    QList<UserEvent> items;
    bool more = false;
    qint64 size = 0;
    qint64 lasttime = 0;
};

using UserEventsCallback = std::function<void(Result<EventPage>)>;
using UserFollowCallback = std::function<void(Result<UserFollowState>)>;
using SocialActionCallback = std::function<void(Result<bool>)>;

// `personal` attaches the stored MUSIC_U cookie; pass true for the current
// account's own feed, false for a public profile.
// lasttime <= 0 uses the API's initial cursor (-1).
void fetchUserEvents(NcmClient &client, qint64 uid, qint64 lasttime, int limit,
                     bool personal, QObject *context, UserEventsCallback callback);

// Follow/unfollow a user. This is a write: no retry, requires the stored
// MUSIC_U cookie. The returned state is the intent accepted by the server.
void setUserFollowed(NcmClient &client, qint64 userId, bool followed, QObject *context,
                     UserFollowCallback callback);

// Event write actions. They answer with `code=200` only, so the SDK reports
// success as a boolean. `eventUserId` is the event author, not the caller.
void forwardEvent(NcmClient &client, qint64 eventId, qint64 eventUserId,
                  const QString &comment, QObject *context,
                  SocialActionCallback callback);
void deleteEvent(NcmClient &client, qint64 eventId, QObject *context,
                 SocialActionCallback callback);
// privacy: 0 everyone, 1 following, 2 only me, 6 mutual follows.
void setEventPrivacy(NcmClient &client, qint64 eventId, int privacy,
                     QObject *context, SocialActionCallback callback);

Result<EventPage> parseUserEvents(const QJsonObject &root);

} // namespace ncm::sdk
