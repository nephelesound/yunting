// Typed account-feed SDK contract: top-list cards, hot searches and liked ids.
#include "sdk/account_feed.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

namespace {

QJsonObject previewRow(const QString &name, const QString &artist)
{
    return QJsonObject{{"first", name}, {"second", artist}};
}

QJsonObject toplistRow(qint64 id, const QString &name)
{
    return QJsonObject{
        {"id", id},
        {"name", name},
        {"coverImgUrl", QStringLiteral("http://p1.music.126.net/cover.jpg")},
        {"updateFrequency", QStringLiteral("weekly")},
        {"trackCount", 100},
        {"tracks", QJsonArray{previewRow(QStringLiteral("song-a"), QStringLiteral("artist-a")),
                              previewRow(QStringLiteral("song-b"), QStringLiteral("artist-b"))}},
        {"unknownField", QStringLiteral("ignored")}};
}

QJsonObject hotSearchRow(qint64 score, const QString &word)
{
    return QJsonObject{{"searchWord", word},
                       {"content", QStringLiteral("note")},
                       {"score", score},
                       {"unknownField", true}};
}

} // namespace

class AccountFeedTest : public QObject {
    Q_OBJECT

private slots:
    void parsesToplists()
    {
        const auto parsed = parseToplists(QJsonObject{
            {"code", 200},
            {"list", QJsonArray{toplistRow(42, QStringLiteral("Chart A")),
                                toplistRow(7, QStringLiteral("Chart B"))}}});

        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->size(), 2);

        const Toplist &first = parsed.value->first();
        QCOMPARE(first.id, 42.0);
        QCOMPARE(first.name, QStringLiteral("Chart A"));
        QCOMPARE(first.cover, QStringLiteral("http://p1.music.126.net/cover.jpg"));
        QCOMPARE(first.update, QStringLiteral("weekly"));
        QCOMPARE(first.count, 100.0);
        QCOMPARE(first.preview.size(), 2);
        QCOMPARE(first.preview.at(0).name, QStringLiteral("song-a"));
        QCOMPARE(first.preview.at(0).artist, QStringLiteral("artist-a"));
        QCOMPARE(first.preview.at(1).name, QStringLiteral("song-b"));
        QCOMPARE(first.preview.at(1).artist, QStringLiteral("artist-b"));

        QCOMPARE(parsed.value->at(1).id, 7.0);
        QCOMPARE(parsed.value->at(1).name, QStringLiteral("Chart B"));
    }

    void toplistCapKeepsFirstTwelveInOrder()
    {
        QJsonArray rows;
        rows.append(toplistRow(42, QStringLiteral("first")));
        rows.append(toplistRow(7, QStringLiteral("second")));
        for (int i = 0; i < 10; ++i)
            rows.append(toplistRow(100 + i, QStringLiteral("row-%1").arg(i)));
        // Beyond the cap: neither this malformed row nor the next one is read.
        rows.append(QJsonObject{{"id", QStringLiteral("bad")}});
        rows.append(toplistRow(999, QStringLiteral("ignored")));

        const auto parsed = parseToplists(QJsonObject{{"code", 200}, {"list", rows}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->size(), 12);
        QCOMPARE(parsed.value->at(0).id, 42.0);
        QCOMPARE(parsed.value->at(1).id, 7.0);
        QCOMPARE(parsed.value->at(11).id, 109.0);
    }

    void missingOrNullToplistIsEmpty()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"list", QJsonValue::Null}},
              QJsonObject{{"code", 200}, {"list", QJsonArray{}}}}) {
            const auto parsed = parseToplists(root);
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->isEmpty());
        }
    }

    void wrongToplistArrayTypeIsParseError()
    {
        const auto parsed =
            parseToplists(QJsonObject{{"code", 200}, {"list", QStringLiteral("nope")}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/toplist/detail"));
        QCOMPARE(parsed.error.path, QStringLiteral("toplists.list"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }

    void nonObjectToplistRowIsParseError()
    {
        const auto parsed =
            parseToplists(QJsonObject{{"code", 200}, {"list", QJsonArray{42}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/toplist/detail"));
        QCOMPARE(parsed.error.path, QStringLiteral("toplists.list[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongToplistFieldTypeIsParseError()
    {
        const auto check = [](const QString &field, const QJsonValue &value,
                              const QString &expectedPath) {
            QJsonObject row = toplistRow(42, QStringLiteral("Chart"));
            row.insert(field, value);
            const auto parsed =
                parseToplists(QJsonObject{{"code", 200}, {"list", QJsonArray{row}}});
            QVERIFY2(!parsed.ok(), qPrintable(field));
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/toplist/detail"));
            QCOMPARE(parsed.error.path, expectedPath);
        };

        check(QStringLiteral("id"), QStringLiteral("42"),
              QStringLiteral("toplists.list[0].id"));
        check(QStringLiteral("name"), 42, QStringLiteral("toplists.list[0].name"));
        check(QStringLiteral("coverImgUrl"), 42,
              QStringLiteral("toplists.list[0].coverImgUrl"));
        check(QStringLiteral("updateFrequency"), 42,
              QStringLiteral("toplists.list[0].updateFrequency"));
        check(QStringLiteral("trackCount"), QStringLiteral("many"),
              QStringLiteral("toplists.list[0].trackCount"));
        check(QStringLiteral("tracks"), QJsonObject{},
              QStringLiteral("toplists.list[0].tracks"));
    }

    void toplistMissingAndNullFieldsUseDefaults()
    {
        const auto missing =
            parseToplists(QJsonObject{{"code", 200}, {"list", QJsonArray{QJsonObject{}}}});
        QVERIFY2(missing.ok(), qPrintable(missing.error.message));
        const Toplist &defaults = missing.value->first();
        QCOMPARE(defaults.id, 0.0);
        QVERIFY(defaults.name.isEmpty());
        QVERIFY(defaults.cover.isEmpty());
        QVERIFY(defaults.update.isEmpty());
        QCOMPARE(defaults.count, 0.0);
        QVERIFY(defaults.preview.isEmpty());

        const QJsonObject nulls{
            {"id", QJsonValue::Null},           {"name", QJsonValue::Null},
            {"coverImgUrl", QJsonValue::Null},  {"updateFrequency", QJsonValue::Null},
            {"trackCount", QJsonValue::Null},   {"tracks", QJsonValue::Null}};
        const auto nullable = parseToplists(
            QJsonObject{{"code", 200}, {"list", QJsonArray{nulls}}});
        QVERIFY2(nullable.ok(), qPrintable(nullable.error.message));
        const Toplist &empty = nullable.value->first();
        QCOMPARE(empty.id, 0.0);
        QVERIFY(empty.name.isEmpty());
        QVERIFY(empty.cover.isEmpty());
        QVERIFY(empty.update.isEmpty());
        QCOMPARE(empty.count, 0.0);
        QVERIFY(empty.preview.isEmpty());
    }

    void malformedPreviewRowIsParseError()
    {
        QJsonObject row = toplistRow(42, QStringLiteral("Chart"));
        row.insert(QStringLiteral("tracks"), QJsonArray{42});

        const auto parsed =
            parseToplists(QJsonObject{{"code", 200}, {"list", QJsonArray{row}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("toplists.list[0].tracks[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongPreviewFieldTypeIsParseError()
    {
        QJsonObject row = toplistRow(42, QStringLiteral("Chart"));
        row.insert(QStringLiteral("tracks"),
                   QJsonArray{QJsonObject{{"first", QStringLiteral("song")},
                                          {"second", 42}}});

        const auto parsed =
            parseToplists(QJsonObject{{"code", 200}, {"list", QJsonArray{row}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("toplists.list[0].tracks[0].second"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected string")));
    }

    void parsesHotSearches()
    {
        const auto parsed = parseHotSearches(QJsonObject{
            {"code", 200},
            {"data", QJsonArray{hotSearchRow(999, QStringLiteral("first")),
                                hotSearchRow(7, QStringLiteral("second"))}}});

        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->size(), 2);
        QCOMPARE(parsed.value->at(0).word, QStringLiteral("first"));
        QCOMPARE(parsed.value->at(0).note, QStringLiteral("note"));
        QCOMPARE(parsed.value->at(0).score, 999.0);
        QCOMPARE(parsed.value->at(1).word, QStringLiteral("second"));
        QCOMPARE(parsed.value->at(1).score, 7.0);
    }

    void hotSearchCapKeepsFirstTwentyInOrder()
    {
        QJsonArray rows;
        for (int i = 0; i < 20; ++i)
            rows.append(hotSearchRow(1000 - i, QStringLiteral("word-%1").arg(i)));
        // Beyond the cap: malformed entries are not inspected.
        rows.append(QJsonObject{{"searchWord", 42}});
        rows.append(hotSearchRow(0, QStringLiteral("ignored")));

        const auto parsed = parseHotSearches(QJsonObject{{"code", 200}, {"data", rows}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->size(), 20);
        QCOMPARE(parsed.value->first().word, QStringLiteral("word-0"));
        QCOMPARE(parsed.value->last().word, QStringLiteral("word-19"));
        QCOMPARE(parsed.value->last().score, 981.0);
    }

    void missingOrNullHotSearchesAreEmpty()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"data", QJsonValue::Null}},
              QJsonObject{{"code", 200}, {"data", QJsonArray{}}}}) {
            const auto parsed = parseHotSearches(root);
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->isEmpty());
        }
    }

    void wrongHotSearchArrayTypeIsParseError()
    {
        const auto parsed =
            parseHotSearches(QJsonObject{{"code", 200}, {"data", QJsonObject{}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/hotsearchlist/get"));
        QCOMPARE(parsed.error.path, QStringLiteral("hotSearches.data"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }

    void nonObjectHotSearchRowIsParseError()
    {
        const auto parsed =
            parseHotSearches(QJsonObject{{"code", 200}, {"data", QJsonArray{QStringLiteral("x")}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("hotSearches.data[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongHotSearchFieldTypeIsParseError()
    {
        const auto check = [](const QString &field, const QJsonValue &value,
                              const QString &expectedPath) {
            QJsonObject row = hotSearchRow(42, QStringLiteral("word"));
            row.insert(field, value);
            const auto parsed =
                parseHotSearches(QJsonObject{{"code", 200}, {"data", QJsonArray{row}}});
            QVERIFY2(!parsed.ok(), qPrintable(field));
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/hotsearchlist/get"));
            QCOMPARE(parsed.error.path, expectedPath);
        };

        check(QStringLiteral("searchWord"), 42,
              QStringLiteral("hotSearches.data[0].searchWord"));
        check(QStringLiteral("content"), true,
              QStringLiteral("hotSearches.data[0].content"));
        check(QStringLiteral("score"), QStringLiteral("hot"),
              QStringLiteral("hotSearches.data[0].score"));
    }

    void hotSearchMissingFieldsUseDefaults()
    {
        const auto parsed =
            parseHotSearches(QJsonObject{{"code", 200}, {"data", QJsonArray{QJsonObject{}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const HotSearch &hot = parsed.value->first();
        QVERIFY(hot.word.isEmpty());
        QVERIFY(hot.note.isEmpty());
        QCOMPARE(hot.score, 0.0);

        const auto nullable = parseHotSearches(QJsonObject{
            {"code", 200},
            {"data", QJsonArray{QJsonObject{{"searchWord", QJsonValue::Null},
                                            {"content", QJsonValue::Null},
                                            {"score", QJsonValue::Null}}}}});
        QVERIFY2(nullable.ok(), qPrintable(nullable.error.message));
        QVERIFY(nullable.value->first().word.isEmpty());
        QVERIFY(nullable.value->first().note.isEmpty());
        QCOMPARE(nullable.value->first().score, 0.0);
    }

    void parsesLikedIds()
    {
        const auto parsed = parseLikedIds(
            QJsonObject{{"code", 200}, {"ids", QJsonArray{42, 7, 1001}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->ids.size(), 3);
        QCOMPARE(parsed.value->ids.at(0), 42.0);
        QCOMPARE(parsed.value->ids.at(1), 7.0);
        QCOMPARE(parsed.value->ids.at(2), 1001.0);
    }

    void missingOrNullLikedIdsAreEmpty()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"ids", QJsonValue::Null}},
              QJsonObject{{"code", 200}, {"ids", QJsonArray{}}}}) {
            const auto parsed = parseLikedIds(root);
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->ids.isEmpty());
        }
    }

    void wrongLikedIdsArrayTypeIsParseError()
    {
        const auto parsed =
            parseLikedIds(QJsonObject{{"code", 200}, {"ids", QStringLiteral("42")}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/song/like/get"));
        QCOMPARE(parsed.error.path, QStringLiteral("likedIds.ids"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }

    void nonNumberLikedIdsAreParseErrors()
    {
        const auto check = [](const QJsonValue &badValue) {
            const auto parsed = parseLikedIds(
                QJsonObject{{"code", 200}, {"ids", QJsonArray{42, badValue}}});
            QVERIFY(!parsed.ok());
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/song/like/get"));
            QCOMPARE(parsed.error.path, QStringLiteral("likedIds.ids[1]"));
            QVERIFY(parsed.error.message.contains(QStringLiteral("expected number")));
        };

        check(QStringLiteral("7"));
        check(QJsonValue::Null);
        check(QJsonObject{});
    }
};

QTEST_GUILESS_MAIN(AccountFeedTest)
#include "account_feed_test.moc"
