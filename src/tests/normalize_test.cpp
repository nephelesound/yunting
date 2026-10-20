// 归一化形状测试：不碰网络，只锁新加的导航字段与 MV / 云盘形状。
#include "../normalize.h"
#include "../sdk/comment.h"

#include <QJsonObject>
#include <QTest>

class NormalizeTest : public QObject {
    Q_OBJECT
private slots:
    void mvSearchUsesSeconds()
    {
        const QJsonObject mv{{"id", 522365}, {"name", "美人鱼"},
                             {"cover", "http://p1.music.126.net/mv.jpg"},
                             {"artistName", "林俊杰"}, {"artistId", 3684},
                             {"duration", 249610}, {"playCount", 7938464}};
        const QVariantMap m = ncmSearchMv(mv);
        QCOMPARE(m.value("duration").toDouble(), 249);
        QCOMPARE(m.value("artistId").toDouble(), 3684);
        QCOMPARE(m.value("cover").toString(),
                 QStringLiteral("http://p1.music.126.net/mv.jpg?param=300y300"));
    }

    void cloudFallbackHasStableShape()
    {
        const QJsonObject item{{"songId", 7}, {"songName", "上传的歌"},
                               {"artist", "某人"}, {"album", "某专辑"}};
        const QVariantMap t = ncmCloudTrack(item);
        QCOMPARE(t.value("artistId").toDouble(), 0);
        QCOMPARE(t.value("albumId").toDouble(), 0);
        QCOMPARE(t.value("name").toString(), QStringLiteral("上传的歌"));
        QVERIFY(t.contains("quality"));
    }

    // 云盘条目始终可播（普通曲目的 privilege/fee 规则已迁到 sdk_track 测试）
    void cloudTrackAlwaysPlayable()
    {
        const QVariantMap cloud = ncmCloudTrack(QJsonObject{{"songId", 5}, {"songName", "上传的歌"},
                                                           {"fee", 4}});
        QCOMPARE(cloud.value("playable").toBool(), true);
        QCOMPARE(cloud.value("blockedBy").toString(), QString());
    }

    // 评论的边界转换：typed Comment → 现有 QML map；avatar 在这里缩成 96 方图，键集合不变
    void commentMapPreservesLegacyShape()
    {
        ncm::sdk::Comment comment;
        comment.user = QStringLiteral("u");
        comment.userId = 7;
        comment.avatar = QStringLiteral("http://cover/u.jpg");
        comment.content = QStringLiteral("hi");
        comment.likes = 3;
        comment.time = 10;
        comment.id = 42;
        comment.liked = true;
        comment.parentId = 1;
        comment.replyCount = 14;
        comment.vipLevel = QStringLiteral("svip");
        comment.beReplied.append(
            ncm::sdk::RepliedComment{9, QStringLiteral("old"), 8, QStringLiteral("v")});

        const QVariantMap commentMap = commentToVariantMap(comment);
        QCOMPARE(commentMap.keys(),
                 (QStringList{QStringLiteral("avatar"), QStringLiteral("avatarPendant"),
                              QStringLiteral("beReplied"), QStringLiteral("content"),
                              QStringLiteral("id"), QStringLiteral("identityIcon"),
                              QStringLiteral("liked"), QStringLiteral("likes"),
                              QStringLiteral("parentId"), QStringLiteral("replyCount"),
                              QStringLiteral("time"), QStringLiteral("user"),
                              QStringLiteral("userId"), QStringLiteral("vipLevel"),
                              QStringLiteral("voice")}));
        QCOMPARE(commentMap.value("user").toString(), QStringLiteral("u"));
        QCOMPARE(commentMap.value("userId").toDouble(), 7.0);
        QCOMPARE(commentMap.value("avatar").toString(),
                 QStringLiteral("http://cover/u.jpg?param=96y96"));
        QCOMPARE(commentMap.value("content").toString(), QStringLiteral("hi"));
        QCOMPARE(commentMap.value("likes").toDouble(), 3.0);
        QCOMPARE(commentMap.value("time").toDouble(), 10.0);
        QCOMPARE(commentMap.value("id").toDouble(), 42.0);
        QCOMPARE(commentMap.value("liked").toBool(), true);
        QCOMPARE(commentMap.value("parentId").toDouble(), 1.0);
        QCOMPARE(commentMap.value("replyCount").toDouble(), 14.0);
        QCOMPARE(commentMap.value("vipLevel").toString(), QStringLiteral("svip"));

        const QVariantList replied = commentMap.value("beReplied").toList();
        QCOMPARE(replied.size(), 1);
        const QVariantMap reply = replied.first().toMap();
        QCOMPARE(reply.value("id").toDouble(), 9.0);
        QCOMPARE(reply.value("content").toString(), QStringLiteral("old"));
        QCOMPARE(reply.value("userId").toDouble(), 8.0);
        QCOMPARE(reply.value("user").toString(), QStringLiteral("v"));

        // 空头像保持空串，不追加尺寸参数；其余字段仍有确定默认值
        const QVariantMap empty = commentToVariantMap(ncm::sdk::Comment{});
        QVERIFY(empty.contains("avatar"));
        QCOMPARE(empty.value("avatar").toString(), QString());
        QCOMPARE(empty.value("replyCount").toDouble(), 0.0);
        QVERIFY(empty.value("beReplied").toList().isEmpty());
        QVERIFY(empty.value("voice").toMap().isEmpty());

        // 语音：毫秒四舍五入到整秒
        ncm::sdk::Comment voice;
        voice.voiceUrl = QStringLiteral("http://audio/v.m4a");
        voice.voiceMs = 25495;
        voice.voiceText = QStringLiteral("唱的那句");
        const QVariantMap clip = commentToVariantMap(voice).value("voice").toMap();
        QCOMPARE(clip.value("url").toString(), QStringLiteral("http://audio/v.m4a"));
        QCOMPARE(clip.value("seconds").toDouble(), 25.0);
        QCOMPARE(clip.value("text").toString(), QStringLiteral("唱的那句"));
    }

    // profile 统一给 vipLevel；vipType 仍给还在读它的旧页面，vipRights 不再进 map
    void profileMapExposesVipLevel()
    {
        const QJsonObject profile{
            {"userId", 7},
            {"nickname", "u"},
            {"vipType", 11},
            {"vipRights", QJsonObject{
                 {"associator", QJsonObject{{"rights", true}}},
                 {"redplus", QJsonObject{{"rights", true}}}}},
        };
        const QVariantMap map = ncmProfile(profile, 96, 1200);
        QCOMPARE(map.value("vipLevel").toString(), QStringLiteral("svip"));
        QCOMPARE(map.value("vipType").toDouble(), 11.0);
        QVERIFY(!map.contains("vipRights"));
    }

    void lrcTranslationMatchesTimestamp()
    {
        const QString lrc = QStringLiteral("[00:01.00]hello\n[00:03.50]world\n");
        const QString tr = QStringLiteral("[00:01.00]你好\n[00:03.50]世界\n");
        const QVariantList lines = ncmParseLrc(lrc, tr);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).toMap().value("tr").toString(), QStringLiteral("你好"));
        QCOMPARE(lines.at(1).toMap().value("t").toDouble(), 3.5);
    }

    // 重复句共用一行：[01:22.68][00:36.19]副歌 → 两句，按时间排；正文里不留时间戳
    void lrcMergedTimestampsExpandAndSort()
    {
        const QString lrc = QStringLiteral(
            "[ar:歌手]\n"
            "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"某人\"}]}\n"
            "[00:10.00]开头\n"
            "[01:22.68][00:36.19]We got a little problem\r\n"
            "[00:50:50]冒号小数\n"
            "[00:60.00]<00:60.00>逐<00:60.40>字\n");
        const QString tr = QStringLiteral("[01:22.68][00:36.19]我们碰到了点小事\n");
        const QVariantList lines = ncmParseLrc(lrc, tr);
        QStringList texts;
        QList<double> times;
        for (const QVariant &v : lines) {
            texts.append(v.toMap().value("text").toString());
            times.append(v.toMap().value("t").toDouble());
        }
        QCOMPARE(texts, (QStringList{QStringLiteral("开头"), QStringLiteral("We got a little problem"),
                                     QStringLiteral("冒号小数"), QStringLiteral("逐字"),
                                     QStringLiteral("We got a little problem")}));
        QCOMPARE(times, (QList<double>{10, 36.19, 50.5, 60, 82.68}));
        QCOMPARE(lines.at(1).toMap().value("tr").toString(), QStringLiteral("我们碰到了点小事"));
        QCOMPARE(lines.at(4).toMap().value("tr").toString(), QStringLiteral("我们碰到了点小事"));
    }

    // 网易的「非滚动歌词」：没有时间戳的纯文本（前面可能挂署名行）。照行给出，t = -1；写回时不加时间戳
    void untimedLyricsKeepTheirLines()
    {
        const QString lrc = QStringLiteral(
            "{\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"某人\"}]}\n"
            "致还能动的身体\n"
            "致还能跳的心\n"
            "\n"
            "  致黑夜与天明 \n");
        const QVariantList lines = ncmParseLrc(lrc, QString());
        QStringList texts;
        for (const QVariant &v : lines) {
            texts.append(v.toMap().value("text").toString());
            QCOMPARE(v.toMap().value("t").toDouble(), -1.0);
        }
        QCOMPARE(texts, (QStringList{QStringLiteral("致还能动的身体"), QStringLiteral("致还能跳的心"),
                                     QStringLiteral("致黑夜与天明")}));
        QCOMPARE(ncmLyricsToLrc(lines), QStringLiteral("致还能动的身体\n致还能跳的心\n致黑夜与天明\n"));
        // 有一句带时间戳就按滚动歌词解析，没时间的行不混进来
        QCOMPARE(ncmParseLrc(QStringLiteral("[00:01.00]一句\n没有时间的行\n"), QString()).size(), 1);
    }

    // 写回的 LRC 再解析一遍得到同样的句子（时间、原文、译文）；分钟超过 99 不会出现，不测
    void lyricsToLrcRoundTrips()
    {
        const QString lrc = QStringLiteral("[00:10.00]开头\n[01:22.68]We got a little problem\n[03:05.40]结尾\n");
        const QString tr = QStringLiteral("[01:22.68]我们碰到了点小事\n");
        const QVariantList lines = ncmParseLrc(lrc, tr);
        const QString written = ncmLyricsToLrc(lines);
        QVERIFY(written.contains(QStringLiteral("[01:22.68]We got a little problem\n[01:22.68]我们碰到了点小事\n")));
        // 双语 LRC 里同一时间戳的第二行是译文：按这个约定拆回原文与译文
        QString original;
        QString translation;
        QString previousStamp;
        for (const QString &row : written.split(u'\n', Qt::SkipEmptyParts)) {
            const QString stamp = row.left(10);
            (stamp == previousStamp ? translation : original) += row + u'\n';
            previousStamp = stamp;
        }
        QCOMPARE(ncmParseLrc(original, translation), lines);
    }

    // 这次播放实际拿到的档位：请求 lossless、服务器降档给 higher 时是 higher；响应缺 level 时退回请求档位
    void playedLevelComesFromTheUrlResponse()
    {
        const NcmPlayedLevel played = ncmPlayedLevel(QJsonObject{{"level", "higher"}, {"br", 192000}},
                                                     QStringLiteral("lossless"));
        QCOMPARE(played.level, QStringLiteral("higher"));
        QCOMPARE(played.bitrate, 192000);
        const NcmPlayedLevel fallback = ncmPlayedLevel({}, QStringLiteral("hires"));
        QCOMPARE(fallback.level, QStringLiteral("hires"));
        QCOMPARE(fallback.bitrate, 999000);
    }

    // YRC：逐字标签拆成 words，句子按词拼接（英文词间空格留在前一个词里），译文按行时间对上；
    // {…} 开头的署名行与 LRC 一样不算歌词
    void yrcParsesWordsAndTranslation()
    {
        const QString yrc = QStringLiteral(
            "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"某人\"}]}\n"
            "[7880,2310](7880,150,0)I've (8030,300,0)been (8330,570,0)reading\n"
            "[10310,2160](10310,210,0)The (10520,820,0)legends (11640,90,0)and "
            "(11730,740,0)the (12470,0,0)myths\n"
            "[12710,2100](12710,960,0)Achilles (13670,270,0)and (13940,180,0)his\r\n");
        const QString tr = QStringLiteral("[00:07.88]我曾饱览\n[00:12.71]阿喀琉斯\n");
        const QVariantList lines = ncmParseYrc(yrc, tr);
        QCOMPARE(lines.size(), 3);

        const QVariantMap first = lines.at(0).toMap();
        QCOMPARE(first.value("t").toDouble(), 7.88);
        QCOMPARE(first.value("text").toString(), QStringLiteral("I've been reading"));
        QCOMPARE(first.value("tr").toString(), QStringLiteral("我曾饱览"));
        const QVariantList words = first.value("words").toList();
        QCOMPARE(words.size(), 3);
        QCOMPARE(words.at(0).toMap().value("t").toDouble(), 7.88);
        QCOMPARE(words.at(0).toMap().value("d").toDouble(), 0.15);
        QCOMPARE(words.at(0).toMap().value("text").toString(), QStringLiteral("I've "));
        QCOMPARE(words.at(2).toMap().value("t").toDouble(), 8.33);
        QCOMPARE(words.at(2).toMap().value("d").toDouble(), 0.57);

        // 零时长的词也保留；没有对应译文的行 tr 为空
        const QVariantMap second = lines.at(1).toMap();
        QCOMPARE(second.value("text").toString(), QStringLiteral("The legends and the myths"));
        QCOMPARE(second.value("tr").toString(), QString());
        const QVariantList secondWords = second.value("words").toList();
        QCOMPARE(secondWords.size(), 5);
        QCOMPARE(secondWords.at(4).toMap().value("d").toDouble(), 0.0);

        QCOMPARE(lines.at(2).toMap().value("t").toDouble(), 12.71);
        QCOMPARE(lines.at(2).toMap().value("tr").toString(), QStringLiteral("阿喀琉斯"));
    }
};

QTEST_GUILESS_MAIN(NormalizeTest)
#include "normalize_test.moc"
