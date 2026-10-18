// Typed Discovery SDK contract: songs, albums, playlists and tags.
#include "sdk/discovery.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkDiscoveryTest : public QObject {
    Q_OBJECT

private slots:
    void parsesNewSongs()
    {
        QJsonObject artist{{"id", 3}, {"name", "歌手"}};
        QJsonObject album{{"id", 4}, {"name", "专辑"}, {"picUrl", "http://cover"}};
        QJsonObject song{{"id", 1}, {"name", "新歌"}, {"duration", 210500},
                         {"fee", 1}, {"artists", QJsonArray{artist}}, {"album", album}};
        QJsonObject item{{"id", 1}, {"picUrl", "http://fallback"}, {"song", song}};
        QJsonObject root{{"code", 200}, {"result", QJsonArray{item}}};

        const auto result = parseNewSongs(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 1);
        const NewSong &out = result.value->first();
        QCOMPARE(out.songId, qint64(1));
        QCOMPARE(out.name, QStringLiteral("新歌"));
        QCOMPARE(out.artists, QStringList{QStringLiteral("歌手")});
        QCOMPARE(out.albumId, qint64(4));
        QCOMPARE(out.cover, QStringLiteral("http://cover"));
        QCOMPARE(out.duration, qint64(210));
        QCOMPARE(out.fee, 1);
    }

    void parsesAlbums()
    {
        QJsonObject artist{{"id", 8}, {"name", "主歌手"}};
        QJsonObject album{{"id", 2}, {"name", "新碟"}, {"picUrl", "http://album"},
                          {"publishTime", 1700000000000.0}, {"artist", artist},
                          {"artists", QJsonArray{artist}}, {"size", 10}};
        QJsonObject root{{"code", 200}, {"albums", QJsonArray{album}}};

        const auto result = parseNewAlbums(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->first().id, qint64(2));
        QCOMPARE(result.value->first().artistName, QStringLiteral("主歌手"));
        QCOMPARE(result.value->first().publishTime, qint64(1700000000000));
        QCOMPARE(result.value->first().size, qint64(10));
    }

    void parsesPlaylistPage()
    {
        QJsonObject creator{{"userId", 9}, {"nickname", "歌单主"}};
        QJsonObject playlist{{"id", 5}, {"name", "精品"}, {"coverImgUrl", "http://p"},
                             {"description", "desc"}, {"trackCount", 12},
                             {"playCount", 34}, {"subscribedCount", 56},
                             {"creator", creator}, {"updateTime", 1234}};
        QJsonObject root{{"code", 200}, {"more", true}, {"lasttime", 777},
                         {"playlists", QJsonArray{playlist}}};

        const auto result = parseHighQualityPlaylists(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->more, true);
        QCOMPARE(result.value->lasttime, qint64(777));
        const HighQualityPlaylist &out = result.value->items.first();
        QCOMPARE(out.id, qint64(5));
        QCOMPARE(out.creatorId, qint64(9));
        QCOMPARE(out.creatorName, QStringLiteral("歌单主"));
        QCOMPARE(out.trackCount, qint64(12));
    }

    void parsesTags()
    {
        QJsonObject tag{{"id", 1}, {"name", "华语"}, {"type", 0},
                        {"category", 1}, {"hot", true}};
        QJsonObject root{{"code", 200}, {"tags", QJsonArray{tag}}};
        const auto result = parseHighQualityTags(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->first().name, QStringLiteral("华语"));
        QCOMPARE(result.value->first().hot, true);
    }

    void parsesBanners()
    {
        QJsonObject banner{{"typeTitle", "新碟"}, {"imageUrl", "http://banner"},
                           {"url", "orpheus://album/1"}, {"targetType", 10},
                           {"targetId", 1}};
        const auto result = parseBanners(QJsonObject{{"code", 200},
                                                     {"banners", QJsonArray{banner}}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 1);
        QCOMPARE(result.value->first().targetType, 10);
        QCOMPARE(result.value->first().targetId, qint64(1));
    }

    void missingArrayIsParseError()
    {
        const auto result = parseNewSongs(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QVERIFY(result.error.message.contains(QStringLiteral("result")));
    }
};

QTEST_GUILESS_MAIN(SdkDiscoveryTest)
#include "sdk_discovery_test.moc"
