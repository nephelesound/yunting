#include "sdk/friend_listening.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"
#include "sdk/user_vip.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto FriendsPath = "api/listen/together/mutual/follows/get/v2";

QString firstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString() && !value.toString().isEmpty())
            return value.toString();
    }
    return {};
}

QString preferredPendant(const QJsonObject &pendant)
{
    return firstString(pendant, {"image", "imageAndroid", "imageIos"});
}

Result<FriendListening> friendFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    const QJsonObject userInfo = reader.object("userInfo");
    if (!reader.ok())
        return parseFailure<FriendListening>(FriendsPath, reader, path + QStringLiteral(".userInfo"));

    JsonReader userReader(userInfo, path + QStringLiteral(".userInfo"));
    FriendListeningUser user;
    user.userId = userReader.integer("userId");
    user.name = firstString(userInfo, {"customNickname", "snsNickname", "nickname"});
    user.avatar = userReader.string("avatarUrl");
    user.signature = userReader.string("signature");
    user.mutual = userReader.boolean("mutual");
    user.followed = userReader.boolean("followed");
    user.vipType = static_cast<int>(userReader.integer("vipType"));
    const QJsonObject avatarDetail = userReader.object("avatarDetail");
    user.identityIcon = avatarDetail.value(QStringLiteral("identityIconUrl")).toString();
    if (!userReader.ok())
        return parseFailure<FriendListening>(FriendsPath, userReader,
                                             path + QStringLiteral(".userInfo"));

    const QJsonObject pendant = reader.object("pendant");
    user.avatarPendant = preferredPendant(pendant);

    FriendListening friendItem;
    friendItem.user = std::move(user);
    friendItem.user.vipLevel = vipLevel(userInfo);
    friendItem.lastListen = reader.boolean("lastListen");
    friendItem.listenStatus = static_cast<int>(reader.integer("listenStatus"));
    friendItem.lastTime = reader.integer("lastTime");
    friendItem.online = reader.boolean("online");
    friendItem.inRoom = reader.boolean("inRoom");
    friendItem.description = reader.string("description");
    if (!reader.ok())
        return parseFailure<FriendListening>(FriendsPath, reader, path);
    return Result<FriendListening>::success(std::move(friendItem));
}

} // namespace

Result<FriendListeningPage> parseFriendListening(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("friendsListening");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<FriendListeningPage>(FriendsPath, rootReader,
                                                 rootPath + QStringLiteral(".data"));

    JsonReader reader(data, rootPath + QStringLiteral(".data"));
    const QJsonArray rows = reader.array("mutualFollowLists");
    if (!reader.ok())
        return parseFailure<FriendListeningPage>(FriendsPath, reader,
                                                 rootPath + QStringLiteral(".data.mutualFollowLists"));

    FriendListeningPage page;
    page.streakGlobalSwitch = static_cast<int>(reader.integer("streakGlobalSwitch"));
    page.streakUserSwitch = static_cast<int>(reader.integer("streakUserSwitch"));
    page.matchButtonText = reader.string("matchButtonText");
    if (!reader.ok())
        return parseFailure<FriendListeningPage>(FriendsPath, reader,
                                                 rootPath + QStringLiteral(".data"));

    const QString listPath = rootPath + QStringLiteral(".data.mutualFollowLists");
    page.items.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QString path = listPath + QLatin1Char('[') + QString::number(i) + QLatin1Char(']');
        if (!rows.at(i).isObject())
            return itemTypeFailure<FriendListeningPage>(FriendsPath, path);
        const auto friendItem = friendFrom(rows.at(i).toObject(), path);
        if (!friendItem)
            return Result<FriendListeningPage>::failure(friendItem.error);
        page.items.append(*friendItem.value);
    }
    return Result<FriendListeningPage>::success(std::move(page));
}

void fetchFriendListening(NcmClient &client, QObject *context,
                          FriendListeningCallback callback)
{
    if (!callback)
        return;
    postFormJson(client, context, QString::fromLatin1(FriendsPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<FriendListeningPage>::failure(root.error));
            return;
        }
        callback(parseFriendListening(*root.value));
    }, true);
}

} // namespace ncm::sdk
