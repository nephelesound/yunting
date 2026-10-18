// Typed search-suggest SDK contract.
#include "sdk/search_suggest.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkSearchSuggestTest : public QObject {
    Q_OBJECT

    static QJsonObject artistObject()
    {
        return QJsonObject{{"id", 1},
                           {"name", "歌手甲"},
                           {"picUrl", "http://p1.music.126.net/artist.jpg"},
                           {"alias", QJsonArray{QStringLiteral("Alias A")}},
                           {"albumSize", 12},
                           {"picId", 123}};
    }

    static QJsonObject songObject()
    {
        QJsonObject artist{{"id", 1}, {"name", "歌手甲"}, {"picUrl", QJsonValue::Null}};
        QJsonObject feature{{"id", 2}, {"name", "歌手乙"}};
        QJsonObject albumArtist{{"id", 3}, {"name", "专辑歌手"}};
        QJsonObject album{{"id", 21},
                          {"name", "专辑甲"},
                          {"artist", albumArtist},
                          {"picId", 456}};
        return QJsonObject{{"id", 11},
                           {"name", "歌曲甲"},
                           {"alias", QJsonArray{QStringLiteral("副标题")}},
                           {"artists", QJsonArray{artist, feature}},
                           {"album", album},
                           {"duration", 215500},
                           {"fee", 1},
                           {"mvid", 789}};
    }

    static QJsonObject albumObject()
    {
        QJsonObject artist{{"id", 1}, {"name", "歌手甲"}};
        return QJsonObject{{"id", 21},
                           {"name", "专辑甲"},
                           {"artist", artist},
                           {"publishTime", 1700000000000.0},
                           {"size", 10},
                           {"picId", 456}};
    }

    static QJsonObject playlistObject()
    {
        return QJsonObject{{"id", 31},
                           {"name", "歌单甲"},
                           {"coverImgUrl", "http://p1.music.126.net/playlist.jpg"},
                           {"description", "歌单简介"},
                           {"trackCount", 20},
                           {"playCount", 100},
                           {"userId", 41},
                           {"creator", QJsonValue::Null},
                           {"specialType", 0}};
    }

private slots:
    void parsesSuggestions()
    {
        QJsonObject result{{"order", QJsonArray{QStringLiteral("songs"),
                                                 QStringLiteral("artists"),
                                                 QStringLiteral("albums"),
                                                 QStringLiteral("playlists")}},
                           {"artists", QJsonArray{artistObject()}},
                           {"songs", QJsonArray{songObject()}},
                           {"albums", QJsonArray{albumObject()}},
                           {"playlists", QJsonArray{playlistObject()}}};
        const QJsonObject root{{"code", 200}, {"result", result}};

        const auto parsed = parseSearchSuggestions(root, QStringLiteral("歌手"));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SearchSuggestions &out = *parsed.value;
        QCOMPARE(out.query, QStringLiteral("歌手"));
        const QStringList expectedOrder{QStringLiteral("songs"), QStringLiteral("artists"),
                                        QStringLiteral("albums"), QStringLiteral("playlists")};
        QCOMPARE(out.order, expectedOrder);

        QCOMPARE(out.artists.size(), 1);
        const SearchSuggestArtist &artist = out.artists.first();
        QCOMPARE(artist.id, qint64(1));
        QCOMPARE(artist.name, QStringLiteral("歌手甲"));
        QCOMPARE(artist.cover, QStringLiteral("http://p1.music.126.net/artist.jpg"));
        QCOMPARE(artist.alias, QStringList{QStringLiteral("Alias A")});

        QCOMPARE(out.songs.size(), 1);
        const SearchSuggestSong &song = out.songs.first();
        QCOMPARE(song.id, qint64(11));
        QCOMPARE(song.name, QStringLiteral("歌曲甲"));
        const QStringList songArtists{QStringLiteral("歌手甲"), QStringLiteral("歌手乙")};
        QCOMPARE(song.artists, songArtists);
        QCOMPARE(song.alias, QStringList{QStringLiteral("副标题")});
        QCOMPARE(song.albumId, qint64(21));
        QCOMPARE(song.albumName, QStringLiteral("专辑甲"));
        QCOMPARE(song.duration, qint64(215));

        QCOMPARE(out.albums.size(), 1);
        const SearchSuggestAlbum &album = out.albums.first();
        QCOMPARE(album.id, qint64(21));
        QCOMPARE(album.name, QStringLiteral("专辑甲"));
        QCOMPARE(album.artistId, qint64(1));
        QCOMPARE(album.artistName, QStringLiteral("歌手甲"));
        QCOMPARE(album.publishTime, qint64(1700000000000));
        QCOMPARE(album.size, qint64(10));

        QCOMPARE(out.playlists.size(), 1);
        const SearchSuggestPlaylist &playlist = out.playlists.first();
        QCOMPARE(playlist.id, qint64(31));
        QCOMPARE(playlist.name, QStringLiteral("歌单甲"));
        QCOMPARE(playlist.cover,
                 QStringLiteral("http://p1.music.126.net/playlist.jpg"));
        QCOMPARE(playlist.description, QStringLiteral("歌单简介"));
        QCOMPARE(playlist.trackCount, qint64(20));
        QCOMPARE(playlist.playCount, qint64(100));
        QCOMPARE(playlist.creatorId, qint64(41));
    }

    void emptyArraysParseAsEmpty()
    {
        QJsonObject result{{"artists", QJsonArray{}},
                           {"songs", QJsonArray{}},
                           {"albums", QJsonArray{}},
                           {"playlists", QJsonArray{}},
                           {"order", QJsonArray{}}};
        const QJsonObject root{{"code", 200}, {"result", result}};

        const auto parsed = parseSearchSuggestions(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QVERIFY(parsed.value->artists.isEmpty());
        QVERIFY(parsed.value->songs.isEmpty());
        QVERIFY(parsed.value->albums.isEmpty());
        QVERIFY(parsed.value->playlists.isEmpty());
        QVERIFY(parsed.value->order.isEmpty());
    }

    void emptyResultObjectIsSuccess()
    {
        // The live endpoint returns result:{} when a query has no suggestions.
        const QJsonObject root{{"code", 200}, {"result", QJsonObject{}}};

        const auto parsed = parseSearchSuggestions(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QVERIFY(parsed.value->artists.isEmpty());
        QVERIFY(parsed.value->songs.isEmpty());
        QVERIFY(parsed.value->albums.isEmpty());
        QVERIFY(parsed.value->playlists.isEmpty());
        QVERIFY(parsed.value->order.isEmpty());
    }

    void nonObjectItemIsParseError()
    {
        const QJsonObject result{{"songs", QJsonArray{QJsonValue(7)}}};
        const QJsonObject root{{"code", 200}, {"result", result}};

        const auto parsed = parseSearchSuggestions(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/search/suggest/web"));
        QCOMPARE(parsed.error.path, QStringLiteral("searchSuggest.result.songs[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void missingResultIsParseError()
    {
        const auto parsed = parseSearchSuggestions(QJsonObject{{"code", 200}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/search/suggest/web"));
        QCOMPARE(parsed.error.path, QStringLiteral("searchSuggest.result"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("missing object")));
    }

    void nonObjectResultIsParseError()
    {
        const QJsonObject root{{"code", 200}, {"result", QJsonArray{}}};

        const auto parsed = parseSearchSuggestions(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchSuggest.result"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void malformedFieldIsParseError()
    {
        QJsonObject artist = artistObject();
        artist.insert(QStringLiteral("id"), QStringLiteral("1"));
        const QJsonObject result{{"artists", QJsonArray{artist}}};
        const QJsonObject root{{"code", 200}, {"result", result}};

        const auto parsed = parseSearchSuggestions(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/search/suggest/web"));
        QCOMPARE(parsed.error.path, QStringLiteral("searchSuggest.result.artists[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected number")));
    }

    void nonArrayCategoryIsParseError()
    {
        const QJsonObject result{{"albums", QJsonObject{}}};
        const QJsonObject root{{"code", 200}, {"result", result}};

        const auto parsed = parseSearchSuggestions(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchSuggest.result.albums"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }
};

QTEST_GUILESS_MAIN(SdkSearchSuggestTest)
#include "sdk_search_suggest_test.moc"
