#include "sdk/listen_together.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto CreateRoomPath = "api/listen/together/room/create";
constexpr auto AcceptRoomPath = "api/listen/together/play/invitation/accept";
constexpr auto EndRoomPath = "api/listen/together/end/v2";
constexpr auto StatusPath = "api/listen/together/status/get";
constexpr auto CheckRoomPath = "api/listen/together/room/check";
constexpr auto SyncPlaylistPath = "api/listen/together/sync/playlist/get";
constexpr auto HeartbeatPath = "api/listen/together/heartbeat";
constexpr auto PlayCommandPath = "api/listen/together/play/command/report";
constexpr auto SyncListCommandPath = "api/listen/together/sync/list/command/report";

// The generic parseListenResult carries no service endpoint; label its errors
// with the REST namespace. Service callers use the exact endpoint instead.
constexpr auto ResultPath = "api/listen/together";

QString fieldPath(const QString &objectPath, const char *key)
{
    return objectPath + QLatin1Char('.') + QLatin1String(key);
}

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

QString decodeEndpoint(const char *endpoint)
{
    return QString::fromLatin1(endpoint);
}

template <typename T>
Result<T> invalidRequest(const char *endpoint, QString message)
{
    return Result<T>::failure(
        endpointError(ErrorKind::InvalidRequest, decodeEndpoint(endpoint), std::move(message)));
}

Result<QJsonObject> dataObject(const QJsonObject &root, const char *endpoint,
                               const QString &rootPath)
{
    const QString path = fieldPath(rootPath, "data");
    const QJsonValue value = root.value(QStringLiteral("data"));
    if (value.isUndefined() || value.isNull())
        return Result<QJsonObject>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            path + QStringLiteral(": missing data"), path));
    if (!value.isObject())
        return Result<QJsonObject>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            path + QStringLiteral(": expected object"), path));
    return Result<QJsonObject>::success(value.toObject());
}

// Listen-together song ids are numeric strings in the real payload; some
// clients encode them as JSON numbers. Missing/null ids are absent (0), any
// other shape is a Parse error at the exact element path.
Result<qint64> songIdValue(const QJsonValue &value, const QString &path,
                           const char *endpoint)
{
    if (value.isUndefined() || value.isNull())
        return Result<qint64>::success(0);
    if (value.isDouble())
        return Result<qint64>::success(static_cast<qint64>(value.toDouble()));
    if (value.isString()) {
        bool ok = false;
        const qint64 id = value.toString().toLongLong(&ok);
        if (ok)
            return Result<qint64>::success(id);
        return Result<qint64>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            path + QStringLiteral(": expected numeric id"), path));
    }
    return Result<qint64>::failure(endpointError(
        ErrorKind::Parse, decodeEndpoint(endpoint),
        path + QStringLiteral(": expected string or number"), path));
}

Result<ListenTogetherRoom> roomFrom(const QJsonObject &object, const QString &path,
                                    const char *endpoint)
{
    JsonReader reader(object, path);
    ListenTogetherRoom room;
    QString badPath;

    room.roomId = reader.string("roomId");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "roomId");
    room.creatorId = reader.integer("creatorId");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "creatorId");
    const QJsonArray users = reader.array("roomUsers");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "roomUsers");
    if (!reader.ok())
        return parseFailure<ListenTogetherRoom>(decodeEndpoint(endpoint), reader, badPath);

    if (room.roomId.isEmpty()) {
        const QString roomIdPath = fieldPath(path, "roomId");
        return Result<ListenTogetherRoom>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            roomIdPath + QStringLiteral(": missing roomId"), roomIdPath));
    }

    const QString usersPath = fieldPath(path, "roomUsers");
    room.users.reserve(users.size());
    for (qsizetype i = 0; i < users.size(); ++i) {
        const QString userPath = indexedPath(usersPath, i);
        if (!users.at(i).isObject())
            return itemTypeFailure<ListenTogetherRoom>(decodeEndpoint(endpoint), userPath);

        JsonReader userReader(users.at(i).toObject(), userPath);
        ListenTogetherUser user;
        QString userBadPath;
        user.userId = userReader.integer("userId");
        if (!userReader.ok() && userBadPath.isEmpty())
            userBadPath = fieldPath(userPath, "userId");
        user.nickname = userReader.string("nickname");
        if (!userReader.ok() && userBadPath.isEmpty())
            userBadPath = fieldPath(userPath, "nickname");
        user.avatar = userReader.string("avatarUrl");
        if (!userReader.ok() && userBadPath.isEmpty())
            userBadPath = fieldPath(userPath, "avatarUrl");
        user.identity = userReader.string("identityName");
        if (!userReader.ok() && userBadPath.isEmpty())
            userBadPath = fieldPath(userPath, "identityName");
        if (!userReader.ok())
            return parseFailure<ListenTogetherRoom>(decodeEndpoint(endpoint), userReader,
                                                   userBadPath);
        room.users.append(std::move(user));
    }
    return Result<ListenTogetherRoom>::success(std::move(room));
}

// room/create and play/invitation/accept share data.roomInfo, but each service
// must report its own endpoint on parse errors.
Result<ListenTogetherRoom> roomResponse(const QJsonObject &root, const char *endpoint,
                                        const QString &rootPath)
{
    const auto data = dataObject(root, endpoint, rootPath);
    if (!data)
        return Result<ListenTogetherRoom>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    const QString roomPath = fieldPath(dataPath, "roomInfo");
    const QJsonValue roomValue = data.value->value(QStringLiteral("roomInfo"));
    if (roomValue.isUndefined() || roomValue.isNull())
        return missingFailure<ListenTogetherRoom>(decodeEndpoint(endpoint), roomPath,
                                                 QStringLiteral("object"));
    if (!roomValue.isObject())
        return itemTypeFailure<ListenTogetherRoom>(decodeEndpoint(endpoint), roomPath);
    return roomFrom(roomValue.toObject(), roomPath, endpoint);
}

Result<ListenTogetherCommand> commandFrom(const QJsonObject &object, const QString &path,
                                          const char *endpoint)
{
    JsonReader reader(object, path);
    ListenTogetherCommand command;
    QString badPath;

    command.userId = reader.integer("userId");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "userId");
    command.commandType = reader.string("commandType");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "commandType");
    command.progress = reader.integer("progress");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "progress");
    command.playStatus = reader.string("playStatus");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "playStatus");
    command.clientSeq = reader.integer("clientSeq");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "clientSeq");
    command.serverSeq = reader.integer("serverSeq");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "serverSeq");
    if (!reader.ok())
        return parseFailure<ListenTogetherCommand>(decodeEndpoint(endpoint), reader, badPath);

    const auto former = songIdValue(object.value(QStringLiteral("formerSongId")),
                                    fieldPath(path, "formerSongId"), endpoint);
    if (!former)
        return Result<ListenTogetherCommand>::failure(former.error);
    command.formerSongId = *former.value;

    const auto target = songIdValue(object.value(QStringLiteral("targetSongId")),
                                    fieldPath(path, "targetSongId"), endpoint);
    if (!target)
        return Result<ListenTogetherCommand>::failure(target.error);
    command.targetSongId = *target.value;

    return Result<ListenTogetherCommand>::success(std::move(command));
}

Result<ListenTogetherPlaylist> playlistFrom(const QJsonObject &object, const QString &path,
                                            const char *endpoint)
{
    JsonReader reader(object, path);
    ListenTogetherPlaylist playlist;
    QString badPath;

    playlist.replaced = reader.boolean("replaced");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "replaced");
    const QJsonArray randomList = reader.array("randomList");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(path, "randomList");
    if (!reader.ok())
        return parseFailure<ListenTogetherPlaylist>(decodeEndpoint(endpoint), reader, badPath);

    const QString randomPath = fieldPath(path, "randomList");
    playlist.randomList.reserve(randomList.size());
    for (qsizetype i = 0; i < randomList.size(); ++i) {
        const auto id = songIdValue(randomList.at(i), indexedPath(randomPath, i), endpoint);
        if (!id)
            return Result<ListenTogetherPlaylist>::failure(id.error);
        playlist.randomList.append(*id.value);
    }

    const QString displayPath = fieldPath(path, "displayList");
    const QJsonValue displayValue = object.value(QStringLiteral("displayList"));
    if (!displayValue.isUndefined() && !displayValue.isNull()) {
        if (!displayValue.isObject())
            return itemTypeFailure<ListenTogetherPlaylist>(decodeEndpoint(endpoint), displayPath);

        JsonReader displayReader(displayValue.toObject(), displayPath);
        const QJsonArray displayList = displayReader.array("result");
        const QString resultPath = fieldPath(displayPath, "result");
        if (!displayReader.ok())
            return parseFailure<ListenTogetherPlaylist>(decodeEndpoint(endpoint), displayReader,
                                                        resultPath);

        playlist.displayList.reserve(displayList.size());
        for (qsizetype i = 0; i < displayList.size(); ++i) {
            const auto id = songIdValue(displayList.at(i), indexedPath(resultPath, i), endpoint);
            if (!id)
                return Result<ListenTogetherPlaylist>::failure(id.error);
            playlist.displayList.append(*id.value);
        }
    }

    return Result<ListenTogetherPlaylist>::success(std::move(playlist));
}

Result<ListenTogetherResult> resultFrom(const QJsonObject &root, const char *endpoint)
{
    const QString rootPath = QStringLiteral("listenTogether.result");
    const auto data = dataObject(root, endpoint, rootPath);
    if (!data)
        return Result<ListenTogetherResult>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    const QJsonObject payload = *data.value;
    JsonReader reader(payload, dataPath);
    ListenTogetherResult result;

    if (payload.contains(QStringLiteral("result"))) {
        result.success = reader.boolean("result");
        if (!reader.ok())
            return parseFailure<ListenTogetherResult>(decodeEndpoint(endpoint), reader,
                                                      fieldPath(dataPath, "result"));
    } else if (payload.contains(QStringLiteral("success"))) {
        result.success = reader.boolean("success");
        if (!reader.ok())
            return parseFailure<ListenTogetherResult>(decodeEndpoint(endpoint), reader,
                                                      fieldPath(dataPath, "success"));
    } else {
        return Result<ListenTogetherResult>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            dataPath + QStringLiteral(": missing result/success"), dataPath));
    }

    result.message = reader.string("message");
    if (!reader.ok())
        return parseFailure<ListenTogetherResult>(decodeEndpoint(endpoint), reader,
                                                  fieldPath(dataPath, "message"));
    if (result.message.isEmpty()) {
        // Some actions put the human-readable status on the envelope root.
        const QJsonValue rootMessage = root.value(QStringLiteral("message"));
        if (rootMessage.isString())
            result.message = rootMessage.toString();
    }
    return Result<ListenTogetherResult>::success(std::move(result));
}

} // namespace

Result<ListenTogetherRoom> parseRoomCreate(const QJsonObject &root)
{
    return roomResponse(root, CreateRoomPath, QStringLiteral("listenTogether.create"));
}

Result<ListenTogetherStatus> parseRoomStatus(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenTogether.status");
    const auto data = dataObject(root, StatusPath, rootPath);
    if (!data)
        return Result<ListenTogetherStatus>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    const QJsonObject payload = *data.value;
    JsonReader reader(payload, dataPath);
    ListenTogetherStatus status;
    QString badPath;

    status.inRoom = reader.boolean("inRoom");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "inRoom");
    status.status = reader.string("status");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "status");
    const QJsonObject roomInfo = reader.object("roomInfo");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "roomInfo");
    if (!reader.ok())
        return parseFailure<ListenTogetherStatus>(decodeEndpoint(StatusPath), reader, badPath);

    // roomInfo is absent/null when the account is not in a room; an empty
    // object carries no room id and likewise leaves `room` invalid.
    if (!roomInfo.isEmpty()) {
        const auto room = roomFrom(roomInfo, fieldPath(dataPath, "roomInfo"), StatusPath);
        if (!room)
            return Result<ListenTogetherStatus>::failure(room.error);
        status.room = *room.value;
    }
    return Result<ListenTogetherStatus>::success(std::move(status));
}

Result<ListenTogetherCheck> parseRoomCheck(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenTogether.check");
    const auto data = dataObject(root, CheckRoomPath, rootPath);
    if (!data)
        return Result<ListenTogetherCheck>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    JsonReader reader(*data.value, dataPath);
    ListenTogetherCheck check;
    QString badPath;

    check.joinable = reader.boolean("joinable");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "joinable");
    check.type = reader.string("type");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "type");
    check.status = reader.string("status");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "status");
    check.copywriting = reader.string("copywriting");
    if (!reader.ok() && badPath.isEmpty())
        badPath = fieldPath(dataPath, "copywriting");
    if (!reader.ok())
        return parseFailure<ListenTogetherCheck>(decodeEndpoint(CheckRoomPath), reader, badPath);
    return Result<ListenTogetherCheck>::success(std::move(check));
}

Result<ListenTogetherSync> parseSyncPlaylist(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenTogether.sync");
    const auto data = dataObject(root, SyncPlaylistPath, rootPath);
    if (!data)
        return Result<ListenTogetherSync>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    const QJsonObject payload = *data.value;
    ListenTogetherSync sync;

    const QString commandPath = fieldPath(dataPath, "playCommand");
    const QJsonValue commandValue = payload.value(QStringLiteral("playCommand"));
    if (!commandValue.isUndefined() && !commandValue.isNull()) {
        if (!commandValue.isObject())
            return itemTypeFailure<ListenTogetherSync>(decodeEndpoint(SyncPlaylistPath),
                                                      commandPath);
        const auto command = commandFrom(commandValue.toObject(), commandPath, SyncPlaylistPath);
        if (!command)
            return Result<ListenTogetherSync>::failure(command.error);
        sync.command = *command.value;
    }

    const QString playlistPath = fieldPath(dataPath, "playlist");
    const QJsonValue playlistValue = payload.value(QStringLiteral("playlist"));
    if (!playlistValue.isUndefined() && !playlistValue.isNull()) {
        if (!playlistValue.isObject())
            return itemTypeFailure<ListenTogetherSync>(decodeEndpoint(SyncPlaylistPath),
                                                      playlistPath);
        const auto playlist = playlistFrom(playlistValue.toObject(), playlistPath,
                                           SyncPlaylistPath);
        if (!playlist)
            return Result<ListenTogetherSync>::failure(playlist.error);
        sync.playlist = *playlist.value;
    }

    return Result<ListenTogetherSync>::success(std::move(sync));
}

Result<ListenTogetherResult> parseListenResult(const QJsonObject &root)
{
    return resultFrom(root, ResultPath);
}

void createRoom(NcmClient &client, QObject *context, RoomCallback callback)
{
    if (!callback)
        return;
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("refer"), QStringLiteral("songplay_more"));
    postFormJson(client, context, decodeEndpoint(CreateRoomPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherRoom>::failure(root.error));
            return;
        }
        callback(roomResponse(*root.value, CreateRoomPath,
                              QStringLiteral("listenTogether.create")));
    }, true);
}

void acceptRoom(NcmClient &client, QString roomId, qint64 inviterId, QObject *context,
                RoomCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherRoom>(AcceptRoomPath,
                                                    QStringLiteral("roomId must not be empty")));
        return;
    }
    if (inviterId <= 0) {
        callback(invalidRequest<ListenTogetherRoom>(AcceptRoomPath,
                                                    QStringLiteral("inviterId must be positive")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("refer"), QStringLiteral("inbox_invite"));
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("inviterId"), QString::number(inviterId));
    postFormJson(client, context, decodeEndpoint(AcceptRoomPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherRoom>::failure(root.error));
            return;
        }
        callback(roomResponse(*root.value, AcceptRoomPath,
                              QStringLiteral("listenTogether.accept")));
    }, true);
}

void endRoom(NcmClient &client, QString roomId, QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(EndRoomPath,
                                                      QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    postFormJson(client, context, decodeEndpoint(EndRoomPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(resultFrom(*root.value, EndRoomPath));
    }, true);
}

void fetchStatus(NcmClient &client, QObject *context, StatusCallback callback)
{
    if (!callback)
        return;
    postFormJson(client, context, decodeEndpoint(StatusPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherStatus>::failure(root.error));
            return;
        }
        callback(parseRoomStatus(*root.value));
    }, true);
}

void checkRoom(NcmClient &client, QString roomId, QObject *context, CheckCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherCheck>(CheckRoomPath,
                                                     QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    postFormJson(client, context, decodeEndpoint(CheckRoomPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherCheck>::failure(root.error));
            return;
        }
        callback(parseRoomCheck(*root.value));
    }, true);
}

void fetchSyncPlaylist(NcmClient &client, QString roomId, QObject *context,
                       SyncCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherSync>(SyncPlaylistPath,
                                                    QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    postFormJson(client, context, decodeEndpoint(SyncPlaylistPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherSync>::failure(root.error));
            return;
        }
        callback(parseSyncPlaylist(*root.value));
    }, true);
}

void reportHeartbeat(NcmClient &client, QString roomId, qint64 songId, QString playStatus,
                     qint64 progressMs, QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(HeartbeatPath,
                                                      QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("songId"), QString::number(songId));
    form.addQueryItem(QStringLiteral("playStatus"), playStatus);
    form.addQueryItem(QStringLiteral("progress"), QString::number(progressMs));
    postFormJson(client, context, decodeEndpoint(HeartbeatPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(resultFrom(*root.value, HeartbeatPath));
    }, true);
}

void reportPlayCommand(NcmClient &client, QString roomId, QJsonObject commandInfo,
                       QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(PlayCommandPath,
                                                      QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("commandInfo"),
                      QString::fromUtf8(QJsonDocument(commandInfo).toJson(QJsonDocument::Compact)));
    postFormJson(client, context, decodeEndpoint(PlayCommandPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(resultFrom(*root.value, PlayCommandPath));
    }, true);
}

void reportPlaylist(NcmClient &client, QString roomId, QJsonObject playlistParam,
                    QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(SyncListCommandPath,
                                                      QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("playlistParam"),
                      QString::fromUtf8(QJsonDocument(playlistParam).toJson(QJsonDocument::Compact)));
    postFormJson(client, context, decodeEndpoint(SyncListCommandPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(resultFrom(*root.value, SyncListCommandPath));
    }, true);
}

} // namespace ncm::sdk
