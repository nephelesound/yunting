#include "sdk/search_suggest.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto SuggestPath = "api/search/suggest/web";
constexpr auto ResultPath = "searchSuggest.result";

template <typename T>
Result<T> parseFailure(const JsonReader &reader, const QString &path)
{
    return Result<T>::failure(
        endpointError(ErrorKind::Parse, SuggestPath, reader.error(), path));
}

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

Result<QJsonArray> arrayField(const QJsonObject &object, const QString &objectPath,
                              const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    const QString path = objectPath + QLatin1Char('.') + QLatin1String(key);
    if (value.isUndefined() || value.isNull())
        return Result<QJsonArray>::success({});
    if (!value.isArray())
        return Result<QJsonArray>::failure(endpointError(
            ErrorKind::Parse, SuggestPath,
            path + QStringLiteral(": expected array"), path));
    return Result<QJsonArray>::success(value.toArray());
}

Result<QStringList> stringArrayField(const QJsonObject &object,
                                     const QString &objectPath, const char *key)
{
    const auto values = arrayField(object, objectPath, key);
    if (!values)
        return Result<QStringList>::failure(values.error);
    const QString path = objectPath + QLatin1Char('.') + QLatin1String(key);
    QStringList out;
    out.reserve(values.value->size());
    for (qsizetype i = 0; i < values.value->size(); ++i) {
        const QJsonValue value = values.value->at(i);
        if (!value.isString())
            return Result<QStringList>::failure(endpointError(
                ErrorKind::Parse, SuggestPath,
                path + QStringLiteral("[%1]: expected string").arg(i),
                indexedPath(path, i)));
        out.append(value.toString());
    }
    return Result<QStringList>::success(out);
}

Result<QJsonObject> objectItem(const QJsonValue &value, const QString &path)
{
    if (!value.isObject())
        return Result<QJsonObject>::failure(endpointError(
            ErrorKind::Parse, SuggestPath,
            path + QStringLiteral(": expected object"), path));
    return Result<QJsonObject>::success(value.toObject());
}

Result<SearchSuggestArtist> artistFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SearchSuggestArtist artist;
    artist.id = reader.integer("id");
    artist.name = reader.string("name");
    artist.cover = reader.string("picUrl");
    artist.alias = reader.stringList("alias");
    if (artist.id <= 0)
        reader.fail(QStringLiteral("id must be positive"));
    if (!reader.ok())
        return parseFailure<SearchSuggestArtist>(reader, path);
    return Result<SearchSuggestArtist>::success(artist);
}

Result<SearchSuggestSong> songFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SearchSuggestSong song;
    song.id = reader.integer("id");
    song.name = reader.string("name");
    song.alias = reader.stringList("alias");
    const QJsonArray artistValues = reader.array("artists");
    if (!reader.ok())
        return parseFailure<SearchSuggestSong>(reader, path);

    const QString artistsPath = path + QStringLiteral(".artists");
    song.artists.reserve(artistValues.size());
    for (qsizetype i = 0; i < artistValues.size(); ++i) {
        const QString artistPath = indexedPath(artistsPath, i);
        const auto item = objectItem(artistValues.at(i), artistPath);
        if (!item)
            return Result<SearchSuggestSong>::failure(item.error);
        JsonReader artistReader(*item.value, artistPath);
        const QString name = artistReader.string("name");
        if (!artistReader.ok())
            return parseFailure<SearchSuggestSong>(artistReader, artistPath);
        if (!name.isEmpty())
            song.artists.append(name);
    }

    const QJsonObject album = reader.object("album");
    if (!reader.ok())
        return parseFailure<SearchSuggestSong>(reader, path);
    JsonReader albumReader(album, path + QStringLiteral(".album"));
    song.albumId = albumReader.integer("id");
    song.albumName = albumReader.string("name");
    song.duration = reader.integer("duration") / 1000;
    if (!albumReader.ok())
        return parseFailure<SearchSuggestSong>(albumReader,
                                               path + QStringLiteral(".album"));
    if (song.id <= 0)
        reader.fail(QStringLiteral("id must be positive"));
    if (!reader.ok())
        return parseFailure<SearchSuggestSong>(reader, path);
    return Result<SearchSuggestSong>::success(song);
}

Result<SearchSuggestAlbum> albumFrom(const QJsonObject &object, const QString &path)
{
    JsonReader reader(object, path);
    SearchSuggestAlbum album;
    album.id = reader.integer("id");
    album.name = reader.string("name");
    const QJsonObject artist = reader.object("artist");
    album.publishTime = reader.integer("publishTime");
    album.size = reader.integer("size");
    if (!reader.ok())
        return parseFailure<SearchSuggestAlbum>(reader, path);

    const QString artistPath = path + QStringLiteral(".artist");
    JsonReader artistReader(artist, artistPath);
    album.artistId = artistReader.integer("id");
    album.artistName = artistReader.string("name");
    if (!artistReader.ok())
        return parseFailure<SearchSuggestAlbum>(artistReader, artistPath);
    if (album.id <= 0)
        reader.fail(QStringLiteral("id must be positive"));
    if (!reader.ok())
        return parseFailure<SearchSuggestAlbum>(reader, path);
    return Result<SearchSuggestAlbum>::success(album);
}

Result<SearchSuggestPlaylist> playlistFrom(const QJsonObject &object,
                                           const QString &path)
{
    JsonReader reader(object, path);
    SearchSuggestPlaylist playlist;
    playlist.id = reader.integer("id");
    playlist.name = reader.string("name");
    playlist.cover = reader.string("coverImgUrl");
    playlist.description = reader.string("description");
    playlist.trackCount = reader.integer("trackCount");
    playlist.playCount = reader.integer("playCount");
    playlist.creatorId = reader.integer("userId");
    if (playlist.id <= 0)
        reader.fail(QStringLiteral("id must be positive"));
    if (!reader.ok())
        return parseFailure<SearchSuggestPlaylist>(reader, path);
    return Result<SearchSuggestPlaylist>::success(playlist);
}

} // namespace

Result<SearchSuggestions> parseSearchSuggestions(const QJsonObject &root,
                                                 QString query)
{
    const QJsonValue resultValue = root.value(QStringLiteral("result"));
    if (!resultValue.isObject()) {
        const QString message = resultValue.isUndefined()
            ? QString::fromLatin1(ResultPath) + QStringLiteral(": missing object")
            : QString::fromLatin1(ResultPath) + QStringLiteral(": expected object");
        return Result<SearchSuggestions>::failure(endpointError(
            ErrorKind::Parse, SuggestPath, message, QString::fromLatin1(ResultPath)));
    }
    const QJsonObject result = resultValue.toObject();
    const QString resultPath = QString::fromLatin1(ResultPath);

    const auto artists = arrayField(result, resultPath, "artists");
    if (!artists)
        return Result<SearchSuggestions>::failure(artists.error);
    const auto songs = arrayField(result, resultPath, "songs");
    if (!songs)
        return Result<SearchSuggestions>::failure(songs.error);
    const auto albums = arrayField(result, resultPath, "albums");
    if (!albums)
        return Result<SearchSuggestions>::failure(albums.error);
    const auto playlists = arrayField(result, resultPath, "playlists");
    if (!playlists)
        return Result<SearchSuggestions>::failure(playlists.error);
    const auto order = stringArrayField(result, resultPath, "order");
    if (!order)
        return Result<SearchSuggestions>::failure(order.error);

    SearchSuggestions suggestions;
    suggestions.query = std::move(query);
    suggestions.order = *order.value;

    const QString artistsPath = resultPath + QStringLiteral(".artists");
    suggestions.artists.reserve(artists.value->size());
    for (qsizetype i = 0; i < artists.value->size(); ++i) {
        const QString path = indexedPath(artistsPath, i);
        const auto item = objectItem(artists.value->at(i), path);
        if (!item)
            return Result<SearchSuggestions>::failure(item.error);
        const auto parsed = artistFrom(*item.value, path);
        if (!parsed)
            return Result<SearchSuggestions>::failure(parsed.error);
        suggestions.artists.append(*parsed.value);
    }

    const QString songsPath = resultPath + QStringLiteral(".songs");
    suggestions.songs.reserve(songs.value->size());
    for (qsizetype i = 0; i < songs.value->size(); ++i) {
        const QString path = indexedPath(songsPath, i);
        const auto item = objectItem(songs.value->at(i), path);
        if (!item)
            return Result<SearchSuggestions>::failure(item.error);
        const auto parsed = songFrom(*item.value, path);
        if (!parsed)
            return Result<SearchSuggestions>::failure(parsed.error);
        suggestions.songs.append(*parsed.value);
    }

    const QString albumsPath = resultPath + QStringLiteral(".albums");
    suggestions.albums.reserve(albums.value->size());
    for (qsizetype i = 0; i < albums.value->size(); ++i) {
        const QString path = indexedPath(albumsPath, i);
        const auto item = objectItem(albums.value->at(i), path);
        if (!item)
            return Result<SearchSuggestions>::failure(item.error);
        const auto parsed = albumFrom(*item.value, path);
        if (!parsed)
            return Result<SearchSuggestions>::failure(parsed.error);
        suggestions.albums.append(*parsed.value);
    }

    const QString playlistsPath = resultPath + QStringLiteral(".playlists");
    suggestions.playlists.reserve(playlists.value->size());
    for (qsizetype i = 0; i < playlists.value->size(); ++i) {
        const QString path = indexedPath(playlistsPath, i);
        const auto item = objectItem(playlists.value->at(i), path);
        if (!item)
            return Result<SearchSuggestions>::failure(item.error);
        const auto parsed = playlistFrom(*item.value, path);
        if (!parsed)
            return Result<SearchSuggestions>::failure(parsed.error);
        suggestions.playlists.append(*parsed.value);
    }

    return Result<SearchSuggestions>::success(std::move(suggestions));
}

void fetchSearchSuggestions(NcmClient &client, QString query, QObject *context,
                            SearchSuggestionsCallback callback)
{
    if (!callback)
        return;
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("s"), query);
    postFormJson(client, context, QString::fromLatin1(SuggestPath), form,
                 [callback = std::move(callback), query = std::move(query)]
                 (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<SearchSuggestions>::failure(root.error));
            return;
        }
        callback(parseSearchSuggestions(*root.value, query));
    });
}

} // namespace ncm::sdk
