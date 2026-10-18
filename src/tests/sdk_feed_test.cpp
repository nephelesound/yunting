// Typed Feed SDK contract: personalized MVs, private content and homepage blocks.
#include "sdk/feed.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class SdkFeedTest : public QObject {
    Q_OBJECT

    static QJsonObject mvObject()
    {
        return QJsonObject{
            {"id", 10973299},
            {"type", 5},
            {"name", "Mute"},
            {"copywriter", "编辑推荐：孟美岐全新个人EP主打歌"},
            {"picUrl", "http://cover/mv.jpg"},
            {"duration", 184000},
            {"playCount", 3348950},
            {"artistId", 12098023},
            {"artistName", "孟美岐"},
            {"artists", QJsonArray{QJsonObject{{"id", 12098023}, {"name", "孟美岐"}}}},
            {"alg", "featured"},
            {"canDislike", false},
        };
    }

    static QJsonObject privateContentObject()
    {
        return QJsonObject{
            {"id", 14514232},
            {"type", 5},
            {"name", "《超级面对面》第254期 keshi"},
            {"copywriter", "《超级面对面》第254期 keshi"},
            {"picUrl", "http://cover/private.jpg"},
            {"sPicUrl", "http://cover/private-small.jpg"},
            {"url", "orpheus://private/14514232"},
            {"alg", "featured"},
        };
    }

    static QJsonObject bannerBlock()
    {
        const QJsonObject banner{
            {"bannerId", "1717750651205666"},
            {"typeTitle", "独家策划"},
            {"mainTitle", QJsonValue(QJsonValue::Null)},
            {"pic", "http://cover/banner.jpg"},
            {"url", "https://example.test/banner"},
            {"targetType", 3000},
            {"targetId", 0},
            {"alg", "banner-feature"},
        };
        return QJsonObject{
            {"blockCode", "HOMEPAGE_BANNER"},
            {"showType", "BANNER"},
            {"extInfo", QJsonObject{{"banners", QJsonArray{banner}}}},
        };
    }

    static QJsonObject playlistCardObject()
    {
        return QJsonObject{
            {"resourceId", "17837791376"},
            {"resourceType", "list"},
            {"action", "orpheus://playlist/17837791376"},
            {"uiElement", QJsonObject{
                 {"mainTitle", QJsonObject{{"title", "私人雷达"}}},
                 {"subTitle", QJsonObject{{"title", "为你推荐"}}},
                 {"image", QJsonObject{{"imageUrl", "http://cover/playlist.jpg"}}},
                 {"labelTexts", QJsonArray{QStringLiteral("流行"), QStringLiteral("夜晚")}},
             }},
            {"resourceExtInfo", QJsonObject{
                 {"playCount", 4899152},
                 {"highQuality", false},
                 {"hasListened", false},
                 {"specialType", 0},
             }},
        };
    }

    static QJsonObject playlistBlock()
    {
        const QJsonObject creative{
            {"creativeType", "list"},
            {"uiElement", QJsonObject{{"mainTitle", QJsonObject{{"title", "歌单推荐"}}}}},
            {"resources", QJsonArray{playlistCardObject()}},
        };
        return QJsonObject{
            {"blockCode", "HOMEPAGE_BLOCK_PLAYLIST_RCMD"},
            {"showType", "HOMEPAGE_SLIDE_PLAYLIST"},
            {"creatives", QJsonArray{creative}},
        };
    }

    static QJsonObject newSongBlock()
    {
        const QJsonObject resource{
            {"resourceId", "3447033086"},
            {"resourceType", "song"},
            {"action", "orpheus://song/3447033086"},
            {"uiElement", QJsonObject{
                 {"mainTitle", QJsonObject{{"title", "Thanks For The Lesson"}}},
                 {"image", QJsonObject{{"imageUrl", "http://cover/song.jpg"}}},
             }},
            {"resourceExtInfo", QJsonObject{
                 {"playCount", 1234},
                 {"songData", QJsonObject{
                      {"artists", QJsonArray{
                           QJsonObject{{"name", "Rosy赵露思"}},
                           QJsonObject{{"name", "Bia"}},
                       }},
                  }},
             }},
        };
        const QJsonObject creative{
            {"creativeType", "NEW_SONG_HOMEPAGE"},
            {"uiElement", QJsonObject{{"mainTitle", QJsonObject{{"title", "新歌"}}}}},
            {"resources", QJsonArray{resource}},
        };
        return QJsonObject{
            {"blockCode", "HOMEPAGE_BLOCK_NEW_ALBUM_NEW_SONG"},
            {"showType", "HOMEPAGE_NEW_SONG_NEW_ALBUM"},
            {"creatives", QJsonArray{creative}},
        };
    }

    static QJsonObject unknownBlock()
    {
        return QJsonObject{
            {"blockCode", "HOMEPAGE_VOICEBOOK_RCMD"},
            {"showType", "SLIDE_RCMDLIKE_VOICELIST"},
            {"dslData", QJsonObject{{"items", QJsonArray{}}}},
        };
    }

    static QJsonObject commentBlock()
    {
        const QJsonObject resource{
            {"resourceId", "52991590"},
            {"resourceType", "orpheus"},
            {"uiElement", QJsonObject{
                 {"mainTitle", QJsonObject{
                      {"title", ""},
                      {"titleDesc", "人生何处不相逢"},
                  }},
                 {"image", QJsonObject{{"imageUrl", "http://cover/comment.jpg"}}},
             }},
            {"resourceExtInfo", QJsonObject{{"likedCount", 5952}, {"replyCount", 54}}},
        };
        const QJsonObject creative{
            {"creativeType", "COMMENT_DETAIL"},
            {"resources", QJsonArray{resource}},
        };
        return QJsonObject{
            {"blockCode", "HOMEPAGE_BLOCK_NEW_HOT_COMMENT"},
            {"showType", "NEW_HOT_COMMENT_WHITE_SKIN"},
            {"creatives", QJsonArray{creative}},
        };
    }

    static QJsonObject homepageRoot()
    {
        return QJsonObject{
            {"code", 200},
            {"data", QJsonObject{
                 {"cursor", "next-cursor"},
                 {"hasMore", true},
                 {"blocks", QJsonArray{
                      bannerBlock(),
                      playlistBlock(),
                      newSongBlock(),
                      unknownBlock(),
                  }},
             }},
        };
    }

private slots:
    void parsesPersonalizedMvs()
    {
        const auto result = parsePersonalizedMvs(
            QJsonObject{{"code", 200}, {"category", 3},
                        {"result", QJsonArray{mvObject()}}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 1);
        const PersonalizedMv &mv = result.value->first();
        QCOMPARE(mv.id, qint64(10973299));
        QCOMPARE(mv.type, 5);
        QCOMPARE(mv.name, QStringLiteral("Mute"));
        QCOMPARE(mv.cover, QStringLiteral("http://cover/mv.jpg"));
        QCOMPARE(mv.duration, qint64(184));
        QCOMPARE(mv.playCount, qint64(3348950));
        QCOMPARE(mv.artistId, qint64(12098023));
        QCOMPARE(mv.artists, QStringList{QStringLiteral("孟美岐")});
        QCOMPARE(mv.alg, QStringLiteral("featured"));
    }

    void personalizedMvMissingResultIsParseError()
    {
        const auto result = parsePersonalizedMvs(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/personalized/mv"));
        QCOMPARE(result.error.path, QStringLiteral("result"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing array")));
    }

    void personalizedMvMalformedItemReportsEndpointAndPath()
    {
        const auto result = parsePersonalizedMvs(
            QJsonObject{{"code", 200},
                        {"result", QJsonArray{QJsonObject{{"id", "not-a-number"}}}}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/personalized/mv"));
        QCOMPARE(result.error.path, QStringLiteral("result[0]"));
        QVERIFY(result.error.message.contains(QStringLiteral("result[0].id")));
    }

    void parsesPrivateContents()
    {
        const auto result = parsePrivateContents(
            QJsonObject{{"code", 200}, {"name", "独家放送"},
                        {"result", QJsonArray{privateContentObject()}}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 1);
        const PrivateContent &content = result.value->first();
        QCOMPARE(content.id, qint64(14514232));
        QCOMPARE(content.type, 5);
        QCOMPARE(content.name, QStringLiteral("《超级面对面》第254期 keshi"));
        QCOMPARE(content.cover, QStringLiteral("http://cover/private.jpg"));
        QCOMPARE(content.smallCover, QStringLiteral("http://cover/private-small.jpg"));
        QCOMPARE(content.url, QStringLiteral("orpheus://private/14514232"));
    }

    void privateContentMissingResultIsParseError()
    {
        const auto result = parsePrivateContents(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/personalized/privatecontent"));
        QCOMPARE(result.error.path, QStringLiteral("result"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing array")));
    }

    void nonObjectResultIsParseError()
    {
        const auto result = parsePersonalizedMvs(
            QJsonObject{{"code", 200}, {"result", QJsonArray{42}}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("result[0]"));
    }

    void parsesHomepageBlockPage()
    {
        const auto result = parseHomepageBlockPage(homepageRoot());
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const HomepageBlockPage &page = *result.value;
        QCOMPARE(page.cursor, QStringLiteral("next-cursor"));
        QCOMPARE(page.hasMore, true);
        QCOMPARE(page.blocks.size(), 4);

        const HomepageBlock &bannerGroup = page.blocks.at(0);
        QCOMPARE(bannerGroup.blockCode, QStringLiteral("HOMEPAGE_BANNER"));
        QCOMPARE(bannerGroup.showType, QStringLiteral("BANNER"));
        QCOMPARE(bannerGroup.cards.size(), 1);
        const HomepageCard &banner = bannerGroup.cards.first();
        QCOMPARE(banner.id, qint64(1717750651205666));
        QCOMPARE(banner.type, QStringLiteral("banner"));
        QCOMPARE(banner.title, QStringLiteral("独家策划"));
        QCOMPARE(banner.subtitle, QString());
        QCOMPARE(banner.cover, QStringLiteral("http://cover/banner.jpg"));
        QCOMPARE(banner.action, QStringLiteral("https://example.test/banner"));

        const HomepageBlock &playlistGroup = page.blocks.at(1);
        QCOMPARE(playlistGroup.cards.size(), 1);
        const HomepageCard &playlist = playlistGroup.cards.first();
        QCOMPARE(playlist.section, QStringLiteral("歌单推荐"));
        QCOMPARE(playlist.id, qint64(17837791376));
        QCOMPARE(playlist.type, QStringLiteral("list"));
        QCOMPARE(playlist.title, QStringLiteral("私人雷达"));
        QCOMPARE(playlist.subtitle, QStringLiteral("为你推荐"));
        QCOMPARE(playlist.cover, QStringLiteral("http://cover/playlist.jpg"));
        QCOMPARE(playlist.action, QStringLiteral("orpheus://playlist/17837791376"));
        QCOMPARE(playlist.playCount, qint64(4899152));
        QCOMPARE(playlist.labels,
                 (QStringList{QStringLiteral("流行"), QStringLiteral("夜晚")}));

        const HomepageBlock &songGroup = page.blocks.at(2);
        QCOMPARE(songGroup.cards.size(), 1);
        const HomepageCard &song = songGroup.cards.first();
        QCOMPARE(song.section, QStringLiteral("新歌"));
        QCOMPARE(song.id, qint64(3447033086));
        QCOMPARE(song.type, QStringLiteral("song"));
        QCOMPARE(song.playCount, qint64(1234));
        QCOMPARE(song.artists,
                 (QStringList{QStringLiteral("Rosy赵露思"), QStringLiteral("Bia")}));

        QCOMPARE(page.blocks.at(3).blockCode, QStringLiteral("HOMEPAGE_VOICEBOOK_RCMD"));
        QVERIFY(page.blocks.at(3).cards.isEmpty());
    }

    void emptyCoreArraysAreValid()
    {
        const auto mvs = parsePersonalizedMvs(
            QJsonObject{{"code", 200}, {"result", QJsonArray{}}});
        QVERIFY2(mvs.ok(), qPrintable(mvs.error.message));
        QVERIFY(mvs.value->isEmpty());

        const auto contents = parsePrivateContents(
            QJsonObject{{"code", 200}, {"result", QJsonArray{}}});
        QVERIFY2(contents.ok(), qPrintable(contents.error.message));
        QVERIFY(contents.value->isEmpty());

        const auto page = parseHomepageBlockPage(
            QJsonObject{{"code", 200},
                        {"data", QJsonObject{{"blocks", QJsonArray{}}}}});
        QVERIFY2(page.ok(), qPrintable(page.error.message));
        QVERIFY(page.value->blocks.isEmpty());
        QCOMPARE(page.value->hasMore, false);
        QCOMPARE(page.value->cursor, QString());
    }

    void homepageCardTitleFallsBackToTitleDesc()
    {
        const auto result = parseHomepageBlockPage(
            QJsonObject{{"code", 200},
                        {"data", QJsonObject{
                             {"hasMore", false},
                             {"blocks", QJsonArray{commentBlock()}},
                         }}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->blocks.size(), 1);
        QCOMPARE(result.value->blocks.first().cards.size(), 1);
        const HomepageCard &card = result.value->blocks.first().cards.first();
        QCOMPARE(card.title, QStringLiteral("人生何处不相逢"));
        QCOMPARE(card.section, QString());
        QCOMPARE(card.cover, QStringLiteral("http://cover/comment.jpg"));
    }

    void homepageMissingDataIsParseError()
    {
        const auto result = parseHomepageBlockPage(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/homepage/block/page"));
        QCOMPARE(result.error.path, QStringLiteral("data"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing object")));
    }

    void homepageMissingBlocksIsParseError()
    {
        const auto result = parseHomepageBlockPage(
            QJsonObject{{"code", 200}, {"data", QJsonObject{{"hasMore", false}}}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/homepage/block/page"));
        QCOMPARE(result.error.path, QStringLiteral("data.blocks"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing array")));
    }

    void homepageMalformedBlocksIsParseError()
    {
        const auto result = parseHomepageBlockPage(
            QJsonObject{{"code", 200},
                        {"data", QJsonObject{{"blocks", QStringLiteral("not-an-array")}}}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/homepage/block/page"));
        QCOMPARE(result.error.path, QStringLiteral("data.blocks"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected array")));
    }
};

QTEST_GUILESS_MAIN(SdkFeedTest)
#include "sdk_feed_test.moc"
