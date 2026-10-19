#pragma once
// Friend-listening SDK — the "friends currently / recently listening" list.
//
// Raw endpoint (POST form, personal MUSIC_U):
//   api/listen/together/mutual/follows/get/v2
//
// The response is a friend directory, not a play history: NetEase exposes
// online/in-room flags, last-listen time and a short description per mutual
// follow, plus the pendant they are wearing. It does not expose the song title
// in this payload. Common-history/statistics endpoints are separate and are
// not part of this model.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct FriendListeningUser {
    qint64 userId = 0;       // userInfo.userId
    QString name;            // row customNickname (your remark) / userInfo.nickname
    QString avatar;          // userInfo.avatarUrl (raw)
    QString avatarPendant;   // pendant.image / imageAndroid / imageIos (raw)
    QString identityIcon;    // userInfo.avatarDetail.identityIconUrl (raw)
    QString signature;       // userInfo.signature
    bool mutual = false;     // userInfo.mutual
    bool followed = false;   // userInfo.followed
    int vipType = 0;         // userInfo.vipType
    QString vipLevel;        // derived: vip / svip / empty
};

struct FriendListening {
    FriendListeningUser user; // userInfo / pendant / display name
    bool lastListen = false;  // lastListen
    int listenStatus = 0;     // listenStatus
    qint64 lastTime = 0;      // lastTime (milliseconds)
    bool online = false;      // online
    bool inRoom = false;      // inRoom (already in a listen-together room)
    QString description;      // description (short presence text, may be empty)
};

struct FriendListeningPage {
    QList<FriendListening> items; // mutualFollowLists[], server order
    int streakGlobalSwitch = 0;   // streakGlobalSwitch
    int streakUserSwitch = 0;     // streakUserSwitch
    QString matchButtonText;      // matchButtonText
};

using FriendListeningCallback = std::function<void(Result<FriendListeningPage>)>;

void fetchFriendListening(NcmClient &client, QObject *context,
                          FriendListeningCallback callback);
Result<FriendListeningPage> parseFriendListening(const QJsonObject &root);

} // namespace ncm::sdk
