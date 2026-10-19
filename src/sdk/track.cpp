#include "sdk/track.h"

#include "sdk/sdk_json.h"

#include <QDebug>
#include <QHash>
#include <QJsonValue>
#include <QStringList>
#include <cmath>
#include <utility>

namespace ncm::sdk {
namespace {

Error parseError(const JsonReader &reader, const QString &path)
{
    Error error;
    error.kind = ErrorKind::Parse;
    error.path = path;
    error.message = reader.error();
    return error;
}

// 数组元素不是对象时按空对象解析：旧归一化对缺失/异构行也返回默认值，而不是整片失败
Result<Track> trackFrom(const QJsonObject &song, const QJsonObject &privilege,
                        const QString &path)
{
    JsonReader reader(song, path);
    Track out;
    out.id = reader.integer("id");
    out.name = reader.string("name");

    // 新旧两种歌曲结构：ar/al 优先，字段缺失时才退回 artists/album
    const QJsonValue ar = song.value(QStringLiteral("ar"));
    const QJsonArray artistValues = ar.isArray() ? ar.toArray()
                                                 : song.value(QStringLiteral("artists")).toArray();
    out.artists.reserve(artistValues.size());
    for (qsizetype i = 0; i < artistValues.size(); ++i) {
        const QJsonObject object = artistValues.at(i).toObject();
        JsonReader artistReader(object, QStringLiteral("%1.artists[%2]").arg(path).arg(i));
        TrackArtist artist;
        artist.id = artistReader.integer("id");
        artist.name = artistReader.string("name");
        if (!artistReader.ok())
            return Result<Track>::failure(parseError(artistReader, artistReader.path()));
        out.artists.append(artist);
    }

    const QJsonValue al = song.value(QStringLiteral("al"));
    const QJsonObject album = al.isObject() ? al.toObject()
                                            : song.value(QStringLiteral("album")).toObject();
    JsonReader albumReader(album, path + QStringLiteral(".album"));
    out.albumId = albumReader.integer("id");
    out.albumName = albumReader.string("name");
    out.cover = albumReader.string("picUrl");

    // 旧结构用 duration；dt 优先，0 与缺失一样退回 duration
    const double dt = reader.number("dt");
    const double milliseconds = dt != 0 ? dt : reader.number("duration");
    out.duration = static_cast<qint64>(std::floor(milliseconds / 1000));
    out.fee = reader.number("fee");
    for (const char *key : {"alia", "alias", "tns", "transNames"}) {
        const QStringList aliases = reader.stringList(key);
        if (!aliases.isEmpty()) {
            out.alias = aliases.first();
            break;
        }
    }
    out.quality = reader.truthy("hr") || reader.truthy("hrMusic") ? QStringLiteral("hires")
        : reader.truthy("sq") || reader.truthy("sqMusic") ? QStringLiteral("lossless") : QString();
    const double mv = reader.number("mv");
    out.mv = static_cast<qint64>(mv != 0 ? mv : reader.number("mvid"));

    if (!reader.ok())
        return Result<Track>::failure(parseError(reader, reader.path()));
    if (!albumReader.ok())
        return Result<Track>::failure(parseError(albumReader, albumReader.path()));

    // 平行数组里的 privilege 优先；没有时读歌曲内嵌的那个
    const QJsonObject privilegeObject = !privilege.isEmpty()
        ? privilege : song.value(QStringLiteral("privilege")).toObject();
    if (!privilegeObject.isEmpty()) {
        JsonReader privilegeReader(privilegeObject, path + QStringLiteral(".privilege"));
        out.privilege.present = true;
        out.privilege.hasFee = privilegeReader.has("fee");
        out.privilege.fee = privilegeReader.number("fee");
        out.privilege.st = privilegeReader.number("st");
        out.privilege.cloud = privilegeReader.truthy("cs");
        out.privilege.pl = privilegeReader.number("pl");
        out.privilege.payed = privilegeReader.truthy("payed");
        if (!privilegeReader.ok())
            return Result<Track>::failure(
                parseError(privilegeReader, privilegeReader.path()));
    }
    return Result<Track>::success(std::move(out));
}

Result<PlaylistCard> cardFrom(const QJsonObject &playlist, const char *coverKey,
                              const QString &path)
{
    JsonReader reader(playlist, path);
    const QJsonObject creator = reader.object("creator");
    JsonReader creatorReader(creator, path + QStringLiteral(".creator"));
    PlaylistCard out;
    out.id = reader.integer("id");
    out.name = reader.string("name");
    out.cover = reader.string(coverKey);
    out.count = reader.integer("trackCount");
    // 前端只展示第一行，与旧归一化的 ncmFirstLine 相同
    out.description = reader.string("description").section(u'\n', 0, 0);
    out.playCount = reader.integer("playCount");
    out.creatorId = creatorReader.integer("userId");
    out.creatorName = creatorReader.string("nickname");
    out.updateTime = reader.integer("updateTime");
    out.subscribed = reader.boolean("subscribed");
    out.privacy = static_cast<int>(reader.integer("privacy"));
    out.tags = reader.stringList("tags");
    out.reason = reader.string("copywriter");
    if (!reader.ok())
        return Result<PlaylistCard>::failure(parseError(reader, reader.path()));
    if (!creatorReader.ok())
        return Result<PlaylistCard>::failure(parseError(creatorReader, creatorReader.path()));
    return Result<PlaylistCard>::success(std::move(out));
}

} // namespace

Result<Track> parseTrack(const QJsonObject &song, const QJsonObject &privilege)
{
    return trackFrom(song, privilege, QStringLiteral("song"));
}

Result<QList<Track>> parseTracks(const QJsonArray &songs, const QJsonArray &privileges)
{
    // privileges 与 songs 分开发，按 id 建索引；id 缺失的条目落到 0，与旧索引一致
    QHash<double, QJsonObject> byId;
    byId.reserve(privileges.size());
    for (const QJsonValue &value : privileges) {
        const QJsonObject privilege = value.toObject();
        if (!privilege.isEmpty())
            byId.insert(privilege.value(QStringLiteral("id")).toDouble(), privilege);
    }
    // 一首歌的字段类型不对只丢这一首（记下字段路径），不让整张列表（每日推荐、歌单）跟着没了
    QList<Track> out;
    out.reserve(songs.size());
    for (qsizetype i = 0; i < songs.size(); ++i) {
        const QJsonObject song = songs.at(i).toObject();
        const auto parsed = trackFrom(
            song, byId.value(song.value(QStringLiteral("id")).toDouble()),
            QStringLiteral("songs[%1]").arg(i));
        if (!parsed) {
            qWarning().noquote() << "skipped malformed song at" << parsed.error.path << parsed.error.message;
            continue;
        }
        out.append(*parsed.value);
    }
    return Result<QList<Track>>::success(std::move(out));
}

TrackPlayability playability(const Track &track)
{
    if (!track.privilege.present) {
        if (track.fee == 4)
            return {false, QStringLiteral("paid")};
        if (track.fee == 1)
            return {false, QStringLiteral("vip")};
        return {true, QString()};
    }
    const TrackPrivilege &privilege = track.privilege;
    const double fee = privilege.hasFee ? privilege.fee : track.fee;
    if (privilege.st < 0)
        return {false, QStringLiteral("unavailable")};
    if (privilege.cloud)
        return {true, QString()};
    if (privilege.pl > 0)
        return {true, QString()};
    if (fee == 1)
        return {false, QStringLiteral("vip")};
    if (fee == 4 && !privilege.payed)
        return {false, QStringLiteral("paid")};
    return {false, QStringLiteral("unavailable")};
}

Result<PlaylistCard> parsePlaylistCard(const QJsonObject &playlist, const char *coverKey)
{
    return cardFrom(playlist, coverKey, QStringLiteral("playlist"));
}

Result<QList<PlaylistCard>> parsePlaylistCards(const QJsonArray &playlists,
                                               const char *coverKey)
{
    QList<PlaylistCard> out;
    out.reserve(playlists.size());
    for (qsizetype i = 0; i < playlists.size(); ++i) {
        const auto parsed = cardFrom(playlists.at(i).toObject(), coverKey,
                                     QStringLiteral("playlists[%1]").arg(i));
        if (!parsed) {
            // 与 parseTracks 一样：坏的一张卡片不连累整排
            qWarning().noquote() << "skipped malformed playlist card at" << parsed.error.path << parsed.error.message;
            continue;
        }
        out.append(*parsed.value);
    }
    return Result<QList<PlaylistCard>>::success(std::move(out));
}

QString sizedUrl(const QString &url, int px)
{
    return url.isEmpty() ? QString() : url + QStringLiteral("?param=%1y%2").arg(px).arg(px);
}

QVariantMap trackToVariantMap(const Track &track, const QString &reason, int coverPx)
{
    QStringList names;
    QVariantList artistList;
    artistList.reserve(track.artists.size());
    for (const TrackArtist &artist : track.artists) {
        names.append(artist.name);
        artistList.append(QVariantMap{{"id", static_cast<double>(artist.id)},
                                      {"name", artist.name}});
    }
    const TrackPlayability play = playability(track);
    const QString cover = coverPx > 0 ? sizedUrl(track.cover, coverPx) : track.cover;
    return {
        {"id", static_cast<double>(track.id)},
        {"name", track.name},
        {"artist", names.join(QStringLiteral(" / "))},
        {"artistId", track.artists.isEmpty()
             ? 0.0 : static_cast<double>(track.artists.first().id)},
        {"artists", artistList},
        {"albumId", static_cast<double>(track.albumId)},
        {"album", track.albumName},
        {"dur", static_cast<double>(track.duration)},
        {"fee", track.fee},
        {"cover", cover},
        {"alias", track.alias},
        {"quality", track.quality},
        {"mv", static_cast<double>(track.mv)},
        {"playable", play.playable},
        {"blockedBy", play.blockedBy},
        {"reason", reason},
    };
}

QVariantList tracksToVariantList(const QList<Track> &tracks)
{
    QVariantList out;
    out.reserve(tracks.size());
    for (const Track &track : tracks)
        out.append(trackToVariantMap(track));
    return out;
}

QVariantMap playlistCardToVariantMap(const PlaylistCard &card)
{
    QVariantList tags;
    tags.reserve(card.tags.size());
    for (const QString &tag : card.tags)
        tags.append(tag);
    return {
        {"id", static_cast<double>(card.id)},
        {"name", card.name},
        {"cover", sizedUrl(card.cover, 300)},
        {"count", static_cast<double>(card.count)},
        {"description", card.description},
        {"playCount", static_cast<double>(card.playCount)},
        {"creatorId", static_cast<double>(card.creatorId)},
        {"creatorName", card.creatorName},
        {"updateTime", static_cast<double>(card.updateTime)},
        {"subscribed", card.subscribed},
        {"privacy", static_cast<double>(card.privacy)},
        {"tags", tags},
        {"reason", card.reason},
    };
}

} // namespace ncm::sdk
