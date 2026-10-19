#pragma once
// Listen-together SDK — typed room state, playback sync and action results.
//
// Raw endpoints (all POST form, plaintext /api, personal MUSIC_U attached):
//   api/listen/together/room/create                 refer=songplay_more
//   api/listen/together/play/invitation/accept      refer=inbox_invite, roomId, inviterId
//   api/listen/together/end/v2                      roomId
//   api/listen/together/status/get                  (empty form)
//   api/listen/together/room/check                  roomId
//   api/listen/together/sync/playlist/get           roomId
//   api/listen/together/heartbeat                   roomId, songId, playStatus, progress
//   api/listen/together/play/command/report         roomId, commandInfo (compact JSON string)
//   api/listen/together/sync/list/command/report    roomId, playlistParam (compact JSON string)
//   api/listen/together/multi/invite                roomId, groupIds, inviteUids (JSON string)
//   api/listen/together/invite/message/send         roomId, acceptorId
//   api/listen/together/privilege/get               otherUserId, trackIds (JSON array string)
//   api/listen/together/relation/statistics/get/v2  roomId, roomUserIds (JSON array string)
//   api/listen/together/vip/gift/report             sku, receiver, roomId
//   api/listen/together/sync/notice                 roomId
//
// Room ids are server-provided strings; user and song ids stay qint64. The
// command's former/target song ids are strings in the real payload but are also
// accepted as JSON numbers; a non-numeric string is a Parse error naming the
// field path. Missing/null command song ids normalize to 0.
//
// `parseRoomCreate`/`acceptRoom` require data.roomInfo with a non-empty roomId;
// roomUsers is optional. `parseSyncPlaylist` accepts an empty `data` object
// (no activity yet), a null/missing playCommand and a null/missing playlist.
// `parseListenResult` requires `data` with either `result` or `success`.
// Non-200 business codes are classified by the shared transport, not here.
#include "sdk/sdk_result.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class NcmClient;

namespace ncm::sdk {

struct ListenTogetherUser {
    qint64 userId = 0; // userId
    QString nickname;  // nickname
    QString avatar;    // avatarUrl (raw URL)
    QString pendant;   // pendantData.imageUrl (raw)
    QString identityIcon; // identityIcon / avatarDetail.identityIconUrl (raw)
    QString identity;  // identityName
    QString vipLevel;  // vipRights / vipType, see user_vip.h
};

struct ListenTogetherRoom {
    QString roomId;    // roomId (string, required)
    qint64 creatorId = 0; // creatorId
    QList<ListenTogetherUser> users; // roomUsers[], optional
    bool valid() const { return !roomId.isEmpty(); }
};

struct ListenTogetherStatus {
    bool inRoom = false;  // data.inRoom
    ListenTogetherRoom room; // data.roomInfo
    QString status;       // data.status
};

struct ListenTogetherCheck {
    bool joinable = false; // data.joinable
    QString type;          // data.type
    QString status;        // data.status
    QString copywriting;   // data.copywriting (nullable)
};

struct ListenTogetherCommand {
    qint64 userId = 0;      // userId
    QString commandType;    // commandType
    qint64 progress = 0;    // progress
    QString playStatus;     // playStatus
    qint64 formerSongId = 0; // formerSongId (string or number)
    qint64 targetSongId = 0; // targetSongId (string or number)
    qint64 clientSeq = 0;   // clientSeq
    qint64 serverSeq = 0;   // serverSeq
};

struct ListenTogetherVersion {
    qint64 userId = 0;  // playlist.version[].userId
    qint64 version = 0; // playlist.version[].version
};

struct ListenTogetherPlaylist {
    QList<qint64> displayList; // playlist.displayList.result[] (string or number ids)
    QList<qint64> randomList;  // playlist.randomList[] (string or number ids)
    bool displayChanged = false; // playlist.displayList.changed
    bool randomChanged = false;  // playlist.randomList.changed
    bool replaced = false;       // playlist.replace（旧回包为 replaced）
    QList<ListenTogetherVersion> versions; // playlist.version[]：每个成员改歌单的版本号
    QString playMode;          // playlist.playMode：ORDER_LOOP / SINGLE_LOOP / RANDOM
};

struct ListenTogetherSync {
    std::optional<ListenTogetherCommand> command; // data.playCommand
    ListenTogetherPlaylist playlist;              // data.playlist
};

struct ListenTogetherResult {
    bool success = false; // data.result or data.success
    QString message;      // data.message, fallback to root message
};

// data.result[] of api/listen/together/privilege/get. The endpoint answers
// "can this other member play these tracks?"; songId is the server's field even
// though the web client renames it to trackId for its own store.
struct ListenTogetherPrivilege {
    qint64 songId = 0;             // data.result[].songId
    bool playable = false;         // data.result[].playable
    QString unplayableType;        // data.result[].unplayableType
    QList<qint64> unplayableUserIds;  // data.result[].unplayableUserIds
    QList<qint64> unplayableOuterIds; // data.result[].unplayableOuterIds
};

// data of api/listen/together/relation/statistics/get/v2. This is the pair's
// cumulative listen-together counters, not the current room duration.
struct ListenTogetherStatistics {
    qint64 listenCount = 0;          // data.listenCount
    qint64 totalConnectionTime = 0;  // data.totalConnectionTime（秒）
};

using RoomCallback = std::function<void(Result<ListenTogetherRoom>)>;
using StatusCallback = std::function<void(Result<ListenTogetherStatus>)>;
using CheckCallback = std::function<void(Result<ListenTogetherCheck>)>;
using SyncCallback = std::function<void(Result<ListenTogetherSync>)>;
using ResultCallback = std::function<void(Result<ListenTogetherResult>)>;
using PrivilegeCallback = std::function<void(Result<QList<ListenTogetherPrivilege>>)>;
using StatisticsCallback = std::function<void(Result<ListenTogetherStatistics>)>;

// Pure parsers, exposed for tests and callers that already hold a response.
Result<ListenTogetherRoom> parseRoomCreate(const QJsonObject &root);
Result<ListenTogetherStatus> parseRoomStatus(const QJsonObject &root);
Result<ListenTogetherCheck> parseRoomCheck(const QJsonObject &root);
Result<ListenTogetherSync> parseSyncPlaylist(const QJsonObject &root);
Result<ListenTogetherResult> parseListenResult(const QJsonObject &root);
Result<QList<ListenTogetherPrivilege>> parsePrivileges(const QJsonObject &root);
Result<ListenTogetherStatistics> parseRelationStatistics(const QJsonObject &root);
// Action endpoints that only promise `code=200` (invite message, VIP gift,
// sync notice). Unlike parseListenResult this does not require data.result.
Result<ListenTogetherResult> parseActionResult(const QJsonObject &root);

// One callback per call, on the Qt thread; dropped when `context` dies first.
// Empty roomId (and a non-positive inviterId for acceptRoom) is reported as an
// InvalidRequest without sending a request.
void createRoom(NcmClient &client, QObject *context, RoomCallback callback);
void acceptRoom(NcmClient &client, QString roomId, qint64 inviterId,
                QObject *context, RoomCallback callback);
void endRoom(NcmClient &client, QString roomId, QObject *context,
             ResultCallback callback);
void fetchStatus(NcmClient &client, QObject *context, StatusCallback callback);
void checkRoom(NcmClient &client, QString roomId, QObject *context,
               CheckCallback callback);
void fetchSyncPlaylist(NcmClient &client, QString roomId, QJsonObject playlistParam,
                       QObject *context, SyncCallback callback);
void reportHeartbeat(NcmClient &client, QString roomId, qint64 songId,
                     QString playStatus, qint64 progressMs, QJsonArray playlistVersion,
                     QObject *context, ResultCallback callback);
void reportPlayCommand(NcmClient &client, QString roomId, QJsonObject commandInfo,
                       QObject *context, ResultCallback callback);
void reportPlaylist(NcmClient &client, QString roomId, QJsonObject playlistParam,
                    qint64 clientSeq, QObject *context, ResultCallback callback);
void inviteUsers(NcmClient &client, QString roomId, QList<qint64> userIds,
                 QObject *context, ResultCallback callback);
// Sends the inbox invitation card to one friend (type 23 generalMsg on their
// side). `acceptorId` is the friend's uid; `roomId` must already exist.
void sendInviteMessage(NcmClient &client, QString roomId, qint64 acceptorId,
                       QObject *context, ResultCallback callback);
// Per-track playability for the other member. Empty trackIds means "all tracks
// in the room": the field is omitted and the server returns its full result.
void fetchPrivileges(NcmClient &client, qint64 otherUserId, QList<qint64> trackIds,
                     QObject *context, PrivilegeCallback callback);
// Cumulative counters for the host + one other member. `userIds` is normalized
// (positive ids only, duplicates removed) before it is sent.
void fetchRelationStatistics(NcmClient &client, QString roomId, QList<qint64> userIds,
                             QObject *context, StatisticsCallback callback);
void reportVipGift(NcmClient &client, QString sku, qint64 receiver, QString roomId,
                   QObject *context, ResultCallback callback);
void notifyPlaylistChange(NcmClient &client, QString roomId, QObject *context,
                          ResultCallback callback);

} // namespace ncm::sdk
