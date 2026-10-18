#pragma once
// Social SDK — typed read model for a user's activity feed.
//
// Raw endpoint:
//   POST /api/event/get/<uid>  form: getcounts,time,limit,total,fromRN
//
// The upstream event object is very wide and UI-specific. This SDK keeps the
// stable, renderable subset and ignores unknown fields. It does not expose event
// write/follow operations yet; those need a login and a separate contract.
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

// `personal` attaches the stored MUSIC_U cookie; pass true for the current
// account's own feed, false for a public profile.
// lasttime <= 0 uses the API's initial cursor (-1).
void fetchUserEvents(NcmClient &client, qint64 uid, qint64 lasttime, int limit,
                     bool personal, QObject *context, UserEventsCallback callback);

// Follow/unfollow a user. This is a write: no retry, requires the stored
// MUSIC_U cookie. The returned state is the intent accepted by the server.
void setUserFollowed(NcmClient &client, qint64 userId, bool followed, QObject *context,
                     UserFollowCallback callback);

Result<EventPage> parseUserEvents(const QJsonObject &root);

} // namespace ncm::sdk
