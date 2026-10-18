#include "sdk/notification.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <utility>

namespace ncm::sdk {
namespace {

// Error::endpoint carries these weapi-relative paths; the shared transport
// prepends "weapi/". The reference modules' "/api/..." prefix is stripped when
// they build their weapi URL (request.js: uri.substr(5)) and must not be passed
// to NcmClient::postWeapi, which would otherwise request /weapi/api/....
constexpr auto SystemNoticesPath = "msg/notices";
constexpr auto CommentNotificationsPath = "v1/user/comments/";
constexpr auto ForwardNotificationsPath = "forwards/get";

// Notification ids are strings in captured notice responses but numbers in
// other recordings; forward video ids are also sometimes non-numeric strings.
// JsonReader intentionally rejects mixed shapes, so identity fields get this
// narrow dual-shape read. Non-numeric values become 0 rather than failing an
// otherwise renderable row.
qint64 numericId(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    if (value.isString()) {
        bool ok = false;
        const qint64 id = value.toString().toLongLong(&ok);
        return ok ? id : 0;
    }
    return 0;
}

QString optionalString(const QJsonObject &object, const char *key)
{
    // Narrow helper for optional fields whose observed shape is nullable or
    // mixed: expression URLs (Object in the reference comment model) and the
    // embedded payload strings. Treat a non-string shape as absent instead of
    // failing the whole notification row.
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString();
}

bool decodeObject(const QString &encoded, QJsonObject *object)
{
    if (encoded.isEmpty())
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(encoded.toUtf8());
    if (!document.isObject())
        return false;
    *object = document.object();
    return true;
}

NotificationUser userFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    NotificationUser user;
    user.id = numericId(object, "userId");
    user.name = reader.string("nickname");
    user.avatar = reader.string("avatarUrl");
    return user;
}

SystemNoticePayload noticePayloadFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SystemNoticePayload payload;
    payload.type = static_cast<int>(reader.integer("type"));
    payload.user = userFrom(reader.object("user"), path + QStringLiteral(".user"));

    const QJsonObject comment = reader.object("comment");
    JsonReader commentReader(comment, path + QStringLiteral(".comment"));
    payload.commentId = numericId(comment, "commentId");
    payload.commentContent = commentReader.string("content");
    payload.commentTime = commentReader.integer("time");
    payload.commentParentId = numericId(comment, "parentCommentId");

    const QJsonObject playlist = reader.object("playlist");
    JsonReader playlistReader(playlist, path + QStringLiteral(".playlist"));
    payload.playlistId = numericId(playlist, "id");
    payload.playlistName = playlistReader.string("name");
    payload.playlistCover = playlistReader.string("coverImgUrl");

    const QJsonObject general = reader.object("generalNotice");
    JsonReader generalReader(general, path + QStringLiteral(".generalNotice"));
    payload.actionDesc = generalReader.string("actionDesc");
    payload.content = generalReader.string("content");
    payload.webUrl = generalReader.string("webUrl");
    payload.nativeUrl = generalReader.string("nativeUrl");
    payload.generalType = static_cast<int>(numericId(general, "type"));
    payload.resourceId = numericId(general, "resourceId");
    payload.specialResourceType =
        static_cast<int>(numericId(general, "specialResourceType"));
    // Every field above is optional; embedded payload type errors are ignored.
    return payload;
}

Result<SystemNotice> systemNoticeFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SystemNotice notice;
    notice.queued = reader.boolean("queued");
    notice.id = numericId(object, "id");
    notice.userId = numericId(object, "userId");
    notice.time = reader.integer("time");
    notice.type = static_cast<int>(reader.integer("type"));
    if (!reader.ok())
        return parseFailure<SystemNotice>(SystemNoticesPath, reader, path);
    // A non-string `notice` is treated like malformed embedded JSON so the
    // outer row remains renderable.
    const QString encoded = optionalString(object, "notice");

    QJsonObject payload;
    if (decodeObject(encoded, &payload)) {
        notice.payloadValid = true;
        notice.payload = noticePayloadFrom(payload, path + QStringLiteral(".notice"));
    }
    return Result<SystemNotice>::success(notice);
}

Result<CommentNotification> commentNotificationFrom(const QJsonObject &object,
                                                    const QString &path)
{
    JsonReader reader(object, path);
    CommentNotification notification;
    notification.user = userFrom(reader.object("user"), path + QStringLiteral(".user"));
    notification.repliedUser =
        userFrom(reader.object("beRepliedUser"), path + QStringLiteral(".beRepliedUser"));
    notification.repliedContent = reader.string("beRepliedContent");
    notification.content = reader.string("content");
    notification.time = reader.integer("time");
    notification.parentCommentId = numericId(object, "parentCommentId");
    notification.resourceType = static_cast<int>(numericId(object, "resourceType"));
    notification.commentLocationType =
        static_cast<int>(numericId(object, "commentLocationType"));
    notification.combinedId = reader.string("combindId");
    notification.expressionUrl = optionalString(object, "expressionUrl");
    notification.repliedExpressionUrl = optionalString(object, "beRepliedExpressionUrl");
    if (!reader.ok())
        return parseFailure<CommentNotification>(CommentNotificationsPath, reader, path);
    return Result<CommentNotification>::success(notification);
}

struct ForwardContent {
    QString kind;
    qint64 id = 0;
    QString title;
    QString cover;
    QString artist;
};

ForwardContent directContentFrom(const QJsonObject &object, const QString &path)
{
    ForwardContent content;

    const QJsonObject song = object.value(QStringLiteral("song")).toObject();
    if (!song.isEmpty()) {
        JsonReader songReader(song, path + QStringLiteral(".song"));
        content.kind = QStringLiteral("song");
        content.id = numericId(song, "id");
        content.title = songReader.string("name");
        JsonReader albumReader(songReader.object("album"),
                               path + QStringLiteral(".song.album"));
        content.cover = albumReader.string("picUrl");
        const QStringList artists = objectNameList(song.value(QStringLiteral("artists")).toArray());
        if (!artists.isEmpty())
            content.artist = artists.first();
        return content;
    }

    const QJsonObject playlist = object.value(QStringLiteral("playlist")).toObject();
    if (!playlist.isEmpty()) {
        JsonReader playlistReader(playlist, path + QStringLiteral(".playlist"));
        content.kind = QStringLiteral("playlist");
        content.id = numericId(playlist, "id");
        content.title = playlistReader.string("name");
        content.cover = playlistReader.string("coverImgUrl");
        JsonReader creatorReader(playlistReader.object("creator"),
                                 path + QStringLiteral(".playlist.creator"));
        content.artist = creatorReader.string("nickname");
        return content;
    }

    const QJsonObject album = object.value(QStringLiteral("album")).toObject();
    if (!album.isEmpty()) {
        JsonReader albumReader(album, path + QStringLiteral(".album"));
        content.kind = QStringLiteral("album");
        content.id = numericId(album, "id");
        content.title = albumReader.string("name");
        content.cover = albumReader.string("picUrl");
        JsonReader artistReader(albumReader.object("artist"),
                                path + QStringLiteral(".album.artist"));
        content.artist = artistReader.string("name");
        return content;
    }

    const QJsonObject program = object.value(QStringLiteral("program")).toObject();
    if (!program.isEmpty()) {
        JsonReader programReader(program, path + QStringLiteral(".program"));
        content.kind = QStringLiteral("program");
        content.id = numericId(program, "id");
        content.title = programReader.string("name");
        content.cover = programReader.string("coverUrl");
        JsonReader radioReader(programReader.object("radio"),
                               path + QStringLiteral(".program.radio"));
        content.artist = radioReader.string("name");
        return content;
    }

    const QJsonObject video = object.value(QStringLiteral("video")).toObject();
    if (!video.isEmpty()) {
        JsonReader videoReader(video, path + QStringLiteral(".video"));
        content.kind = QStringLiteral("video");
        content.id = numericId(video, "videoId");
        if (content.id == 0)
            content.id = numericId(video, "vid");
        content.title = videoReader.string("title");
        content.cover = videoReader.string("coverUrl");
        JsonReader creatorReader(videoReader.object("creator"),
                                 path + QStringLiteral(".video.creator"));
        content.artist = creatorReader.string("nickname");
        return content;
    }

    return content;
}

ForwardContent eventContentFrom(const QJsonObject &event, const QString &path,
                                QString *eventMessage)
{
    QJsonObject shared;
    if (decodeObject(event.value(QStringLiteral("json")).toString(), &shared)) {
        JsonReader sharedReader(shared, path + QStringLiteral(".json"));
        if (eventMessage && eventMessage->isEmpty())
            *eventMessage = sharedReader.string("msg");
        const ForwardContent content = directContentFrom(shared, path + QStringLiteral(".json"));
        if (!content.kind.isEmpty())
            return content;
    }

    ForwardContent content;
    content.kind = QStringLiteral("event");
    content.id = numericId(event, "id");
    JsonReader eventReader(event, path);
    const QJsonObject info = eventReader.object("info");
    JsonReader infoReader(info, path + QStringLiteral(".info"));
    content.title = infoReader.string("title");
    if (content.title.isEmpty())
        content.title = infoReader.string("resourceTitle");
    const QJsonObject thread = infoReader.object("commentThread");
    JsonReader threadReader(thread, path + QStringLiteral(".info.commentThread"));
    const QJsonObject resourceInfo = threadReader.object("resourceInfo");
    JsonReader resourceReader(resourceInfo,
                              path + QStringLiteral(".info.commentThread.resourceInfo"));
    if (content.title.isEmpty())
        content.title = resourceReader.string("name");
    content.cover = resourceReader.string("imgUrl");
    JsonReader userReader(eventReader.object("user"), path + QStringLiteral(".user"));
    content.artist = userReader.string("nickname");
    return content;
}

void assignForwardContent(ForwardNotification &notification, const ForwardContent &content)
{
    notification.resourceKind = content.kind;
    notification.resourceId = content.id;
    notification.resourceTitle = content.title;
    notification.resourceCover = content.cover;
    notification.resourceArtist = content.artist;
}

Result<ForwardNotification> forwardNotificationFrom(const QJsonObject &object,
                                                    const QString &path)
{
    JsonReader reader(object, path);
    ForwardNotification notification;
    notification.id = numericId(object, "id");
    notification.userId = numericId(object, "userId");
    notification.time = reader.integer("time");
    notification.type = static_cast<int>(reader.integer("type"));
    if (!reader.ok())
        return parseFailure<ForwardNotification>(ForwardNotificationsPath, reader, path);
    // A non-string `json` is treated like malformed embedded JSON so the outer
    // row remains renderable.
    const QString encoded = optionalString(object, "json");

    QJsonObject outer;
    if (!decodeObject(encoded, &outer))
        return Result<ForwardNotification>::success(notification);
    notification.payloadValid = true;

    JsonReader outerReader(outer, path + QStringLiteral(".json"));
    notification.user =
        userFrom(outerReader.object("user"), path + QStringLiteral(".json.user"));
    notification.eventType = static_cast<int>(outerReader.integer("type"));

    QJsonObject shared;
    if (!decodeObject(outer.value(QStringLiteral("json")).toString(), &shared))
        return Result<ForwardNotification>::success(notification);

    JsonReader sharedReader(shared, path + QStringLiteral(".json.json"));
    notification.message = sharedReader.string("msg");

    const QJsonObject event = shared.value(QStringLiteral("event")).toObject();
    if (!event.isEmpty()) {
        QString eventMessage;
        const ForwardContent content =
            eventContentFrom(event, path + QStringLiteral(".json.json.event"), &eventMessage);
        if (notification.message.isEmpty())
            notification.message = eventMessage;
        if (notification.user.id == 0 && notification.user.name.isEmpty()
            && notification.user.avatar.isEmpty()) {
            notification.user =
                userFrom(event.value(QStringLiteral("user")).toObject(),
                         path + QStringLiteral(".json.json.event.user"));
        }
        if (notification.eventType == 0) {
            JsonReader eventReader(event, path + QStringLiteral(".json.json.event"));
            notification.eventType = static_cast<int>(eventReader.integer("type"));
        }
        if (!content.kind.isEmpty())
            assignForwardContent(notification, content);
    }

    if (notification.resourceKind.isEmpty()) {
        const ForwardContent content =
            directContentFrom(shared, path + QStringLiteral(".json.json"));
        if (!content.kind.isEmpty())
            assignForwardContent(notification, content);
    }
    return Result<ForwardNotification>::success(notification);
}

} // namespace

Result<SystemNoticePage> parseSystemNotices(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("notification.notices"));
    const QJsonArray items = reader.array("notices");
    if (!reader.ok())
        return parseFailure<SystemNoticePage>(
            SystemNoticesPath, reader, QStringLiteral("notification.notices.notices"));
    if (!reader.has("notices"))
        return missingFailure<SystemNoticePage>(
            SystemNoticesPath, QStringLiteral("notification.notices.notices"));

    SystemNoticePage page;
    page.more = reader.boolean("more");
    if (!reader.ok())
        return parseFailure<SystemNoticePage>(SystemNoticesPath, reader, reader.path());

    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path =
            QStringLiteral("notification.notices.notices[%1]").arg(i);
        if (!items.at(i).isObject())
            return itemTypeFailure<SystemNoticePage>(SystemNoticesPath, path);
        const auto parsed = systemNoticeFrom(items.at(i).toObject(), path);
        if (!parsed)
            return Result<SystemNoticePage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    if (!page.items.isEmpty())
        page.nextTime = page.items.last().time;
    return Result<SystemNoticePage>::success(page);
}

Result<CommentNotificationPage> parseCommentNotifications(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("notification.comments"));
    const QJsonArray items = reader.array("comments");
    if (!reader.ok())
        return parseFailure<CommentNotificationPage>(
            CommentNotificationsPath, reader,
            QStringLiteral("notification.comments.comments"));
    if (!reader.has("comments"))
        return missingFailure<CommentNotificationPage>(
            CommentNotificationsPath, QStringLiteral("notification.comments.comments"));

    CommentNotificationPage page;
    page.more = reader.boolean("more");
    page.total = reader.integer("total");
    if (!reader.ok())
        return parseFailure<CommentNotificationPage>(
            CommentNotificationsPath, reader, reader.path());

    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path =
            QStringLiteral("notification.comments.comments[%1]").arg(i);
        if (!items.at(i).isObject())
            return itemTypeFailure<CommentNotificationPage>(CommentNotificationsPath, path);
        const auto parsed = commentNotificationFrom(items.at(i).toObject(), path);
        if (!parsed)
            return Result<CommentNotificationPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    if (!page.items.isEmpty())
        page.nextBeforeTime = page.items.last().time;
    return Result<CommentNotificationPage>::success(page);
}

Result<ForwardNotificationPage> parseForwardNotifications(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("notification.forwards"));
    const QJsonArray items = reader.array("forwards");
    if (!reader.ok())
        return parseFailure<ForwardNotificationPage>(
            ForwardNotificationsPath, reader,
            QStringLiteral("notification.forwards.forwards"));
    if (!reader.has("forwards"))
        return missingFailure<ForwardNotificationPage>(
            ForwardNotificationsPath, QStringLiteral("notification.forwards.forwards"));

    ForwardNotificationPage page;
    page.more = reader.boolean("more");
    page.newCount = static_cast<int>(reader.integer("newCount"));
    page.lasttime = reader.integer("lasttime");
    if (!reader.ok())
        return parseFailure<ForwardNotificationPage>(
            ForwardNotificationsPath, reader, reader.path());

    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path =
            QStringLiteral("notification.forwards.forwards[%1]").arg(i);
        if (!items.at(i).isObject())
            return itemTypeFailure<ForwardNotificationPage>(ForwardNotificationsPath, path);
        const auto parsed = forwardNotificationFrom(items.at(i).toObject(), path);
        if (!parsed)
            return Result<ForwardNotificationPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<ForwardNotificationPage>::success(page);
}

void fetchSystemNotices(NcmClient &client, int limit, qint64 lasttime,
                        QObject *context, SystemNoticesCallback callback)
{
    if (!callback)
        return;
    const qint64 cursor = lasttime <= 0 ? -1 : lasttime;
    const QJsonObject payload{
        {"limit", limit > 0 ? limit : 30},
        {"time", cursor},
    };
    postWeapiJson(client, context, QString::fromLatin1(SystemNoticesPath), payload, true,
                  [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<SystemNoticePage>::failure(root.error));
            return;
        }
        callback(parseSystemNotices(*root.value));
    });
}

void fetchCommentNotifications(NcmClient &client, qint64 uid, int limit,
                               qint64 beforeTime, QObject *context,
                               CommentNotificationsCallback callback)
{
    if (!callback)
        return;
    if (uid <= 0) {
        callback(Result<CommentNotificationPage>::failure(
            endpointError(ErrorKind::InvalidRequest, QString::fromLatin1(CommentNotificationsPath),
                          QStringLiteral("uid must be positive"))));
        return;
    }
    const QString endpoint =
        QString::fromLatin1(CommentNotificationsPath) + QString::number(uid);
    const qint64 before = beforeTime <= 0 ? -1 : beforeTime;
    const QJsonObject payload{
        {"beforeTime", QString::number(before)},
        {"limit", limit > 0 ? limit : 30},
        {"total", QStringLiteral("true")},
        {"uid", uid},
    };
    postWeapiJson(client, context, endpoint, payload, true,
                  [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<CommentNotificationPage>::failure(root.error));
            return;
        }
        callback(parseCommentNotifications(*root.value));
    });
}

void fetchForwardNotifications(NcmClient &client, int limit, int offset,
                               QObject *context, ForwardNotificationsCallback callback)
{
    if (!callback)
        return;
    const int requestedOffset = std::max(0, offset);
    const QJsonObject payload{
        {"offset", requestedOffset},
        {"limit", limit > 0 ? limit : 30},
        {"total", QStringLiteral("true")},
    };
    postWeapiJson(client, context, QString::fromLatin1(ForwardNotificationsPath), payload, true,
                  [callback = std::move(callback), requestedOffset]
                  (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<ForwardNotificationPage>::failure(root.error));
            return;
        }
        auto page = parseForwardNotifications(*root.value);
        if (page)
            page.value->offset = requestedOffset;
        callback(std::move(page));
    });
}

} // namespace ncm::sdk
