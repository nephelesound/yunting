#pragma once
// Search suggest SDK — typed completion rows for the search box.
//
// Raw endpoint:
//   POST /api/search/suggest/web    plaintext form `s`, anonymous
//
// The endpoint omits categories that have no matches (an empty `result` object
// is valid); absent arrays normalize to empty lists. `order` is the server's
// display order and is preserved verbatim. URLs stay raw strings — the
// frontend owns image sizing.
#include "sdk/sdk_result.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct SearchSuggestArtist {
    qint64 id = 0;          // result.artists[].id
    QString name;           // result.artists[].name
    QString cover;          // result.artists[].picUrl (raw URL, may be empty)
    QStringList alias;      // result.artists[].alias
};

struct SearchSuggestSong {
    qint64 id = 0;          // result.songs[].id
    QString name;           // result.songs[].name
    QStringList artists;    // result.songs[].artists[].name
    QStringList alias;      // result.songs[].alias
    qint64 albumId = 0;     // result.songs[].album.id
    QString albumName;      // result.songs[].album.name
    qint64 duration = 0;    // result.songs[].duration / 1000 (seconds)
};

struct SearchSuggestAlbum {
    qint64 id = 0;          // result.albums[].id
    QString name;           // result.albums[].name
    qint64 artistId = 0;    // result.albums[].artist.id
    QString artistName;     // result.albums[].artist.name
    qint64 publishTime = 0; // result.albums[].publishTime, milliseconds
    qint64 size = 0;        // result.albums[].size (track count)
};

struct SearchSuggestPlaylist {
    qint64 id = 0;          // result.playlists[].id
    QString name;           // result.playlists[].name
    QString cover;          // result.playlists[].coverImgUrl (raw URL)
    QString description;    // result.playlists[].description
    qint64 trackCount = 0;  // result.playlists[].trackCount
    qint64 playCount = 0;   // result.playlists[].playCount
    qint64 creatorId = 0;   // result.playlists[].userId
};

struct SearchSuggestions {
    QString query;          // caller-supplied query (the response has no echo)
    QList<SearchSuggestArtist> artists;       // result.artists[]
    QList<SearchSuggestSong> songs;           // result.songs[]
    QList<SearchSuggestAlbum> albums;         // result.albums[]
    QList<SearchSuggestPlaylist> playlists;   // result.playlists[]
    QStringList order;      // result.order (category slugs, server display order)
};

using SearchSuggestionsCallback = std::function<void(Result<SearchSuggestions>)>;

void fetchSearchSuggestions(NcmClient &client, QString query, QObject *context,
                            SearchSuggestionsCallback callback);

// Pure parser, exposed for tests and callers that already hold a response.
Result<SearchSuggestions> parseSearchSuggestions(const QJsonObject &root,
                                                 QString query = {});

} // namespace ncm::sdk
