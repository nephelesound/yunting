#include "sdk/library.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>
#include <utility>

namespace ncm::sdk {
namespace {

// Error::endpoint carries these weapi-relative paths; the shared transport
// prepends "weapi/". The reference modules' "/api/..." prefix is stripped when
// they build their weapi URL (request.js: uri.substr(5)).
constexpr auto ArtistSublistPath = "artist/sublist";
constexpr auto AlbumSublistPath = "album/sublist";
constexpr auto MvSublistPath = "cloudvideo/allvideo/sublist";

Result<SubscribedArtist> artistFrom(const QJsonObject &object, const QString &path,
                                    const char *endpoint)
{
    JsonReader reader(object, path);
    SubscribedArtist artist;
    artist.id = reader.integer("id");
    artist.name = reader.string("name");
    artist.cover = reader.string("picUrl");
    artist.trans = reader.string("trans");
    artist.alias = reader.stringList("alias");
    artist.albumSize = reader.integer("albumSize");
    artist.mvSize = reader.integer("mvSize");
    artist.latestInfo = reader.string("info");
    if (artist.cover.isEmpty())
        artist.cover = reader.string("img1v1Url");
    if (artist.id <= 0)
        reader.fail(QStringLiteral("id must be positive"));
    if (!reader.ok())
        return parseFailure<SubscribedArtist>(endpoint, reader, path);
    return Result<SubscribedArtist>::success(artist);
}

Result<SubscribedAlbum> albumFrom(const QJsonObject &object, const QString &path,
                                  const char *endpoint)
{
    JsonReader reader(object, path);
    SubscribedAlbum album;
    album.id = reader.integer("id");
    album.name = reader.string("name");
    album.cover = reader.string("picUrl");
    album.size = reader.integer("size");
    album.subTime = reader.integer("subTime");
    album.alias = reader.stringList("alias");

    const QJsonArray artists = reader.array("artists");
    album.artists.reserve(artists.size());
    for (qsizetype i = 0; i < artists.size(); ++i) {
        const QJsonValue value = artists.at(i);
        const QString artistPath = path + QStringLiteral(".artists[%1]").arg(i);
        if (!value.isObject())
            return itemTypeFailure<SubscribedAlbum>(endpoint, artistPath);
        JsonReader artistReader(value.toObject(), artistPath);
        const qint64 artistId = artistReader.integer("id");
        const QString artistName = artistReader.string("name");
        if (!artistReader.ok())
            return parseFailure<SubscribedAlbum>(endpoint, artistReader, artistPath);
        if (album.artists.isEmpty()) {
            album.artistId = artistId;
            album.artistName = artistName;
        }
        if (!artistName.isEmpty())
            album.artists.append(artistName);
    }

    if (album.id <= 0)
        reader.fail(QStringLiteral("id must be positive"));
    if (!reader.ok())
        return parseFailure<SubscribedAlbum>(endpoint, reader, path);
    return Result<SubscribedAlbum>::success(album);
}

Result<SubscribedMv> mvFrom(const QJsonObject &object, const QString &path,
                            const char *endpoint)
{
    JsonReader reader(object, path);
    SubscribedMv mv;
    mv.id = reader.string("vid");
    mv.type = static_cast<int>(reader.integer("type"));
    mv.name = reader.string("title");
    mv.cover = reader.string("coverUrl");
    mv.duration = reader.integer("durationms") / 1000;
    mv.playCount = reader.integer("playTime");

    const QJsonArray creators = reader.array("creator");
    if (!creators.isEmpty()) {
        const QString creatorPath = path + QStringLiteral(".creator[0]");
        const QJsonValue value = creators.first();
        if (!value.isObject())
            return itemTypeFailure<SubscribedMv>(endpoint, creatorPath);
        JsonReader creatorReader(value.toObject(), creatorPath);
        mv.creatorId = creatorReader.integer("userId");
        mv.creatorName = creatorReader.string("userName");
        if (!creatorReader.ok())
            return parseFailure<SubscribedMv>(endpoint, creatorReader, creatorPath);
    }

    if (mv.id.isEmpty())
        reader.fail(QStringLiteral("vid must not be empty"));
    if (!reader.ok())
        return parseFailure<SubscribedMv>(endpoint, reader, path);
    return Result<SubscribedMv>::success(mv);
}

QJsonObject pagePayload(int limit, int offset)
{
    return QJsonObject{{QStringLiteral("limit"), limit > 0 ? limit : 25},
                       {QStringLiteral("offset"), std::max(0, offset)},
                       {QStringLiteral("total"), true}};
}

} // namespace

Result<SubscribedArtistPage> parseSubscribedArtists(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("library.artists"));
    const QJsonArray items = reader.array("data");
    if (!reader.ok())
        return parseFailure<SubscribedArtistPage>(
            ArtistSublistPath, reader, QStringLiteral("library.artists"));
    if (!reader.has("data"))
        return missingFailure<SubscribedArtistPage>(
            ArtistSublistPath, QStringLiteral("library.artists.data"));

    SubscribedArtistPage page;
    page.count = reader.integer("count");
    page.hasMore = reader.boolean("hasMore");
    if (!reader.ok())
        return parseFailure<SubscribedArtistPage>(
            ArtistSublistPath, reader, QStringLiteral("library.artists"));

    page.items.reserve(items.size());
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("library.artists.data[%1]").arg(i);
        if (!items.at(i).isObject())
            return itemTypeFailure<SubscribedArtistPage>(ArtistSublistPath, path);
        const auto parsed = artistFrom(items.at(i).toObject(), path, ArtistSublistPath);
        if (!parsed)
            return Result<SubscribedArtistPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<SubscribedArtistPage>::success(page);
}

Result<SubscribedAlbumPage> parseSubscribedAlbums(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("library.albums"));
    const QJsonArray items = reader.array("data");
    if (!reader.ok())
        return parseFailure<SubscribedAlbumPage>(
            AlbumSublistPath, reader, QStringLiteral("library.albums"));
    if (!reader.has("data"))
        return missingFailure<SubscribedAlbumPage>(
            AlbumSublistPath, QStringLiteral("library.albums.data"));

    SubscribedAlbumPage page;
    page.count = reader.integer("count");
    page.paidCount = reader.integer("paidCount");
    page.hasMore = reader.boolean("hasMore");
    if (!reader.ok())
        return parseFailure<SubscribedAlbumPage>(
            AlbumSublistPath, reader, QStringLiteral("library.albums"));

    page.items.reserve(items.size());
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("library.albums.data[%1]").arg(i);
        if (!items.at(i).isObject())
            return itemTypeFailure<SubscribedAlbumPage>(AlbumSublistPath, path);
        const auto parsed = albumFrom(items.at(i).toObject(), path, AlbumSublistPath);
        if (!parsed)
            return Result<SubscribedAlbumPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<SubscribedAlbumPage>::success(page);
}

Result<SubscribedMvPage> parseSubscribedMvs(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("library.mvs"));
    const QJsonArray items = reader.array("data");
    if (!reader.ok())
        return parseFailure<SubscribedMvPage>(
            MvSublistPath, reader, QStringLiteral("library.mvs"));
    if (!reader.has("data"))
        return missingFailure<SubscribedMvPage>(
            MvSublistPath, QStringLiteral("library.mvs.data"));

    SubscribedMvPage page;
    page.count = reader.integer("count");
    page.hasMore = reader.boolean("hasMore");
    if (!reader.ok())
        return parseFailure<SubscribedMvPage>(
            MvSublistPath, reader, QStringLiteral("library.mvs"));

    page.items.reserve(items.size());
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("library.mvs.data[%1]").arg(i);
        if (!items.at(i).isObject())
            return itemTypeFailure<SubscribedMvPage>(MvSublistPath, path);
        const auto parsed = mvFrom(items.at(i).toObject(), path, MvSublistPath);
        if (!parsed)
            return Result<SubscribedMvPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<SubscribedMvPage>::success(page);
}

void fetchSubscribedArtists(NcmClient &client, int limit, int offset,
                            QObject *context, SubscribedArtistsCallback callback)
{
    if (!callback)
        return;
    const int requestedOffset = std::max(0, offset);
    postWeapiJson(client, context, QString::fromLatin1(ArtistSublistPath),
                  pagePayload(limit, offset), true,
                  [callback = std::move(callback), requestedOffset]
                  (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<SubscribedArtistPage>::failure(root.error));
            return;
        }
        auto page = parseSubscribedArtists(*root.value);
        if (page)
            page.value->offset = requestedOffset;
        callback(std::move(page));
    });
}

void fetchSubscribedAlbums(NcmClient &client, int limit, int offset,
                           QObject *context, SubscribedAlbumsCallback callback)
{
    if (!callback)
        return;
    const int requestedOffset = std::max(0, offset);
    postWeapiJson(client, context, QString::fromLatin1(AlbumSublistPath),
                  pagePayload(limit, offset), true,
                  [callback = std::move(callback), requestedOffset]
                  (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<SubscribedAlbumPage>::failure(root.error));
            return;
        }
        auto page = parseSubscribedAlbums(*root.value);
        if (page)
            page.value->offset = requestedOffset;
        callback(std::move(page));
    });
}

void fetchSubscribedMvs(NcmClient &client, int limit, int offset,
                        QObject *context, SubscribedMvsCallback callback)
{
    if (!callback)
        return;
    const int requestedOffset = std::max(0, offset);
    postWeapiJson(client, context, QString::fromLatin1(MvSublistPath),
                  pagePayload(limit, offset), true,
                  [callback = std::move(callback), requestedOffset]
                  (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<SubscribedMvPage>::failure(root.error));
            return;
        }
        auto page = parseSubscribedMvs(*root.value);
        if (page)
            page.value->offset = requestedOffset;
        callback(std::move(page));
    });
}

} // namespace ncm::sdk
