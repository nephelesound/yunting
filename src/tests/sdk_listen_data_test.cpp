// Typed listen-footprint SDK contract: total, week/month realtime and play rank.
#include "sdk/listen_data.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class ListenDataTest : public QObject {
    Q_OBJECT

    static QJsonObject artistObject(qint64 id, const QString &name)
    {
        return QJsonObject{{"artistId", id}, {"artistName", name},
                           {"unknownArtistField", "ignored"}};
    }

    static QJsonObject songObject(qint64 id, const QString &name, const QJsonArray &artists)
    {
        return QJsonObject{{"songId", id},
                           {"songName", name},
                           {"albumName", QStringLiteral("album")},
                           {"picUrl", QStringLiteral("http://p1.music.126.net/cover.jpg")},
                           {"playCount", 17},
                           {"artists", artists},
                           {"unknownSongField", "ignored"}};
    }

private slots:
    void parsesListenTotal()
    {
        const auto parsed = parseListenTotal(
            QJsonObject{{"code", 200}, {"data", QJsonObject{{"totalDuration", 7200}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->seconds, qint64(7200));
    }

    void missingOrEmptyTotalDataIsParseError()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"data", QJsonObject{}}},
              QJsonObject{{"code", 200}, {"data", QJsonValue::Null}}}) {
            const auto parsed = parseListenTotal(root);
            QVERIFY(!parsed.ok());
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint,
                     QString::fromLatin1("api/content/activity/listen/data/total"));
            QCOMPARE(parsed.error.path, QStringLiteral("listenTotal.data"));
        }
    }

    void wrongTotalDataTypeIsParseError()
    {
        const auto parsed =
            parseListenTotal(QJsonObject{{"code", 200}, {"data", QStringLiteral("nope")}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("listenTotal.data"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongTotalDurationTypeIsParseError()
    {
        const auto parsed = parseListenTotal(QJsonObject{
            {"code", 200}, {"data", QJsonObject{{"totalDuration", QStringLiteral("7200")}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("listenTotal.data.totalDuration"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected number")));
    }

    void parsesListenRealtime()
    {
        const QJsonObject distribution{
            {"timeDistribution", QJsonObject{{"morning", 60}, {"night", 40}}}};
        const QJsonObject friends{{"friendCount", 3}, {"rank", 2}};
        const QJsonObject today{{"playCount", 7}, {"songCount", 5}};
        const QJsonObject root{
            {"code", 200},
            {"data", QJsonObject{
                 {"startTime", 1750000000000.0},
                 {"endTime", 1750604800000.0},
                 {"userNickName", QStringLiteral("Listener")},
                 {"listenTimeDistributionBlock", distribution},
                 {"weekFriendsListenBlock", friends},
                 {"weekTodayListenBlock", today},
                 {"unknownDataField", true}}}};

        const auto parsed = parseListenRealtime(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const ListenRealtime &realtime = *parsed.value;
        QCOMPARE(realtime.startTime, qint64(1750000000000));
        QCOMPARE(realtime.endTime, qint64(1750604800000));
        QCOMPARE(realtime.userNickName, QStringLiteral("Listener"));
        QCOMPARE(realtime.listenTimeDistribution, distribution);
        QCOMPARE(realtime.weekFriends, friends);
        QCOMPARE(realtime.weekToday, today);
    }

    void realtimeMissingFieldsUseDefaults()
    {
        const auto parsed = parseListenRealtime(
            QJsonObject{{"code", 200}, {"data", QJsonObject{{"startTime", 1}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const ListenRealtime &realtime = *parsed.value;
        QCOMPARE(realtime.startTime, qint64(1));
        QCOMPARE(realtime.endTime, qint64(0));
        QVERIFY(realtime.userNickName.isEmpty());
        QVERIFY(realtime.listenTimeDistribution.isEmpty());
        QVERIFY(realtime.weekFriends.isEmpty());
        QVERIFY(realtime.weekToday.isEmpty());
    }

    void missingOrEmptyRealtimeDataIsParseError()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"data", QJsonObject{}}},
              QJsonObject{{"code", 200}, {"data", QJsonValue::Null}}}) {
            const auto parsed = parseListenRealtime(root);
            QVERIFY(!parsed.ok());
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint,
                     QString::fromLatin1("api/content/activity/listen/data/realtime/report"));
            QCOMPARE(parsed.error.path, QStringLiteral("listenRealtime.data"));
        }
    }

    void wrongRealtimeBlockTypeIsParseError()
    {
        const auto parsed = parseListenRealtime(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"weekFriendsListenBlock", QStringLiteral("nope")}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("listenRealtime.data.weekFriendsListenBlock"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongRealtimeFieldTypeIsParseError()
    {
        const auto parsed = parseListenRealtime(QJsonObject{
            {"code", 200}, {"data", QJsonObject{{"userNickName", 7}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("listenRealtime.data.userNickName"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected string")));
    }

    void parsesListenRank()
    {
        const QJsonObject root{
            {"code", 200},
            {"data", QJsonObject{
                 {"startTime", 1750000000000.0},
                 {"endTime", 1750604800000.0},
                 {"songCount", 2},
                 {"songItems", QJsonArray{
                      songObject(42, QStringLiteral("First"),
                                 QJsonArray{artistObject(7, QStringLiteral("Alice")),
                                            artistObject(8, QStringLiteral("Bob"))}),
                      songObject(43, QStringLiteral("Second"), QJsonArray{})}}}}};

        const auto parsed = parseListenRank(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const ListenRank &rank = *parsed.value;
        QCOMPARE(rank.startTime, qint64(1750000000000));
        QCOMPARE(rank.endTime, qint64(1750604800000));
        QCOMPARE(rank.songCount, qint64(2));
        QCOMPARE(rank.items.size(), 2);

        const ListenRankSong &first = rank.items.first();
        QCOMPARE(first.id, qint64(42));
        QCOMPARE(first.name, QStringLiteral("First"));
        QCOMPARE(first.album, QStringLiteral("album"));
        QCOMPARE(first.cover, QStringLiteral("http://p1.music.126.net/cover.jpg"));
        QCOMPARE(first.playCount, qint64(17));
        QCOMPARE(first.artists.size(), 2);
        QCOMPARE(first.artists.at(0).id, qint64(7));
        QCOMPARE(first.artists.at(0).name, QStringLiteral("Alice"));
        QCOMPARE(first.artists.at(1).id, qint64(8));
        QCOMPARE(first.artists.at(1).name, QStringLiteral("Bob"));

        QCOMPARE(rank.items.at(1).id, qint64(43));
        QVERIFY(rank.items.at(1).artists.isEmpty());
    }

    void rankMissingSongItemsIsEmpty()
    {
        const auto parsed = parseListenRank(
            QJsonObject{{"code", 200}, {"data", QJsonObject{{"songCount", 0}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QVERIFY(parsed.value->items.isEmpty());
    }

    void missingOrEmptyRankDataIsParseError()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"data", QJsonObject{}}},
              QJsonObject{{"code", 200}, {"data", QJsonValue::Null}}}) {
            const auto parsed = parseListenRank(root);
            QVERIFY(!parsed.ok());
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint,
                     QString::fromLatin1("api/content/activity/listen/data/song/play/rank"));
            QCOMPARE(parsed.error.path, QStringLiteral("listenRank.data"));
        }
    }

    void wrongSongItemsTypeIsParseError()
    {
        const auto parsed = parseListenRank(QJsonObject{
            {"code", 200}, {"data", QJsonObject{{"songItems", QStringLiteral("nope")}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("listenRank.data.songItems"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }

    void malformedSongRowIsParseError()
    {
        const auto parsed = parseListenRank(QJsonObject{
            {"code", 200}, {"data", QJsonObject{{"songItems", QJsonArray{42}}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("listenRank.data.songItems[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongSongFieldTypeIsParseError()
    {
        QJsonObject broken = songObject(42, QStringLiteral("First"), QJsonArray{});
        broken.insert(QStringLiteral("songName"), 7);

        const auto parsed = parseListenRank(QJsonObject{
            {"code", 200}, {"data", QJsonObject{{"songItems", QJsonArray{broken}}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("listenRank.data.songItems[0].songName"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected string")));
    }

    void malformedArtistRowIsParseError()
    {
        const auto parsed = parseListenRank(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"songItems", QJsonArray{
                 songObject(42, QStringLiteral("First"), QJsonArray{42})}}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("listenRank.data.songItems[0].artists[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongArtistFieldTypeIsParseError()
    {
        const auto parsed = parseListenRank(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"songItems", QJsonArray{
                 songObject(42, QStringLiteral("First"),
                            QJsonArray{QJsonObject{{"artistId", 7},
                                                   {"artistName", 8}}})}}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("listenRank.data.songItems[0].artists[0].artistName"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected string")));
    }

    void wrongArtistsTypeIsParseError()
    {
        QJsonObject broken = songObject(42, QStringLiteral("First"), QJsonArray{});
        broken.insert(QStringLiteral("artists"), QStringLiteral("nope"));

        const auto parsed = parseListenRank(QJsonObject{
            {"code", 200}, {"data", QJsonObject{{"songItems", QJsonArray{broken}}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("listenRank.data.songItems[0].artists"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }
};

QTEST_GUILESS_MAIN(ListenDataTest)
#include "sdk_listen_data_test.moc"
