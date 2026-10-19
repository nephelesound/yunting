#include "sdk/listen_data.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonValue>

namespace ncm::sdk {
namespace {

constexpr auto TotalPath = "api/content/activity/listen/data/total";
constexpr auto RealtimePath = "api/content/activity/listen/data/realtime/report";
constexpr auto RankPath = "api/content/activity/listen/data/song/play/rank";

QString fieldPath(const QString &objectPath, const char *key)
{
    return objectPath + QLatin1Char('.') + QLatin1String(key);
}

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

qint64 readInteger(JsonReader &reader, const QString &objectPath, const char *key,
                   QString *badPath)
{
    const qint64 value = reader.integer(key);
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

QJsonObject readObject(JsonReader &reader, const QString &objectPath, const char *key,
                       QString *badPath)
{
    const QJsonObject value = reader.object(key);
    if (!reader.ok() && badPath->isEmpty())
        *badPath = fieldPath(objectPath, key);
    return value;
}

} // namespace

Result<ListenTotal> parseListenTotal(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenTotal");
    const QString dataPath = fieldPath(rootPath, "data");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<ListenTotal>(QString::fromLatin1(TotalPath), rootReader, dataPath);
    if (data.isEmpty())
        return missingFailure<ListenTotal>(QString::fromLatin1(TotalPath), dataPath,
                                           QStringLiteral("object"));

    JsonReader reader(data, dataPath);
    ListenTotal total;
    QString badPath;
    total.seconds = readInteger(reader, dataPath, "totalDuration", &badPath);
    if (!reader.ok())
        return parseFailure<ListenTotal>(QString::fromLatin1(TotalPath), reader, badPath);
    return Result<ListenTotal>::success(total);
}

Result<ListenRealtime> parseListenRealtime(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenRealtime");
    const QString dataPath = fieldPath(rootPath, "data");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<ListenRealtime>(QString::fromLatin1(RealtimePath), rootReader,
                                            dataPath);
    if (data.isEmpty())
        return missingFailure<ListenRealtime>(QString::fromLatin1(RealtimePath), dataPath,
                                              QStringLiteral("object"));

    JsonReader reader(data, dataPath);
    ListenRealtime realtime;
    QString badPath;
    realtime.startTime = readInteger(reader, dataPath, "startTime", &badPath);
    realtime.endTime = readInteger(reader, dataPath, "endTime", &badPath);
    realtime.userNickName = readString(reader, dataPath, "userNickName", &badPath);
    realtime.listenTimeDistribution =
        readObject(reader, dataPath, "listenTimeDistributionBlock", &badPath);
    realtime.weekFriends = readObject(reader, dataPath, "weekFriendsListenBlock", &badPath);
    realtime.weekToday = readObject(reader, dataPath, "weekTodayListenBlock", &badPath);
    if (!reader.ok())
        return parseFailure<ListenRealtime>(QString::fromLatin1(RealtimePath), reader, badPath);
    return Result<ListenRealtime>::success(realtime);
}

Result<ListenRank> parseListenRank(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("listenRank");
    const QString dataPath = fieldPath(rootPath, "data");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<ListenRank>(QString::fromLatin1(RankPath), rootReader, dataPath);
    if (data.isEmpty())
        return missingFailure<ListenRank>(QString::fromLatin1(RankPath), dataPath,
                                          QStringLiteral("object"));

    JsonReader reader(data, dataPath);
    const QJsonArray rows = reader.array("songItems");
    const QString itemsPath = fieldPath(dataPath, "songItems");
    if (!reader.ok())
        return parseFailure<ListenRank>(QString::fromLatin1(RankPath), reader, itemsPath);

    ListenRank rank;
    QString badPath;
    rank.startTime = readInteger(reader, dataPath, "startTime", &badPath);
    rank.endTime = readInteger(reader, dataPath, "endTime", &badPath);
    rank.songCount = readInteger(reader, dataPath, "songCount", &badPath);
    if (!reader.ok())
        return parseFailure<ListenRank>(QString::fromLatin1(RankPath), reader, badPath);

    rank.items.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QString itemPath = indexedPath(itemsPath, i);
        if (!rows.at(i).isObject())
            return itemTypeFailure<ListenRank>(QString::fromLatin1(RankPath), itemPath);

        JsonReader itemReader(rows.at(i).toObject(), itemPath);
        ListenRankSong song;
        song.id = readInteger(itemReader, itemPath, "songId", &badPath);
        song.name = readString(itemReader, itemPath, "songName", &badPath);
        song.album = readString(itemReader, itemPath, "albumName", &badPath);
        song.cover = readString(itemReader, itemPath, "picUrl", &badPath);
        song.playCount = readInteger(itemReader, itemPath, "playCount", &badPath);
        if (!itemReader.ok())
            return parseFailure<ListenRank>(QString::fromLatin1(RankPath), itemReader, badPath);

        // artists 可能缺失（归空表）；显式类型错误仍然报 Parse。
        const QJsonArray artistRows = itemReader.array("artists");
        const QString artistsPath = fieldPath(itemPath, "artists");
        if (!itemReader.ok())
            return parseFailure<ListenRank>(QString::fromLatin1(RankPath), itemReader,
                                            artistsPath);
        song.artists.reserve(artistRows.size());
        for (qsizetype j = 0; j < artistRows.size(); ++j) {
            const QString artistPath = indexedPath(artistsPath, j);
            if (!artistRows.at(j).isObject())
                return itemTypeFailure<ListenRank>(QString::fromLatin1(RankPath), artistPath);

            JsonReader artistReader(artistRows.at(j).toObject(), artistPath);
            ListenRankArtist artist;
            artist.id = readInteger(artistReader, artistPath, "artistId", &badPath);
            artist.name = readString(artistReader, artistPath, "artistName", &badPath);
            if (!artistReader.ok())
                return parseFailure<ListenRank>(QString::fromLatin1(RankPath), artistReader,
                                                badPath);
            song.artists.append(artist);
        }

        rank.items.append(song);
    }

    return Result<ListenRank>::success(rank);
}

} // namespace ncm::sdk
