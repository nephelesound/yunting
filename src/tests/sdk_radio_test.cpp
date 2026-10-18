// Typed Radio/Podcast SDK contract.
#include "sdk/radio.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkRadioTest : public QObject {
    Q_OBJECT

    static QJsonObject radioObject()
    {
        QJsonObject dj{{"userId", 99}, {"nickname", "主播"}, {"avatarUrl", "http://avatar"}};
        return QJsonObject{{"id", 7}, {"name", "电台"}, {"picUrl", "http://radio"},
                           {"desc", "简介"}, {"playCount", 12}, {"programCount", 3},
                           {"subCount", 4}, {"dj", dj}};
    }

    static QJsonObject programObject()
    {
        QJsonObject mainSong{{"id", 55}, {"name", "节目歌"}, {"duration", 65000},
                             {"fee", 0},
                             {"artists", QJsonArray{QJsonObject{{"name", "歌手"}}}}};
        return QJsonObject{{"id", 66}, {"name", "节目"}, {"description", "节目简介"},
                           {"createTime", 1700000000000.0}, {"duration", 794000},
                           {"serialNum", 31}, {"coverUrl", "http://program"},
                           {"mainSong", mainSong}, {"radio", radioObject()}};
    }

private slots:
    void parsesStation()
    {
        QJsonObject root{{"code", 200}, {"data", radioObject()}};
        const auto result = parseRadioStation(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->id, qint64(7));
        QCOMPARE(result.value->djName, QStringLiteral("主播"));
        QCOMPARE(result.value->programCount, qint64(3));
    }

    void parsesProgram()
    {
        QJsonObject root{{"code", 200}, {"program", programObject()}};
        const auto result = parseRadioProgram(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const RadioProgram &out = *result.value;
        QCOMPARE(out.id, qint64(66));
        QCOMPARE(out.duration, qint64(794));
        QCOMPARE(out.mainSongId, qint64(55));
        QCOMPARE(out.mainSongDuration, qint64(65));
        QCOMPARE(out.artists, QStringList{QStringLiteral("歌手")});
        QCOMPARE(out.cover, QStringLiteral("http://program"));
        QCOMPARE(out.radio.name, QStringLiteral("电台"));

        // 节目 map：主歌 id + 文档里的节目覆盖，另带节目字段；封面保持原图
        const QVariantMap map = programToVariantMap(out);
        QCOMPARE(map.value("id").toDouble(), 55.0);
        QCOMPARE(map.value("name").toString(), QStringLiteral("节目"));
        QCOMPARE(map.value("artist").toString(), QStringLiteral("主播"));
        QCOMPARE(map.value("album").toString(), QStringLiteral("电台"));
        QCOMPARE(map.value("cover").toString(), QStringLiteral("http://program"));
        QCOMPARE(map.value("dur").toDouble(), 65.0);
        QCOMPARE(map.value("playable").toBool(), true);
        QCOMPARE(map.value("programId").toDouble(), 66.0);
        QCOMPARE(map.value("radioId").toDouble(), 7.0);
        QCOMPARE(map.value("serial").toDouble(), 31.0);
        QCOMPARE(map.value("createTime").toDouble(), 1700000000000.0);
        QCOMPARE(map.value("description").toString(), QStringLiteral("节目简介"));
        QCOMPARE(map.size(), 21);
    }

    // byradio 的部分节目只带 mainTrackId：播放 id 不能因此丢掉
    void fallsBackToMainTrackId()
    {
        QJsonObject program{{"id", 66}, {"name", "节目"}, {"mainTrackId", 55}};
        QJsonObject root{{"code", 200}, {"program", program}};
        const auto result = parseRadioProgram(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->mainSongId, qint64(55));
    }

    void parsesRecommendations()
    {
        QJsonObject root{{"code", 200}, {"result", QJsonArray{
            QJsonObject{{"id", 66}, {"program", programObject()}}}}};
        const auto result = parseRecommendedPrograms(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 1);
        QCOMPARE(result.value->first().radio.id, qint64(7));
    }

    void parsesProgramPage()
    {
        QJsonObject root{{"code", 200}, {"count", 1}, {"more", true},
                         {"programs", QJsonArray{programObject()}}};
        const auto result = parseRadioProgramPage(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->count, qint64(1));
        QCOMPARE(result.value->more, true);
        QCOMPARE(result.value->items.first().name, QStringLiteral("节目"));
    }

    void missingStationDataIsParseError()
    {
        const auto result = parseRadioStation(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QVERIFY(result.error.message.contains(QStringLiteral("data")));
    }
};

QTEST_GUILESS_MAIN(SdkRadioTest)
#include "sdk_radio_test.moc"
