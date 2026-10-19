#include "sdk/social.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <algorithm>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto EventsPath = "api/event/get/";

QString eventJsonText(const QJsonObject &event, const char *key)
{
    const QString raw = event.value(QStringLiteral("json")).toString();
    if (raw.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject())
        return {};
    return doc.object().value(QLatin1String(key)).toString();
}

QString eventJsonCover(const QJsonObject &event)
{
    const QString raw = event.value(QStringLiteral("json")).toString();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject())
        return {};
    const QJsonObject resource = doc.object().value(QStringLiteral("resource")).toObject();
    for (const char *container : {"mlogBaseData", "song", "album", "playlist", "mv"}) {
        const QJsonObject object = resource.value(QLatin1String(container)).toObject();
        for (const char *key : {"coverUrl", "picUrl", "imgUrl", "coverImgUrl"}) {
            const QString value = object.value(QLatin1String(key)).toString();
            if (!value.isEmpty())
                return value;
        }
    }
    return {};
}

Result<UserEvent> eventFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    UserEvent event;
    event.id = reader.integer("id");
    event.actionName = reader.string("actName");
    event.eventTime = reader.integer("eventTime");

    const QJsonObject user = reader.object("user");
    JsonReader userReader(user, path + QStringLiteral(".user"));
    event.user.id = userReader.integer("userId");
    event.user.name = userReader.string("nickname");
    event.user.avatar = userReader.string("avatarUrl");
    event.user.pendant =
        userReader.object("pendantData").value(QStringLiteral("imageUrl")).toString();
    event.user.identityIcon =
        userReader.object("avatarDetail").value(QStringLiteral("identityIconUrl")).toString();

    const QJsonObject info = reader.object("info");
    JsonReader infoReader(info, path + QStringLiteral(".info"));
    event.title = infoReader.string("title");
    event.description = infoReader.string("description");
    event.likeCount = infoReader.integer("likedCount");
    event.commentCount = infoReader.integer("commentCount");
    event.shareCount = infoReader.integer("shareCount");

    const qint64 infoResourceId = infoReader.integer("resourceId");
    const int infoResourceType = static_cast<int>(infoReader.integer("resourceType"));
    const QJsonObject thread = infoReader.object("commentThread");
    JsonReader threadReader(thread, path + QStringLiteral(".info.commentThread"));
    const QJsonObject resource = threadReader.object("resourceInfo");
    JsonReader resourceReader(resource, path + QStringLiteral(".info.commentThread.resourceInfo"));
    event.resource.id = resourceReader.integer("id");
    event.resource.name = resourceReader.string("name");
    event.resource.cover = resourceReader.string("imgUrl");
    event.resource.webUrl = resourceReader.string("webUrl");
    event.resource.eventType = static_cast<int>(resourceReader.integer("eventType"));
    event.resource.resourceType = static_cast<int>(threadReader.integer("resourceType"));
    // Many live events (especially mlog/song posts) have no resourceInfo. Keep the
    // renderable fallback rather than dropping the event.
    if (event.resource.id == 0)
        event.resource.id = infoResourceId;
    if (event.resource.resourceType == 0)
        event.resource.resourceType = infoResourceType;
    if (event.description.isEmpty())
        event.description = eventJsonText(object, "msg");
    if (event.title.isEmpty())
        event.title = eventJsonText(object, "title");
    if (event.resource.cover.isEmpty())
        event.resource.cover = eventJsonCover(object);
    if (event.title.isEmpty())
        event.title = event.resource.name;
    if (event.title.isEmpty())
        event.title = event.actionName;
    if (!reader.ok())
        return Result<UserEvent>::failure(
            endpointError(ErrorKind::Parse, EventsPath, reader.error(), path));
    if (!userReader.ok())
        return Result<UserEvent>::failure(
            endpointError(ErrorKind::Parse, EventsPath, userReader.error(), path));
    if (!infoReader.ok())
        return Result<UserEvent>::failure(
            endpointError(ErrorKind::Parse, EventsPath, infoReader.error(), path));
    if (!threadReader.ok())
        return Result<UserEvent>::failure(
            endpointError(ErrorKind::Parse, EventsPath, threadReader.error(), path));
    if (!resourceReader.ok())
        return Result<UserEvent>::failure(
            endpointError(ErrorKind::Parse, EventsPath, resourceReader.error(), path));
    return Result<UserEvent>::success(event);
}

} // namespace

Result<EventPage> parseUserEvents(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("social.events"));
    const QJsonArray items = reader.array("events");
    if (!reader.ok())
        return Result<EventPage>::failure(
            endpointError(ErrorKind::Parse, EventsPath, reader.error(), QStringLiteral("social.events")));
    if (!reader.has("events"))
        return Result<EventPage>::failure(
            endpointError(ErrorKind::Parse, EventsPath,
                          QStringLiteral("social.events.events: missing array"),
                          QStringLiteral("social.events.events")));

    EventPage page;
    page.more = reader.boolean("more");
    page.size = reader.integer("size");
    page.lasttime = reader.integer("lasttime");
    if (!reader.ok())
        return Result<EventPage>::failure(
            endpointError(ErrorKind::Parse, EventsPath, reader.error(),
                          QStringLiteral("social.events")));
    for (qsizetype i = 0; i < items.size(); ++i) {
        const auto parsed = eventFrom(items.at(i).toObject(),
                                      QStringLiteral("social.events.events[%1]").arg(i));
        if (!parsed)
            return Result<EventPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<EventPage>::success(page);
}

void fetchUserEvents(NcmClient &client, qint64 uid, qint64 lasttime, int limit,
                     bool personal, QObject *context, UserEventsCallback callback)
{
    if (uid <= 0) {
        if (callback)
            callback(Result<EventPage>::failure(
                endpointError(ErrorKind::InvalidRequest, QString::fromLatin1(EventsPath),
                              QStringLiteral("uid must be positive"))));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("getcounts"), QStringLiteral("true"));
    // API treats 0 as "no events"; -1 is the initial cursor (same as reference default).
    form.addQueryItem(QStringLiteral("time"), QString::number(lasttime <= 0 ? -1 : lasttime));
    form.addQueryItem(QStringLiteral("limit"), QString::number(std::max(1, limit)));
    form.addQueryItem(QStringLiteral("total"), QStringLiteral("false"));
    form.addQueryItem(QStringLiteral("fromRN"), QStringLiteral("true"));
    const QString path = QString::fromLatin1(EventsPath) + QString::number(uid);
    postFormJson(client, context, path, form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<EventPage>::failure(root.error));
            return;
        }
        callback(parseUserEvents(*root.value));
    }, personal);
}

void setUserFollowed(NcmClient &client, qint64 userId, bool followed, QObject *context,
                     UserFollowCallback callback)
{
    if (userId <= 0) {
        if (callback)
            callback(Result<UserFollowState>::failure(
                endpointError(ErrorKind::InvalidRequest, QStringLiteral("user/follow"),
                              QStringLiteral("userId must be positive"))));
        return;
    }
    // NcmClient::postWeapi prefixes /weapi/; use the transport-relative path.
    const QString endpoint = (followed ? QStringLiteral("user/follow/")
                                       : QStringLiteral("user/delfollow/"))
        + QString::number(userId);
    postWeapiJson(client, context, endpoint, {}, true,
                  [callback = std::move(callback), userId, followed]
                  (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<UserFollowState>::failure(root.error));
            return;
        }
        callback(Result<UserFollowState>::success(UserFollowState{userId, followed}));
    });
}

} // namespace ncm::sdk
