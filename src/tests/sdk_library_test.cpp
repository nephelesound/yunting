// Typed Library/Subscription SDK contract.
#include "sdk/library.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class SdkLibraryTest : public QObject {
    Q_OBJECT

    static QJsonObject artistObject()
    {
        return QJsonObject{
            {"info", "新碟上架"},
            {"id", 17028},
            {"name", "水樹奈々"},
            {"trans", "水树奈奈"},
            {"alias", QJsonArray{"Mizuki Nana"}},
            {"albumSize", 89},
            {"mvSize", 3},
            {"picUrl", "https://p1.music.126.net/artist.jpg"},
            {"img1v1Url", "https://p2.music.126.net/artist.jpg"},
        };
    }

    static QJsonObject albumArtistObject()
    {
        return QJsonObject{{"id", 17028}, {"name", "水樹奈々"}};
    }

    static QJsonObject albumObject()
    {
        return QJsonObject{
            {"id", 81347636},
            {"name", "戦姫絶唱シンフォギアXV キャラクターソング6"},
            {"picUrl", "https://p1.music.126.net/album.jpg"},
            {"artists", QJsonArray{albumArtistObject(),
                                   QJsonObject{{"id", 42}, {"name", "合唱"}}}},
            {"size", 4},
            {"subTime", 1568863447079.0},
            {"alias", QJsonArray{"角色歌6"}},
            {"msg", QJsonArray{}},
            {"transNames", QJsonArray{}},
        };
    }

    static QJsonObject mvObject(const QString &vid)
    {
        return QJsonObject{
            {"type", 0},
            {"title", "你的微笑"},
            {"durationms", 265000},
            {"creator", QJsonArray{QJsonObject{{"userId", 1001},
                                               {"userName", "示例艺人"}}}},
            {"playTime", 782783},
            {"coverUrl", "https://p1.music.126.net/mv.jpg"},
            {"vid", vid},
        };
    }

private slots:
    void parsesArtistPage()
    {
        const QJsonObject root{{"code", 200}, {"count", 2}, {"hasMore", true},
                               {"data", QJsonArray{artistObject()}}};
        const auto result = parseSubscribedArtists(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));

        const SubscribedArtistPage &page = *result.value;
        QCOMPARE(page.count, qint64(2));
        QCOMPARE(page.hasMore, true);
        QCOMPARE(page.offset, 0);
        QCOMPARE(page.items.size(), 1);

        const SubscribedArtist &artist = page.items.first();
        QCOMPARE(artist.id, qint64(17028));
        QCOMPARE(artist.name, QStringLiteral("水樹奈々"));
        QCOMPARE(artist.trans, QStringLiteral("水树奈奈"));
        QCOMPARE(artist.cover, QStringLiteral("https://p1.music.126.net/artist.jpg"));
        QCOMPARE(artist.alias, QStringList{QStringLiteral("Mizuki Nana")});
        QCOMPARE(artist.albumSize, qint64(89));
        QCOMPARE(artist.mvSize, qint64(3));
        QCOMPARE(artist.latestInfo, QStringLiteral("新碟上架"));
    }

    void parsesAlbumPage()
    {
        const QJsonObject root{{"code", 200}, {"count", 1}, {"paidCount", 1},
                               {"hasMore", false}, {"data", QJsonArray{albumObject()}}};
        const auto result = parseSubscribedAlbums(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));

        const SubscribedAlbumPage &page = *result.value;
        QCOMPARE(page.count, qint64(1));
        QCOMPARE(page.paidCount, qint64(1));
        QCOMPARE(page.hasMore, false);
        QCOMPARE(page.items.size(), 1);

        const SubscribedAlbum &album = page.items.first();
        QCOMPARE(album.id, qint64(81347636));
        QCOMPARE(album.cover, QStringLiteral("https://p1.music.126.net/album.jpg"));
        QCOMPARE(album.artistId, qint64(17028));
        QCOMPARE(album.artistName, QStringLiteral("水樹奈々"));
        const QStringList expectedArtists{QStringLiteral("水樹奈々"),
                                          QStringLiteral("合唱")};
        QCOMPARE(album.artists, expectedArtists);
        QCOMPARE(album.size, qint64(4));
        QCOMPARE(album.subTime, qint64(1568863447079LL));
        QCOMPARE(album.alias, QStringList{QStringLiteral("角色歌6")});
    }

    void parsesMvPage()
    {
        const QJsonObject root{{"code", 200}, {"count", 2}, {"hasMore", true},
                               {"data", QJsonArray{mvObject(QStringLiteral("5307442")),
                                                    mvObject(QStringLiteral(
                                                        "4E673ABCA5B939B889FDE4A2B148C2BD"))}}};
        const auto result = parseSubscribedMvs(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));

        const SubscribedMvPage &page = *result.value;
        QCOMPARE(page.count, qint64(2));
        QCOMPARE(page.hasMore, true);
        QCOMPARE(page.items.size(), 2);

        const SubscribedMv &mv = page.items.first();
        QCOMPARE(mv.id, QStringLiteral("5307442"));
        QCOMPARE(mv.type, 0);
        QCOMPARE(mv.name, QStringLiteral("你的微笑"));
        QCOMPARE(mv.cover, QStringLiteral("https://p1.music.126.net/mv.jpg"));
        QCOMPARE(mv.duration, qint64(265));
        QCOMPARE(mv.playCount, qint64(782783));
        QCOMPARE(mv.creatorId, qint64(1001));
        QCOMPARE(mv.creatorName, QStringLiteral("示例艺人"));
        QCOMPARE(page.items.at(1).id,
                 QStringLiteral("4E673ABCA5B939B889FDE4A2B148C2BD"));
    }

    void emptyDataIsEmptyPage()
    {
        const QJsonObject root{{"code", 200}, {"count", 0}, {"hasMore", false},
                               {"data", QJsonArray{}}};

        const auto artists = parseSubscribedArtists(root);
        QVERIFY2(artists.ok(), qPrintable(artists.error.message));
        QCOMPARE(artists.value->items.size(), 0);
        QCOMPARE(artists.value->count, qint64(0));
        QCOMPARE(artists.value->hasMore, false);

        const auto albums = parseSubscribedAlbums(root);
        QVERIFY2(albums.ok(), qPrintable(albums.error.message));
        QCOMPARE(albums.value->items.size(), 0);
        QCOMPARE(albums.value->paidCount, qint64(0));

        const auto mvs = parseSubscribedMvs(root);
        QVERIFY2(mvs.ok(), qPrintable(mvs.error.message));
        QCOMPARE(mvs.value->items.size(), 0);
    }

    void nullDataIsEmptyPage()
    {
        const QJsonObject root{{"code", 200}, {"count", 0}, {"hasMore", false},
                               {"data", QJsonValue::Null}};

        const auto artists = parseSubscribedArtists(root);
        QVERIFY2(artists.ok(), qPrintable(artists.error.message));
        QVERIFY(artists.value->items.isEmpty());

        const auto albums = parseSubscribedAlbums(root);
        QVERIFY2(albums.ok(), qPrintable(albums.error.message));
        QVERIFY(albums.value->items.isEmpty());

        const auto mvs = parseSubscribedMvs(root);
        QVERIFY2(mvs.ok(), qPrintable(mvs.error.message));
        QVERIFY(mvs.value->items.isEmpty());
    }

    void missingDataIsParseError()
    {
        const QJsonObject root{{"code", 200}};

        const auto artists = parseSubscribedArtists(root);
        QVERIFY(!artists.ok());
        QCOMPARE(artists.error.kind, ErrorKind::Parse);
        QCOMPARE(artists.error.endpoint, QStringLiteral("artist/sublist"));
        QCOMPARE(artists.error.path, QStringLiteral("library.artists.data"));
        QVERIFY(artists.error.message.contains(QStringLiteral("missing array")));

        const auto albums = parseSubscribedAlbums(root);
        QVERIFY(!albums.ok());
        QCOMPARE(albums.error.kind, ErrorKind::Parse);
        QCOMPARE(albums.error.endpoint, QStringLiteral("album/sublist"));
        QCOMPARE(albums.error.path, QStringLiteral("library.albums.data"));

        const auto mvs = parseSubscribedMvs(root);
        QVERIFY(!mvs.ok());
        QCOMPARE(mvs.error.kind, ErrorKind::Parse);
        QCOMPARE(mvs.error.endpoint,
                 QStringLiteral("cloudvideo/allvideo/sublist"));
        QCOMPARE(mvs.error.path, QStringLiteral("library.mvs.data"));
    }

    void nonObjectItemIsParseError()
    {
        const QJsonObject root{{"code", 200},
                               {"data", QJsonArray{QJsonValue(7)}}};

        const auto artists = parseSubscribedArtists(root);
        QVERIFY(!artists.ok());
        QCOMPARE(artists.error.kind, ErrorKind::Parse);
        QCOMPARE(artists.error.path, QStringLiteral("library.artists.data[0]"));
        QVERIFY(artists.error.message.contains(QStringLiteral("expected object")));

        const auto albums = parseSubscribedAlbums(root);
        QVERIFY(!albums.ok());
        QCOMPARE(albums.error.path, QStringLiteral("library.albums.data[0]"));

        const auto mvs = parseSubscribedMvs(root);
        QVERIFY(!mvs.ok());
        QCOMPARE(mvs.error.path, QStringLiteral("library.mvs.data[0]"));
    }

    void wrongFieldTypeIsParseError()
    {
        QJsonObject artist = artistObject();
        artist.insert(QStringLiteral("id"), QStringLiteral("17028"));
        const QJsonObject root{{"code", 200}, {"data", QJsonArray{artist}}};

        const auto result = parseSubscribedArtists(root);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("artist/sublist"));
        QCOMPARE(result.error.path, QStringLiteral("library.artists.data[0]"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected number")));
    }

    void missingOptionalItemFieldsUseDefaults()
    {
        const QJsonObject root{{"code", 200},
                               {"data", QJsonArray{QJsonObject{{"id", 5}}}}};
        const auto artistResult = parseSubscribedArtists(root);
        QVERIFY2(artistResult.ok(), qPrintable(artistResult.error.message));
        QCOMPARE(artistResult.value->count, qint64(0));
        QCOMPARE(artistResult.value->hasMore, false);
        const SubscribedArtist &artist = artistResult.value->items.first();
        QCOMPARE(artist.id, qint64(5));
        QVERIFY(artist.name.isEmpty());
        QVERIFY(artist.cover.isEmpty());
        QVERIFY(artist.alias.isEmpty());
        QCOMPARE(artist.albumSize, qint64(0));
        QCOMPARE(artist.mvSize, qint64(0));

        const QJsonObject mvRoot{{"code", 200},
                                 {"data", QJsonArray{QJsonObject{{"vid", "abc"}}}}};
        const auto mvResult = parseSubscribedMvs(mvRoot);
        QVERIFY2(mvResult.ok(), qPrintable(mvResult.error.message));
        const SubscribedMv &mv = mvResult.value->items.first();
        QCOMPARE(mv.id, QStringLiteral("abc"));
        QCOMPARE(mv.duration, qint64(0));
        QCOMPARE(mv.playCount, qint64(0));
        QCOMPARE(mv.creatorId, qint64(0));
        QVERIFY(mv.creatorName.isEmpty());
    }

    void artistCoverFallsBackToImg1v1Url()
    {
        const QJsonObject item{{"id", 9},
                               {"img1v1Url", "https://p2.music.126.net/avatar.jpg"}};
        const QJsonObject root{{"code", 200}, {"data", QJsonArray{item}}};
        const auto result = parseSubscribedArtists(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->items.first().cover,
                 QStringLiteral("https://p2.music.126.net/avatar.jpg"));
    }

    void missingMvIdIsParseError()
    {
        const QJsonObject root{{"code", 200},
                               {"data", QJsonArray{QJsonObject{{"title", "无 id"}}}}};
        const auto result = parseSubscribedMvs(root);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("library.mvs.data[0]"));
        QVERIFY(result.error.message.contains(QStringLiteral("vid")));
    }
};

QTEST_GUILESS_MAIN(SdkLibraryTest)
#include "sdk_library_test.moc"
