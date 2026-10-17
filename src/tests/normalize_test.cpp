// 归一化形状测试：不碰网络，只锁新加的导航字段与 MV / 云盘形状。
#include "../normalize.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

class NormalizeTest : public QObject {
    Q_OBJECT
private slots:
    void trackKeepsNavigationIds()
    {
        const QJsonObject song{
            {"id", 1},
            {"name", "晴天"},
            {"ar", QJsonArray{QJsonObject{{"id", 6452}, {"name", "周杰伦"}}}},
            {"al", QJsonObject{{"id", 32311}, {"name", "叶惠美"},
                               {"picUrl", "http://p1.music.126.net/x.jpg"}}},
            {"dt", 269000},
            {"alia", QJsonArray{"晴天 (Live)"}},
            {"hr", QJsonObject{{"br", 999000}}},
        };
        const QVariantMap t = ncmTrack(song);
        QCOMPARE(t.value("artistId").toDouble(), 6452);
        QCOMPARE(t.value("albumId").toDouble(), 32311);
        QCOMPARE(t.value("dur").toDouble(), 269);
        QCOMPARE(t.value("alias").toString(), QStringLiteral("晴天 (Live)"));
        QCOMPARE(t.value("quality").toString(), QStringLiteral("hires"));
        QCOMPARE(t.value("cover").toString(),
                 QStringLiteral("http://p1.music.126.net/x.jpg?param=120y120"));
    }

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
};

QTEST_GUILESS_MAIN(NormalizeTest)
#include "normalize_test.moc"
