#include "sdk/search_people.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonValue>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto CloudsearchPath = "api/cloudsearch/pc";
constexpr auto UsersRoot = "searchUsers";
constexpr auto RadiosRoot = "searchRadios";

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

Result<SearchUser> userFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SearchUser user;
    user.id = reader.integer("userId");
    user.name = reader.string("nickname");
    user.cover = reader.string("avatarUrl");
    user.followed = reader.boolean("followed");
    user.fans = reader.integer("followeds");
    user.follows = reader.integer("follows");
    user.playlists = reader.integer("playlistCount");
    user.signature = reader.string("signature");
    if (!reader.ok())
        return parseFailure<SearchUser>(QString::fromLatin1(CloudsearchPath), reader, path);
    return Result<SearchUser>::success(user);
}

Result<SearchRadio> radioFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SearchRadio radio;
    radio.id = reader.integer("id");
    radio.name = reader.string("name");
    radio.cover = reader.string("picUrl");
    radio.description = reader.string("desc");
    radio.programCount = reader.integer("programCount");
    radio.subCount = reader.integer("subCount");
    radio.playCount = reader.integer("playCount");
    if (!reader.ok())
        return parseFailure<SearchRadio>(QString::fromLatin1(CloudsearchPath), reader, path);

    const QString djPath = path + QStringLiteral(".dj");
    const QJsonValue djValue = object.value(QStringLiteral("dj"));
    if (!djValue.isUndefined() && !djValue.isNull() && !djValue.isObject()) {
        return Result<SearchRadio>::failure(endpointError(
            ErrorKind::Parse, QString::fromLatin1(CloudsearchPath),
            djPath + QStringLiteral(": expected object"), djPath));
    }
    JsonReader djReader(djValue.toObject(), djPath);
    radio.host = djReader.string("nickname");
    if (!djReader.ok())
        return parseFailure<SearchRadio>(QString::fromLatin1(CloudsearchPath), djReader, djPath);
    return Result<SearchRadio>::success(radio);
}

} // namespace

Result<SearchUsers> parseSearchUsers(const QJsonObject &root, QString query)
{
    const QString resultPath = QString::fromLatin1(UsersRoot) + QStringLiteral(".result");
    JsonReader rootReader(root, QString::fromLatin1(UsersRoot));
    const QJsonObject result = rootReader.object("result");
    if (!rootReader.ok())
        return parseFailure<SearchUsers>(QString::fromLatin1(CloudsearchPath), rootReader,
                                         resultPath);

    JsonReader resultReader(result, resultPath);
    const QJsonArray profiles = resultReader.array("userprofiles");
    if (!resultReader.ok())
        return parseFailure<SearchUsers>(QString::fromLatin1(CloudsearchPath), resultReader,
                                         resultPath + QStringLiteral(".userprofiles"));

    SearchUsers out;
    out.query = std::move(query);
    out.items.reserve(profiles.size());
    const QString profilesPath = resultPath + QStringLiteral(".userprofiles");
    for (qsizetype i = 0; i < profiles.size(); ++i) {
        const QString path = indexedPath(profilesPath, i);
        if (!profiles.at(i).isObject())
            return itemTypeFailure<SearchUsers>(QString::fromLatin1(CloudsearchPath), path);
        const auto parsed = userFrom(profiles.at(i).toObject(), path);
        if (!parsed)
            return Result<SearchUsers>::failure(parsed.error);
        out.items.append(*parsed.value);
    }
    return Result<SearchUsers>::success(std::move(out));
}

Result<SearchRadios> parseSearchRadios(const QJsonObject &root, QString query)
{
    const QString resultPath = QString::fromLatin1(RadiosRoot) + QStringLiteral(".result");
    JsonReader rootReader(root, QString::fromLatin1(RadiosRoot));
    const QJsonObject result = rootReader.object("result");
    if (!rootReader.ok())
        return parseFailure<SearchRadios>(QString::fromLatin1(CloudsearchPath), rootReader,
                                          resultPath);

    JsonReader resultReader(result, resultPath);
    const QJsonArray radios = resultReader.array("djRadios");
    if (!resultReader.ok())
        return parseFailure<SearchRadios>(QString::fromLatin1(CloudsearchPath), resultReader,
                                          resultPath + QStringLiteral(".djRadios"));

    SearchRadios out;
    out.query = std::move(query);
    out.items.reserve(radios.size());
    const QString radiosPath = resultPath + QStringLiteral(".djRadios");
    for (qsizetype i = 0; i < radios.size(); ++i) {
        const QString path = indexedPath(radiosPath, i);
        if (!radios.at(i).isObject())
            return itemTypeFailure<SearchRadios>(QString::fromLatin1(CloudsearchPath), path);
        const auto parsed = radioFrom(radios.at(i).toObject(), path);
        if (!parsed)
            return Result<SearchRadios>::failure(parsed.error);
        out.items.append(*parsed.value);
    }
    return Result<SearchRadios>::success(std::move(out));
}

} // namespace ncm::sdk
