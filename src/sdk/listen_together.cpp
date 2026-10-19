#include "sdk/listen_together.h"

#include "ncmclient.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"
#include "sdk/user_vip.h"

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
constexpr auto InviteMessagePath = "api/listen/together/invite/message/send";
constexpr auto PrivilegePath = "api/listen/together/privilege/get";
constexpr auto StatisticsPath = "api/listen/together/relation/statistics/get/v2";
constexpr auto VipGiftPath = "api/listen/together/vip/gift/report";
constexpr auto SyncNoticePath = "api/listen/together/sync/notice";

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

        const QJsonObject object = users.at(i).toObject();
        JsonReader userReader(object, userPath);
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
        // Upstream sends pendantData as an object on room members; identityIcon is
        // sometimes a direct URL field and sometimes inside avatarDetail.
        const QJsonObject pendant = userReader.object("pendantData");
        user.pendant = pendant.value(QStringLiteral("imageUrl")).toString();
        const QJsonObject avatarDetail = userReader.object("avatarDetail");
        user.identityIcon = userReader.string("identityIcon");
        if (user.identityIcon.isEmpty())
            user.identityIcon = avatarDetail.value(QStringLiteral("identityIconUrl")).toString();
        user.identity = userReader.string("identityName");
        if (!userReader.ok() && userBadPath.isEmpty())
            userBadPath = fieldPath(userPath, "identityName");
        user.vipLevel = vipLevel(object);
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

// The web client advertises a song list either as a bare array (older
// payloads) or as {"changed": bool, "result": [...]}. Missing lists are valid:
// a room with no queue yet returns no displayList/randomList at all.
Result<QList<qint64>> songIdList(const QJsonValue &value, const QString &path,
                                const char *endpoint)
{
    if (value.isUndefined() || value.isNull())
        return Result<QList<qint64>>::success({});

    QJsonValue list = value;
    QString listPath = path;
    if (value.isObject()) {
        listPath = fieldPath(path, "result");
        list = value.toObject().value(QStringLiteral("result"));
        if (list.isUndefined() || list.isNull())
            return Result<QList<qint64>>::success({});
    }
    if (!list.isArray()) {
        return Result<QList<qint64>>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            listPath + QStringLiteral(": expected array"), listPath));
    }

    QList<qint64> ids;
    const QJsonArray array = list.toArray();
    ids.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        const auto id = songIdValue(array.at(i), indexedPath(listPath, i), endpoint);
        if (!id)
            return Result<QList<qint64>>::failure(id.error);
        ids.append(*id.value);
    }
    return Result<QList<qint64>>::success(std::move(ids));
}

// Arrays such as unplayableUserIds are optional; every present element must be
// a numeric id (string or number), and null entries are ignored.
Result<QList<qint64>> idList(const QJsonValue &value, const QString &path,
                             const char *endpoint)
{
    if (value.isUndefined() || value.isNull())
        return Result<QList<qint64>>::success({});
    if (!value.isArray())
        return Result<QList<qint64>>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(endpoint),
            path + QStringLiteral(": expected array"), path));

    QList<qint64> ids;
    const QJsonArray array = value.toArray();
    ids.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        const auto id = songIdValue(array.at(i), indexedPath(path, i), endpoint);
        if (!id)
            return Result<QList<qint64>>::failure(id.error);
        if (*id.value > 0)
            ids.append(*id.value);
    }
    return Result<QList<qint64>>::success(std::move(ids));
}

Result<ListenTogetherPlaylist> playlistFrom(const QJsonObject &object, const QString &path,
                                            const char *endpoint)
{
    ListenTogetherPlaylist playlist;

    const QString replacePath = fieldPath(path, "replace");
    const QJsonValue replaceValue = object.value(QStringLiteral("replace"));
    if (!replaceValue.isUndefined() && !replaceValue.isNull()) {
        if (!replaceValue.isBool())
            return Result<ListenTogetherPlaylist>::failure(endpointError(
                ErrorKind::Parse, decodeEndpoint(endpoint),
                replacePath + QStringLiteral(": expected boolean"), replacePath));
        playlist.replaced = replaceValue.toBool();
    } else {
        // Older snapshots and the previous SDK tests used "replaced"; keep it
        // readable while the web client uses "replace".
        const QString oldPath = fieldPath(path, "replaced");
        const QJsonValue oldValue = object.value(QStringLiteral("replaced"));
        if (!oldValue.isUndefined() && !oldValue.isNull()) {
            if (!oldValue.isBool())
                return Result<ListenTogetherPlaylist>::failure(endpointError(
                    ErrorKind::Parse, decodeEndpoint(endpoint),
                    oldPath + QStringLiteral(": expected boolean"), oldPath));
            playlist.replaced = oldValue.toBool();
        }
    }

    const auto randomValue = object.value(QStringLiteral("randomList"));
    const auto random = songIdList(randomValue, fieldPath(path, "randomList"), endpoint);
    if (!random)
        return Result<ListenTogetherPlaylist>::failure(random.error);
    playlist.randomList = *random.value;
    if (!randomValue.isUndefined() && !randomValue.isNull()) {
        playlist.randomChanged = true;
        if (randomValue.isObject()
            && !randomValue.toObject().value(QStringLiteral("changed")).isUndefined()) {
            const QJsonValue changed = randomValue.toObject().value(QStringLiteral("changed"));
            if (!changed.isBool())
                return Result<ListenTogetherPlaylist>::failure(endpointError(
                    ErrorKind::Parse, decodeEndpoint(endpoint),
                    fieldPath(fieldPath(path, "randomList"), "changed")
                        + QStringLiteral(": expected boolean"),
                    fieldPath(fieldPath(path, "randomList"), "changed")));
            playlist.randomChanged = changed.toBool();
        }
    }

    const auto displayValue = object.value(QStringLiteral("displayList"));
    const auto display = songIdList(displayValue, fieldPath(path, "displayList"), endpoint);
    if (!display)
        return Result<ListenTogetherPlaylist>::failure(display.error);
    playlist.displayList = *display.value;
    if (!displayValue.isUndefined() && !displayValue.isNull()) {
        playlist.displayChanged = true;
        if (displayValue.isObject()
            && !displayValue.toObject().value(QStringLiteral("changed")).isUndefined()) {
            const QJsonValue changed = displayValue.toObject().value(QStringLiteral("changed"));
            if (!changed.isBool())
                return Result<ListenTogetherPlaylist>::failure(endpointError(
                    ErrorKind::Parse, decodeEndpoint(endpoint),
                    fieldPath(fieldPath(path, "displayList"), "changed")
                        + QStringLiteral(": expected boolean"),
                    fieldPath(fieldPath(path, "displayList"), "changed")));
            playlist.displayChanged = changed.toBool();
        }
    }

    // version 是每个成员各自改歌单的版本号；下一次上报要把别人的版本原样带回，
    // 只带自己的版本时服务器会认为这次改动没有合并对方的队列。
    const QString versionsPath = fieldPath(path, "version");
    const QJsonValue versionsValue = object.value(QStringLiteral("version"));
    if (!versionsValue.isUndefined() && !versionsValue.isNull()) {
        if (!versionsValue.isArray())
            return Result<ListenTogetherPlaylist>::failure(endpointError(
                ErrorKind::Parse, decodeEndpoint(endpoint),
                versionsPath + QStringLiteral(": expected array"), versionsPath));
        const QJsonArray versions = versionsValue.toArray();
        playlist.versions.reserve(versions.size());
        for (qsizetype i = 0; i < versions.size(); ++i) {
            const QString entryPath = indexedPath(versionsPath, i);
            if (!versions.at(i).isObject())
                return Result<ListenTogetherPlaylist>::failure(endpointError(
                    ErrorKind::Parse, decodeEndpoint(endpoint),
                    entryPath + QStringLiteral(": expected object"), entryPath));
            const QJsonObject entry = versions.at(i).toObject();
            const auto userId = songIdValue(entry.value(QStringLiteral("userId")),
                                            fieldPath(entryPath, "userId"), endpoint);
            if (!userId)
                return Result<ListenTogetherPlaylist>::failure(userId.error);
            const auto version = songIdValue(entry.value(QStringLiteral("version")),
                                             fieldPath(entryPath, "version"), endpoint);
            if (!version)
                return Result<ListenTogetherPlaylist>::failure(version.error);
            playlist.versions.append({*userId.value, *version.value});
        }
    }

    const QString playModePath = fieldPath(path, "playMode");
    const QJsonValue playModeValue = object.value(QStringLiteral("playMode"));
    if (!playModeValue.isUndefined() && !playModeValue.isNull()) {
        if (!playModeValue.isString())
            return Result<ListenTogetherPlaylist>::failure(endpointError(
                ErrorKind::Parse, decodeEndpoint(endpoint),
                playModePath + QStringLiteral(": expected string"), playModePath));
        playlist.playMode = playModeValue.toString();
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

Result<QList<ListenTogetherPrivilege>> parsePrivileges(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenTogether.privileges");
    const auto data = dataObject(root, PrivilegePath, rootPath);
    if (!data)
        return Result<QList<ListenTogetherPrivilege>>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    const QJsonValue rowsValue = data.value->value(QStringLiteral("result"));
    if (rowsValue.isUndefined() || rowsValue.isNull())
        return Result<QList<ListenTogetherPrivilege>>::success({});
    if (!rowsValue.isArray())
        return itemTypeFailure<QList<ListenTogetherPrivilege>>(
            decodeEndpoint(PrivilegePath), fieldPath(dataPath, "result"));

    const QString rowsPath = fieldPath(dataPath, "result");
    const QJsonArray rows = rowsValue.toArray();
    QList<ListenTogetherPrivilege> privileges;
    privileges.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QString entryPath = indexedPath(rowsPath, i);
        if (!rows.at(i).isObject())
            return itemTypeFailure<QList<ListenTogetherPrivilege>>(
                decodeEndpoint(PrivilegePath), entryPath);
        const QJsonObject object = rows.at(i).toObject();
        ListenTogetherPrivilege privilege;

        const auto songId = songIdValue(object.value(QStringLiteral("songId")),
                                        fieldPath(entryPath, "songId"), PrivilegePath);
        if (!songId)
            return Result<QList<ListenTogetherPrivilege>>::failure(songId.error);
        privilege.songId = *songId.value;

        const QJsonValue playable = object.value(QStringLiteral("playable"));
        if (!playable.isBool())
            return Result<QList<ListenTogetherPrivilege>>::failure(endpointError(
                ErrorKind::Parse, decodeEndpoint(PrivilegePath),
                fieldPath(entryPath, "playable") + QStringLiteral(": expected boolean"),
                fieldPath(entryPath, "playable")));
        privilege.playable = playable.toBool();

        const QJsonValue unplayableType = object.value(QStringLiteral("unplayableType"));
        if (!unplayableType.isUndefined() && !unplayableType.isNull()) {
            if (!unplayableType.isString())
                return Result<QList<ListenTogetherPrivilege>>::failure(endpointError(
                    ErrorKind::Parse, decodeEndpoint(PrivilegePath),
                    fieldPath(entryPath, "unplayableType") + QStringLiteral(": expected string"),
                    fieldPath(entryPath, "unplayableType")));
            privilege.unplayableType = unplayableType.toString();
        }

        const auto userIds = idList(object.value(QStringLiteral("unplayableUserIds")),
                                    fieldPath(entryPath, "unplayableUserIds"), PrivilegePath);
        if (!userIds)
            return Result<QList<ListenTogetherPrivilege>>::failure(userIds.error);
        privilege.unplayableUserIds = *userIds.value;

        const auto outerIds = idList(object.value(QStringLiteral("unplayableOuterIds")),
                                     fieldPath(entryPath, "unplayableOuterIds"), PrivilegePath);
        if (!outerIds)
            return Result<QList<ListenTogetherPrivilege>>::failure(outerIds.error);
        privilege.unplayableOuterIds = *outerIds.value;

        privileges.append(std::move(privilege));
    }
    return Result<QList<ListenTogetherPrivilege>>::success(std::move(privileges));
}

Result<ListenTogetherStatistics> parseRelationStatistics(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenTogether.statistics");
    const auto data = dataObject(root, StatisticsPath, rootPath);
    if (!data)
        return Result<ListenTogetherStatistics>::failure(data.error);

    const QString dataPath = fieldPath(rootPath, "data");
    const QJsonObject payload = *data.value;
    ListenTogetherStatistics statistics;

    const auto readCounter = [&](const char *key, qint64 &out) -> std::optional<Error> {
        const QJsonValue value = payload.value(QLatin1String(key));
        if (value.isUndefined() || value.isNull())
            return std::nullopt;
        if (!value.isDouble())
            return endpointError(ErrorKind::Parse, decodeEndpoint(StatisticsPath),
                                 fieldPath(dataPath, key) + QStringLiteral(": expected number"),
                                 fieldPath(dataPath, key));
        out = static_cast<qint64>(value.toDouble());
        return std::nullopt;
    };

    if (const auto error = readCounter("listenCount", statistics.listenCount))
        return Result<ListenTogetherStatistics>::failure(*error);
    if (const auto error = readCounter("totalConnectionTime", statistics.totalConnectionTime))
        return Result<ListenTogetherStatistics>::failure(*error);
    return Result<ListenTogetherStatistics>::success(std::move(statistics));
}

Result<ListenTogetherResult> parseActionResult(const QJsonObject &root)
{
    ListenTogetherResult result;
    result.success = true;

    QString message;
    const QJsonValue dataValue = root.value(QStringLiteral("data"));
    if (dataValue.isObject()) {
        const QJsonValue inner = dataValue.toObject().value(QStringLiteral("message"));
        if (!inner.isUndefined() && !inner.isNull()) {
            if (!inner.isString())
                return Result<ListenTogetherResult>::failure(endpointError(
                    ErrorKind::Parse, decodeEndpoint(ResultPath),
                    QStringLiteral("listenTogether.action.data.message: expected string"),
                    QStringLiteral("listenTogether.action.data.message")));
            message = inner.toString();
        }
    } else if (!dataValue.isUndefined() && !dataValue.isNull()) {
        return Result<ListenTogetherResult>::failure(endpointError(
            ErrorKind::Parse, decodeEndpoint(ResultPath),
            QStringLiteral("listenTogether.action.data: expected object"),
            QStringLiteral("listenTogether.action.data")));
    }

    if (message.isEmpty()) {
        const QJsonValue rootMessage = root.value(QStringLiteral("message"));
        if (rootMessage.isString())
            message = rootMessage.toString();
        else if (!rootMessage.isUndefined() && !rootMessage.isNull())
            return Result<ListenTogetherResult>::failure(endpointError(
                ErrorKind::Parse, decodeEndpoint(ResultPath),
                QStringLiteral("listenTogether.action.message: expected string"),
                QStringLiteral("listenTogether.action.message")));
    }
    result.message = std::move(message);
    return Result<ListenTogetherResult>::success(std::move(result));
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

void fetchSyncPlaylist(NcmClient &client, QString roomId, QJsonObject playlistParam,
                       QObject *context, SyncCallback callback)
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
    if (!playlistParam.isEmpty()) {
        form.addQueryItem(QStringLiteral("playlistParam"),
                          QString::fromUtf8(QJsonDocument(playlistParam).toJson(QJsonDocument::Compact)));
    }
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
                     qint64 progressMs, QJsonArray playlistVersion, QObject *context,
                     ResultCallback callback)
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
    if (!playlistVersion.isEmpty()) {
        form.addQueryItem(QStringLiteral("playlistVersion"),
                          QString::fromUtf8(QJsonDocument(playlistVersion).toJson(QJsonDocument::Compact)));
    }
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
                    qint64 clientSeq, QObject *context, ResultCallback callback)
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
    form.addQueryItem(QStringLiteral("clientSeq"), QString::number(clientSeq));
    postFormJson(client, context, decodeEndpoint(SyncListCommandPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(resultFrom(*root.value, SyncListCommandPath));
    }, true);
}

void inviteUsers(NcmClient &client, QString roomId, QList<qint64> userIds,
                 QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(
            "api/listen/together/multi/invite", QStringLiteral("roomId must not be empty")));
        return;
    }

    QStringList ids;
    ids.reserve(userIds.size());
    for (qint64 userId : std::as_const(userIds)) {
        if (userId > 0)
            ids.append(QString::number(userId));
    }
    if (ids.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(
            "api/listen/together/multi/invite",
            QStringLiteral("at least one invited user is required")));
        return;
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("groupIds"), QStringLiteral("[]"));
    form.addQueryItem(QStringLiteral("inviteUids"),
                      QStringLiteral("[") + ids.join(u',') + QStringLiteral("]"));
    const QString csrf = client.cookieJar().value(QStringLiteral("__csrf"));
    if (!csrf.isEmpty())
        form.addQueryItem(QStringLiteral("csrf_token"), csrf);
    postFormJson(client, context, QStringLiteral("api/listen/together/multi/invite"),
                 std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        ListenTogetherResult result;
        result.success = true;
        // The invite endpoint reports failures through code/message; non-200
        // codes are already classified by postFormJson.
        result.message = root.value->value(QStringLiteral("message")).toString();
        callback(Result<ListenTogetherResult>::success(std::move(result)));
    }, true);
}

void sendInviteMessage(NcmClient &client, QString roomId, qint64 acceptorId,
                       QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(
            InviteMessagePath, QStringLiteral("roomId must not be empty")));
        return;
    }
    if (acceptorId <= 0) {
        callback(invalidRequest<ListenTogetherResult>(
            InviteMessagePath, QStringLiteral("acceptorId must be positive")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("acceptorId"), QString::number(acceptorId));
    const QString csrf = client.cookieJar().value(QStringLiteral("__csrf"));
    if (!csrf.isEmpty())
        form.addQueryItem(QStringLiteral("csrf_token"), csrf);
    postFormJson(client, context, decodeEndpoint(InviteMessagePath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(parseActionResult(*root.value));
    }, true);
}

void fetchPrivileges(NcmClient &client, qint64 otherUserId, QList<qint64> trackIds,
                     QObject *context, PrivilegeCallback callback)
{
    if (!callback)
        return;
    if (otherUserId <= 0) {
        callback(invalidRequest<QList<ListenTogetherPrivilege>>(
            PrivilegePath, QStringLiteral("otherUserId must be positive")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("otherUserId"), QString::number(otherUserId));
    if (!trackIds.isEmpty()) {
        QJsonArray ids;
        for (qint64 id : std::as_const(trackIds)) {
            if (id > 0)
                ids.append(QString::number(id));
        }
        if (!ids.isEmpty())
            form.addQueryItem(QStringLiteral("trackIds"),
                              QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)));
    }
    postFormJson(client, context, decodeEndpoint(PrivilegePath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<ListenTogetherPrivilege>>::failure(root.error));
            return;
        }
        callback(parsePrivileges(*root.value));
    }, true);
}

void fetchRelationStatistics(NcmClient &client, QString roomId, QList<qint64> userIds,
                             QObject *context, StatisticsCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherStatistics>(
            StatisticsPath, QStringLiteral("roomId must not be empty")));
        return;
    }
    QJsonArray ids;
    for (qint64 userId : std::as_const(userIds)) {
        if (userId > 0)
            ids.append(QString::number(userId));
    }
    if (ids.isEmpty()) {
        callback(invalidRequest<ListenTogetherStatistics>(
            StatisticsPath, QStringLiteral("at least one userId is required")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    form.addQueryItem(QStringLiteral("roomUserIds"),
                      QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)));
    postFormJson(client, context, decodeEndpoint(StatisticsPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherStatistics>::failure(root.error));
            return;
        }
        callback(parseRelationStatistics(*root.value));
    }, true);
}

void reportVipGift(NcmClient &client, QString sku, qint64 receiver, QString roomId,
                   QObject *context, ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(
            VipGiftPath, QStringLiteral("roomId must not be empty")));
        return;
    }
    if (sku.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(
            VipGiftPath, QStringLiteral("sku must not be empty")));
        return;
    }
    if (receiver <= 0) {
        callback(invalidRequest<ListenTogetherResult>(
            VipGiftPath, QStringLiteral("receiver must be positive")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("sku"), sku);
    form.addQueryItem(QStringLiteral("receiver"), QString::number(receiver));
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    postFormJson(client, context, decodeEndpoint(VipGiftPath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(parseActionResult(*root.value));
    }, true);
}

void notifyPlaylistChange(NcmClient &client, QString roomId, QObject *context,
                          ResultCallback callback)
{
    if (!callback)
        return;
    if (roomId.isEmpty()) {
        callback(invalidRequest<ListenTogetherResult>(
            SyncNoticePath, QStringLiteral("roomId must not be empty")));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("roomId"), roomId);
    postFormJson(client, context, decodeEndpoint(SyncNoticePath), std::move(form),
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ListenTogetherResult>::failure(root.error));
            return;
        }
        callback(parseActionResult(*root.value));
    }, true);
}

} // namespace ncm::sdk
