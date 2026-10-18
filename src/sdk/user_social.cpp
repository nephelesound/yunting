#include "sdk/user_social.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonValue>

namespace ncm::sdk {
namespace {

constexpr auto FollowsPath = "api/user/getfollows/";
constexpr auto FollowedsPath = "api/user/getfolloweds/";
constexpr auto LevelPath = "api/user/level";
constexpr auto SubcountPath = "api/subcount";

QString fieldPath(const QString &objectPath, const char *key)
{
    return objectPath + QLatin1Char('.') + QLatin1String(key);
}

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

int readCount(JsonReader &reader, const QString &objectPath, const char *key,
              QString *badPath)
{
    const int value = static_cast<int>(reader.integer(key));
    if (!reader.ok() && badPath->isEmpty())
        *badPath = fieldPath(objectPath, key);
    return value;
}

double readNumber(JsonReader &reader, const QString &objectPath, const char *key,
                  QString *badPath)
{
    const double value = reader.number(key);
    if (!reader.ok() && badPath->isEmpty())
        *badPath = fieldPath(objectPath, key);
    return value;
}

QString readString(JsonReader &reader, const QString &objectPath, const char *key,
                   QString *badPath)
{
    const QString value = reader.string(key);
    if (!reader.ok() && badPath->isEmpty())
        *badPath = fieldPath(objectPath, key);
    return value;
}

Result<SocialUser> userFrom(const QJsonObject &object, const QString &path,
                            const char *endpoint)
{
    JsonReader reader(object, path);
    SocialUser user;
    user.id = reader.integer("userId");
    user.name = reader.string("nickname");
    user.avatar = reader.string("avatarUrl");
    user.signature = reader.string("signature");
    user.gender = static_cast<int>(reader.integer("gender"));
    user.followed = reader.boolean("followed");
    user.mutual = reader.boolean("mutual");
    user.vipType = static_cast<int>(reader.integer("vipType"));
    if (!reader.ok())
        return parseFailure<SocialUser>(QString::fromLatin1(endpoint), reader, path);
    return Result<SocialUser>::success(user);
}

Result<FollowPage> parseFollowPage(const QJsonObject &root, const char *listKey,
                                   const char *endpoint, const QString &rootPath)
{
    JsonReader reader(root, rootPath);
    const QJsonArray rows = reader.array(listKey);
    const QString listPath = fieldPath(rootPath, listKey);
    if (!reader.ok())
        return parseFailure<FollowPage>(QString::fromLatin1(endpoint), reader, listPath);

    FollowPage page;
    page.more = reader.boolean("more");
    if (!reader.ok())
        return parseFailure<FollowPage>(QString::fromLatin1(endpoint), reader,
                                        fieldPath(rootPath, "more"));

    page.items.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QString itemPath = indexedPath(listPath, i);
        if (!rows.at(i).isObject())
            return itemTypeFailure<FollowPage>(QString::fromLatin1(endpoint), itemPath);
        const auto parsed = userFrom(rows.at(i).toObject(), itemPath, endpoint);
        if (!parsed)
            return Result<FollowPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<FollowPage>::success(page);
}

} // namespace

Result<FollowPage> parseUserFollows(const QJsonObject &root)
{
    return parseFollowPage(root, "follow", FollowsPath, QStringLiteral("userFollows"));
}

Result<FollowPage> parseUserFolloweds(const QJsonObject &root)
{
    return parseFollowPage(root, "followeds", FollowedsPath,
                           QStringLiteral("userFolloweds"));
}

Result<UserLevel> parseUserLevel(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("userLevel");
    const QString dataPath = fieldPath(rootPath, "data");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<UserLevel>(QString::fromLatin1(LevelPath), rootReader, dataPath);
    // The current Store treats missing, null and empty data alike: there is no
    // level to display, so the request failed even though the envelope was 200.
    if (data.isEmpty())
        return missingFailure<UserLevel>(QString::fromLatin1(LevelPath), dataPath,
                                         QStringLiteral("object"));

    JsonReader reader(data, dataPath);
    UserLevel level;
    QString badPath;
    level.level = readCount(reader, dataPath, "level", &badPath);
    level.nowPlayCount = readCount(reader, dataPath, "nowPlayCount", &badPath);
    level.nextPlayCount = readCount(reader, dataPath, "nextPlayCount", &badPath);
    level.nowLoginCount = readCount(reader, dataPath, "nowLoginCount", &badPath);
    level.nextLoginCount = readCount(reader, dataPath, "nextLoginCount", &badPath);
    level.progress = readNumber(reader, dataPath, "progress", &badPath);
    level.info = readString(reader, dataPath, "info", &badPath);
    if (!reader.ok())
        return parseFailure<UserLevel>(QString::fromLatin1(LevelPath), reader, badPath);
    return Result<UserLevel>::success(level);
}

Result<UserSubcount> parseUserSubcount(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("userSubcount");
    JsonReader reader(root, rootPath);
    UserSubcount counts;
    QString badPath;
    counts.artistCount = readCount(reader, rootPath, "artistCount", &badPath);
    counts.createdPlaylistCount =
        readCount(reader, rootPath, "createdPlaylistCount", &badPath);
    counts.subPlaylistCount = readCount(reader, rootPath, "subPlaylistCount", &badPath);
    counts.mvCount = readCount(reader, rootPath, "mvCount", &badPath);
    counts.djRadioCount = readCount(reader, rootPath, "djRadioCount", &badPath);
    if (!reader.ok())
        return parseFailure<UserSubcount>(QString::fromLatin1(SubcountPath), reader, badPath);
    return Result<UserSubcount>::success(counts);
}

} // namespace ncm::sdk
