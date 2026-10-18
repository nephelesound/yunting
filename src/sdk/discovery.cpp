#include "sdk/discovery.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <algorithm>
#include <QJsonObject>
#include <QPointer>
#include <QUrlQuery>
#include <memory>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto NewSongsPath = "api/personalized/newsong";
constexpr auto NewAlbumsPath = "api/album/new";
constexpr auto HighQualityPath = "api/playlist/highquality/list";
constexpr auto HighQualityTagsPath = "api/playlist/highquality/tags";
constexpr auto BannersPath = "api/v2/banner/get";

QStringList namesOf(const QJsonArray &objects)
{
    QStringList names;
    for (const QJsonValue &value : objects) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            names.append(name);
    }
    return names;
}
} // namespace

Result<QList<NewSong>> parseNewSongs(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("discovery.newSongs"));
    const QJsonArray items = reader.array("result");
    if (!reader.ok())
        return Result<QList<NewSong>>::failure(
            endpointError(ErrorKind::Parse, NewSongsPath, reader.error()));
    if (!reader.has("result"))
        return Result<QList<NewSong>>::failure(
            endpointError(ErrorKind::Parse, NewSongsPath,
                          QStringLiteral("discovery.newSongs.result: missing array")));

    QList<NewSong> songs;
    for (qsizetype i = 0; i < items.size(); ++i) {
        JsonReader itemReader(items.at(i).toObject(),
                              QStringLiteral("discovery.newSongs.result[%1]").arg(i));
        const QJsonObject song = itemReader.object("song");
        if (!itemReader.ok())
            return Result<QList<NewSong>>::failure(
                endpointError(ErrorKind::Parse, NewSongsPath, itemReader.error()));
        if (song.isEmpty())
            continue;
        JsonReader songReader(song, itemReader.path() + QStringLiteral(".song"));
        NewSong out;
        out.songId = songReader.integer("id");
        out.name = songReader.string("name");
        out.artists = namesOf(song.value(QStringLiteral("artists")).toArray());
        const QJsonObject album = song.value(QStringLiteral("album")).toObject();
        JsonReader albumReader(album, songReader.path() + QStringLiteral(".album"));
        out.albumId = albumReader.integer("id");
        out.albumName = albumReader.string("name");
        out.cover = albumReader.string("picUrl");
        out.duration = songReader.integer("duration") / 1000;
        out.fee = static_cast<int>(songReader.integer("fee"));
        if (out.cover.isEmpty())
            out.cover = itemReader.string("picUrl");
        if (!songReader.ok() || !albumReader.ok()) {
            const QString error = !songReader.ok() ? songReader.error() : albumReader.error();
            return Result<QList<NewSong>>::failure(
                endpointError(ErrorKind::Parse, NewSongsPath, error));
        }
        if (out.songId > 0)
            songs.append(out);
    }
    return Result<QList<NewSong>>::success(songs);
}

Result<QList<NewAlbum>> parseNewAlbums(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("discovery.newAlbums"));
    const QJsonArray items = reader.array("albums");
    if (!reader.ok())
        return Result<QList<NewAlbum>>::failure(
            endpointError(ErrorKind::Parse, NewAlbumsPath, reader.error()));
    if (!reader.has("albums"))
        return Result<QList<NewAlbum>>::failure(
            endpointError(ErrorKind::Parse, NewAlbumsPath,
                          QStringLiteral("discovery.newAlbums.albums: missing array")));

    QList<NewAlbum> albums;
    for (qsizetype i = 0; i < items.size(); ++i) {
        JsonReader item(items.at(i).toObject(),
                        QStringLiteral("discovery.newAlbums.albums[%1]").arg(i));
        NewAlbum out;
        out.id = item.integer("id");
        out.name = item.string("name");
        out.cover = item.string("picUrl");
        out.publishTime = item.integer("publishTime");
        const QJsonObject artist = item.object("artist");
        JsonReader artistReader(artist, item.path() + QStringLiteral(".artist"));
        out.artistId = artistReader.integer("id");
        out.artistName = artistReader.string("name");
        out.artists = namesOf(item.array("artists"));
        out.size = item.integer("size");
        if (!item.ok() || !artistReader.ok()) {
            const QString error = !item.ok() ? item.error() : artistReader.error();
            return Result<QList<NewAlbum>>::failure(
                endpointError(ErrorKind::Parse, NewAlbumsPath, error));
        }
        if (out.id > 0)
            albums.append(out);
    }
    return Result<QList<NewAlbum>>::success(albums);
}

Result<HighQualityPlaylistPage> parseHighQualityPlaylists(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("discovery.highQualityPlaylists"));
    const QJsonArray items = reader.array("playlists");
    if (!reader.ok())
        return Result<HighQualityPlaylistPage>::failure(
            endpointError(ErrorKind::Parse, HighQualityPath, reader.error()));
    if (!reader.has("playlists"))
        return Result<HighQualityPlaylistPage>::failure(
            endpointError(ErrorKind::Parse, HighQualityPath,
                          QStringLiteral("discovery.highQualityPlaylists.playlists: missing array")));

    HighQualityPlaylistPage page;
    page.more = reader.boolean("more");
    page.lasttime = reader.integer("lasttime");
    for (qsizetype i = 0; i < items.size(); ++i) {
        JsonReader item(items.at(i).toObject(),
                        QStringLiteral("discovery.highQualityPlaylists.playlists[%1]").arg(i));
        HighQualityPlaylist out;
        out.id = item.integer("id");
        out.name = item.string("name");
        out.cover = item.string("coverImgUrl");
        out.description = item.string("description");
        out.trackCount = item.integer("trackCount");
        out.playCount = item.integer("playCount");
        out.subscribedCount = item.integer("subscribedCount");
        out.updateTime = item.integer("updateTime");
        const QJsonObject creator = item.object("creator");
        JsonReader creatorReader(creator, item.path() + QStringLiteral(".creator"));
        out.creatorId = creatorReader.integer("userId");
        out.creatorName = creatorReader.string("nickname");
        if (!item.ok() || !creatorReader.ok()) {
            const QString error = !item.ok() ? item.error() : creatorReader.error();
            return Result<HighQualityPlaylistPage>::failure(
                endpointError(ErrorKind::Parse, HighQualityPath, error));
        }
        if (out.id > 0)
            page.items.append(out);
    }
    return Result<HighQualityPlaylistPage>::success(page);
}

Result<QList<HighQualityTag>> parseHighQualityTags(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("discovery.highQualityTags"));
    const QJsonArray items = reader.array("tags");
    if (!reader.ok())
        return Result<QList<HighQualityTag>>::failure(
            endpointError(ErrorKind::Parse, HighQualityTagsPath, reader.error()));
    if (!reader.has("tags"))
        return Result<QList<HighQualityTag>>::failure(
            endpointError(ErrorKind::Parse, HighQualityTagsPath,
                          QStringLiteral("discovery.highQualityTags.tags: missing array")));

    QList<HighQualityTag> tags;
    for (qsizetype i = 0; i < items.size(); ++i) {
        JsonReader item(items.at(i).toObject(),
                        QStringLiteral("discovery.highQualityTags.tags[%1]").arg(i));
        HighQualityTag out;
        out.id = item.integer("id");
        out.name = item.string("name");
        out.type = static_cast<int>(item.integer("type"));
        out.category = static_cast<int>(item.integer("category"));
        out.hot = item.boolean("hot");
        if (!item.ok()) {
            return Result<QList<HighQualityTag>>::failure(
                endpointError(ErrorKind::Parse, HighQualityTagsPath, item.error()));
        }
        if (!out.name.isEmpty())
            tags.append(out);
    }
    return Result<QList<HighQualityTag>>::success(tags);
}

Result<QList<Banner>> parseBanners(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("discovery.banners"));
    const QJsonArray items = reader.array("banners");
    if (!reader.ok())
        return Result<QList<Banner>>::failure(
            endpointError(ErrorKind::Parse, BannersPath, reader.error()));
    if (!reader.has("banners"))
        return Result<QList<Banner>>::failure(
            endpointError(ErrorKind::Parse, BannersPath,
                          QStringLiteral("discovery.banners.banners: missing array")));

    QList<Banner> banners;
    for (qsizetype i = 0; i < items.size(); ++i) {
        JsonReader item(items.at(i).toObject(),
                        QStringLiteral("discovery.banners.banners[%1]").arg(i));
        Banner out;
        out.typeTitle = item.string("typeTitle");
        out.imageUrl = item.string("imageUrl");
        out.url = item.string("url");
        out.targetType = static_cast<int>(item.integer("targetType"));
        out.targetId = item.integer("targetId");
        if (!item.ok())
            return Result<QList<Banner>>::failure(
                endpointError(ErrorKind::Parse, BannersPath, item.error()));
        if (!out.imageUrl.isEmpty())
            banners.append(out);
    }
    return Result<QList<Banner>>::success(banners);
}

void fetchNewSongs(NcmClient &client, int limit, QObject *context,
                   NewSongsCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("type"), QStringLiteral("recommend"));
    form.addQueryItem(QStringLiteral("limit"), QString::number(std::max(1, limit)));
    form.addQueryItem(QStringLiteral("areaId"), QStringLiteral("0"));
    postFormJson(client, context, QString::fromLatin1(NewSongsPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<NewSong>>::failure(root.error));
            return;
        }
        callback(parseNewSongs(*root.value));
    });
}

void fetchNewAlbums(NcmClient &client, int limit, int offset, QString area,
                    QObject *context, NewAlbumsCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("limit"), QString::number(std::max(1, limit)));
    form.addQueryItem(QStringLiteral("offset"), QString::number(std::max(0, offset)));
    form.addQueryItem(QStringLiteral("total"), QStringLiteral("true"));
    form.addQueryItem(QStringLiteral("area"), area.isEmpty() ? QStringLiteral("ALL") : area);
    postFormJson(client, context, QString::fromLatin1(NewAlbumsPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<NewAlbum>>::failure(root.error));
            return;
        }
        callback(parseNewAlbums(*root.value));
    });
}

void fetchHighQualityPlaylists(NcmClient &client, QString cat, int limit, qint64 before,
                               QObject *context, HighQualityPlaylistsCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("cat"), cat.isEmpty() ? QStringLiteral("全部") : cat);
    form.addQueryItem(QStringLiteral("limit"), QString::number(std::max(1, limit)));
    form.addQueryItem(QStringLiteral("lasttime"), QString::number(std::max<qint64>(0, before)));
    form.addQueryItem(QStringLiteral("total"), QStringLiteral("true"));
    postFormJson(client, context, QString::fromLatin1(HighQualityPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<HighQualityPlaylistPage>::failure(root.error));
            return;
        }
        callback(parseHighQualityPlaylists(*root.value));
    });
}

void fetchHighQualityTags(NcmClient &client, QObject *context,
                          HighQualityTagsCallback callback)
{
    postFormJson(client, context, QString::fromLatin1(HighQualityTagsPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<HighQualityTag>>::failure(root.error));
            return;
        }
        callback(parseHighQualityTags(*root.value));
    });
}

void fetchBanners(NcmClient &client, QObject *context, BannersCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("clientType"), QStringLiteral("pc"));
    postFormJson(client, context, QString::fromLatin1(BannersPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<Banner>>::failure(root.error));
            return;
        }
        callback(parseBanners(*root.value));
    });
}

} // namespace ncm::sdk
