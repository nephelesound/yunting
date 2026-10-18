// Typed Song Wiki SDK contract: parse a representative multi-block response,
// reject a malformed wiki root, and freeze the frontend-facing map keys.
#include "sdk/song_wiki.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkSongWikiTest : public QObject {
    Q_OBJECT

    static QJsonObject fixture()
    {
        QJsonArray basicRows;
        basicRows.append(QJsonObject{{"title", "语种"}, {"content", "日语"}});
        basicRows.append(QJsonObject{
            {"title", "曲风"},
            {"wikiSubMetaVos", QJsonArray{
                QJsonObject{{"text", "原声带"}},
                QJsonObject{{"text", "动画"}}}}});

        QJsonObject basicInfo;
        basicInfo.insert("wikiSubElementVos", basicRows);

        QJsonObject mv;
        mv.insert("title", "MV 名");
        mv.insert("artists", "歌手");
        mv.insert("imgUrl", "http://cover");
        mv.insert("duration", 221000);
        mv.insert("publishTime", "2022-08-03");
        mv.insert("playNum", 2750);
        mv.insert("jumpUrl", "orpheus://mv/123");
        QJsonObject mvInfo;
        mvInfo.insert("wikiSubElementVos", QJsonArray{mv});

        QJsonObject review;
        review.insert("content", "乐评正文");
        review.insert("commenter", "评论者");
        review.insert("likes", 7);
        review.insert("typeName", "精选乐评");
        QJsonObject reviewInfo;
        reviewInfo.insert("wikiSubElementVos", QJsonArray{review});

        QJsonObject sheet;
        sheet.insert("id", 9);
        sheet.insert("name", "吉他谱");
        sheet.insert("difficulty", "简单");
        sheet.insert("musicKey", "B");
        sheet.insert("bpm", 89);
        sheet.insert("totalPageSize", 12);
        sheet.insert("type", QJsonArray{QJsonObject{{"name", "吉他"}}});
        QJsonObject sheetInfo;
        sheetInfo.insert("wikiSubElementVos", QJsonArray{sheet});

        QJsonObject award;
        award.insert("title", "第1届测试奖");
        award.insert("subTitle", "最佳歌曲");
        award.insert("content", "获");
        award.insert("imgUrl", "http://award");
        award.insert("jumpUrl", "https://award");
        QJsonObject awardInfo;
        awardInfo.insert("wikiSubElementVos", QJsonArray{award});

        QJsonArray subBlocks;
        subBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockSongInfoVo"},
            {"blockInfo", QJsonObject{{"desc", "百科简介"}}}});
        subBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockBaseInfoVo"}, {"blockInfo", basicInfo}});
        subBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockMvInfoVo"}, {"blockInfo", mvInfo}});
        subBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockSongCommentVo"}, {"blockInfo", reviewInfo}});
        subBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockSheetInfoVo"}, {"blockInfo", sheetInfo}});
        subBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockAwardRecordVo"}, {"blockInfo", awardInfo}});

        QJsonObject detail;
        detail.insert("songName", "歌曲");
        detail.insert("albumName", "专辑");
        detail.insert("artistName", "歌手");
        detail.insert("coverImage", "http://cover");
        detail.insert("blocks", subBlocks);

        QJsonObject firstListen;
        firstListen.insert("period", "深夜");
        firstListen.insert("season", "仲夏");
        firstListen.insert("time", "01:25");
        firstListen.insert("timestamp", 1234);
        QJsonObject totalPlay;
        totalPlay.insert("playCount", 99);
        totalPlay.insert("text", "累计");
        QJsonObject memory;
        memory.insert("firstListen", firstListen);
        memory.insert("totalPlay", totalPlay);

        QJsonArray topBlocks;
        topBlocks.append(QJsonObject{{"rnData", memory}});
        topBlocks.append(QJsonObject{{"rnData", detail}});

        QJsonObject data;
        data.insert("blocks", topBlocks);
        QJsonObject root;
        root.insert("code", 200);
        root.insert("data", data);
        return root;
    }

    static QJsonObject creators()
    {
        QJsonObject person;
        person.insert("artistId", 42);
        person.insert("artistName", "作者");
        person.insert("artistPic", "http://creator");
        person.insert("isSub", true);

        QJsonObject role;
        role.insert("roleName", "作词");
        role.insert("creatorMetaVOS", QJsonArray{person});

        QJsonObject data;
        data.insert("songCreatorsRoleVos", QJsonArray{role});
        QJsonObject root;
        root.insert("code", 200);
        root.insert("data", data);
        return root;
    }

    static QJsonObject chorus()
    {
        QJsonObject item;
        item.insert("id", 7);
        item.insert("startTime", 42402);
        item.insert("endTime", 80866);
        item.insert("ugcLocked", 0);
        QJsonObject root;
        root.insert("code", 200);
        root.insert("chorus", QJsonArray{item});
        return root;
    }

private slots:
    void parsesTypedBlocks()
    {
        const Result<SongWiki> result = SongWiki::fromApi(fixture(), creators(), chorus(), 7);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const SongWiki &wiki = *result.value;
        QCOMPARE(wiki.songId, qint64(7));
        QCOMPARE(wiki.songName, QStringLiteral("歌曲"));
        QCOMPARE(wiki.description, QStringLiteral("百科简介"));
        QCOMPARE(wiki.basic.size(), 2);
        QCOMPARE(wiki.basic.at(1).tags, QStringList({QStringLiteral("原声带"), QStringLiteral("动画")}));
        QCOMPARE(wiki.creators.size(), 1);
        QCOMPARE(wiki.creators.first().people.first().artistId, qint64(42));
        QCOMPARE(wiki.mvs.first().id, qint64(123));
        QCOMPARE(wiki.mvs.first().duration, qint64(221));
        QCOMPARE(wiki.reviews.first().likes, qint64(7));
        QCOMPARE(wiki.sheets.first().types, QStringList{QStringLiteral("吉他")});
        QCOMPARE(wiki.awards.first().title, QStringLiteral("第1届测试奖"));
        QCOMPARE(wiki.awards.first().awardName, QStringLiteral("最佳歌曲"));
        QCOMPARE(wiki.awards.first().result, QStringLiteral("获"));
        QCOMPARE(wiki.memory.playCount, qint64(99));
        QCOMPARE(wiki.chorus.start, 42.402);
        QVERIFY(wiki.chorus.valid());
        QCOMPARE(wiki.empty, false);
    }

    void missingBlocksIsAParseError()
    {
        const Result<SongWiki> result = SongWiki::fromApi(
            QJsonObject{{"code", 200}, {"data", QJsonObject{}}}, {}, {}, 7);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("wiki.data.blocks"));
        QVERIFY(result.error.endpoint.contains(QStringLiteral("link/page")));
    }

    void optionalCreatorsMayBeAbsent()
    {
        const Result<SongWiki> result = SongWiki::fromApi(fixture(), {}, {}, 7);
        QVERIFY(result.ok());
        QVERIFY(result.value->creators.isEmpty());
    }

    void numericLockedAndZeroChorusStart()
    {
        const QJsonObject chorusRoot{
            {"code", 200},
            {"chorus", QJsonArray{QJsonObject{{"id", 7}, {"startTime", 0},
                                               {"endTime", 1000}, {"ugcLocked", 1}}}}};
        const auto result = SongWiki::fromApi(fixture(), {}, chorusRoot, 7);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->chorus.valid());
        QCOMPARE(result.value->chorus.start, 0.0);
        QCOMPARE(result.value->chorus.locked, true);
    }

    void shellBlocksAreEmpty()
    {
        QJsonArray blocks;
        blocks.append(QJsonObject{{"rnData", QJsonObject{{"simSongs", QJsonArray{}}}}});
        blocks.append(QJsonObject{{"rnData", QJsonObject{{"simPlaylist", QJsonArray{}}}}});
        QJsonObject data;
        data.insert("blocks", blocks);
        QJsonObject root;
        root.insert("code", 200);
        root.insert("data", data);
        const auto result = SongWiki::fromApi(root, {}, {}, 7);
        QVERIFY(result.ok());
        QCOMPARE(result.value->empty, true);
    }

    // 前端当前仍消费 QVariantMap；字段名是兼容契约，不能悄悄改名。
    void frontendMapHasStableKeys()
    {
        const auto result = SongWiki::fromApi(fixture(), creators(), chorus(), 7);
        QVERIFY(result.ok());
        const QVariantMap map = result.value->toVariantMap();
        for (const char *key : {"songId", "loaded", "empty", "songName", "albumName",
                                "artistName", "cover", "description", "memory", "chorus",
                                "basic", "creators", "mvs", "reviews", "sheets",
                                "entertainments", "awards"})
            QVERIFY(map.contains(QLatin1String(key)));
        QCOMPARE(map.value("loaded").toBool(), true);
        QCOMPARE(map.value("mvs").toList().first().toMap().value("id").toLongLong(), 123);
        QCOMPARE(map.value("chorus").toMap().value("start").toDouble(), 42.402);
    }
};

QTEST_GUILESS_MAIN(SdkSongWikiTest)
#include "sdk_song_wiki_test.moc"
