// Typed MV detail SDK contract.
#include "sdk/mv_page.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class MvPageTest : public QObject {
    Q_OBJECT

    static QJsonObject detailObject()
    {
        return QJsonObject{
            {"id", 42},
            {"name", "MV 甲"},
            {"cover", "http://p1.music.126.net/mv.jpg"},
            {"artistName", "歌手甲"},
            {"artistId", 7},
            {"duration", 213999},
            {"playCount", 123},
            {"subCount", 45},
            {"shareCount", 6},
            {"commentCount", 8},
            {"publishTime", "2024-01-02"},
            {"desc", "MV 简介"},
            {"br", 1080},
        };
    }

    static QJsonObject rootWith(const QJsonObject &data)
    {
        return QJsonObject{{"code", 200}, {"data", data}, {"subed", true}};
    }

private slots:
    void parsesDetail()
    {
        const auto parsed = parseMvDetail(rootWith(detailObject()));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const MvDetail &detail = *parsed.value;
        QCOMPARE(detail.id, qint64(42));
        QCOMPARE(detail.name, QStringLiteral("MV 甲"));
        QCOMPARE(detail.cover, QStringLiteral("http://p1.music.126.net/mv.jpg"));
        QCOMPARE(detail.artist, QStringLiteral("歌手甲"));
        QCOMPARE(detail.artistId, qint64(7));
        QCOMPARE(detail.duration, qint64(213));
        QCOMPARE(detail.playCount, qint64(123));
        QCOMPARE(detail.subCount, qint64(45));
        QCOMPARE(detail.shareCount, qint64(6));
        QCOMPARE(detail.commentCount, qint64(8));
        QCOMPARE(detail.publishTime, QStringLiteral("2024-01-02"));
        QCOMPARE(detail.description, QStringLiteral("MV 简介"));
        QCOMPARE(detail.subscribed, true);
    }

    void durationIsFlooredToWholeSeconds()
    {
        struct Case {
            double milliseconds;
            qint64 seconds;
        };
        const Case cases[]{{0, 0}, {999, 0}, {1000, 1}, {1999, 1}, {215999, 215},
                           {213999.9, 213}};
        for (const Case &item : cases) {
            QJsonObject data = detailObject();
            data.insert(QStringLiteral("duration"), item.milliseconds);
            const auto parsed = parseMvDetail(rootWith(data));
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QCOMPARE(parsed.value->duration, item.seconds);
        }
    }

    void subscribedComesFromRoot()
    {
        QJsonObject data = detailObject();
        data.insert(QStringLiteral("subscribed"), true); // data has no such field in live JSON
        const QJsonObject root{{"code", 200}, {"data", data}, {"subed", false}};
        const auto parsed = parseMvDetail(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->subscribed, false);

        const auto absent = parseMvDetail(QJsonObject{{"code", 200}, {"data", data}});
        QVERIFY2(absent.ok(), qPrintable(absent.error.message));
        QCOMPARE(absent.value->subscribed, false);
    }

    void missingAndNullFieldsUseDefaults()
    {
        const auto parsed = parseMvDetail(rootWith(QJsonObject{{"id", 42}}));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const MvDetail &detail = *parsed.value;
        QCOMPARE(detail.id, qint64(42));
        QCOMPARE(detail.name, QString());
        QCOMPARE(detail.cover, QString());
        QCOMPARE(detail.artist, QString());
        QCOMPARE(detail.artistId, qint64(0));
        QCOMPARE(detail.duration, qint64(0));
        QCOMPARE(detail.playCount, qint64(0));
        QCOMPARE(detail.subCount, qint64(0));
        QCOMPARE(detail.shareCount, qint64(0));
        QCOMPARE(detail.commentCount, qint64(0));
        QCOMPARE(detail.publishTime, QString());
        QCOMPARE(detail.description, QString());
        QCOMPARE(detail.subscribed, true);

        QJsonObject nulls{{"id", 42}};
        for (const char *key : {"name", "cover", "artistName", "artistId", "duration",
                                "playCount", "subCount", "shareCount", "commentCount",
                                "publishTime", "desc"})
            nulls.insert(QString::fromLatin1(key), QJsonValue::Null);
        const auto nullParsed = parseMvDetail(rootWith(nulls));
        QVERIFY2(nullParsed.ok(), qPrintable(nullParsed.error.message));
        QCOMPARE(nullParsed.value->artistId, qint64(0));
        QCOMPARE(nullParsed.value->duration, qint64(0));
        QCOMPARE(nullParsed.value->publishTime, QString());
    }

    void missingDataIsParseError()
    {
        const QJsonObject root{{"code", 200}, {"subed", true}};
        const auto parsed = parseMvDetail(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("v1/mv/detail"));
        QCOMPARE(parsed.error.path, QStringLiteral("mv.detail.data"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("missing object")));
    }

    void nullOrEmptyDataIsParseError()
    {
        for (const QJsonValue &data : {QJsonValue(QJsonValue::Null),
                                       QJsonValue(QJsonObject{})}) {
            const QJsonObject root{{"code", 200}, {"data", data}};
            const auto parsed = parseMvDetail(root);
            QVERIFY(!parsed.ok());
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.path, QStringLiteral("mv.detail.data"));
        }
    }

    void nonObjectDataIsParseError()
    {
        const QJsonObject root{{"code", 200}, {"data", QJsonValue(7)}};
        const auto parsed = parseMvDetail(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("mv.detail.data"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void malformedDurationIsParseError()
    {
        QJsonObject data = detailObject();
        data.insert(QStringLiteral("duration"), QStringLiteral("213999"));
        const auto parsed = parseMvDetail(rootWith(data));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("mv.detail.data"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("duration: expected number")));
    }

    void malformedPublishTimeIsParseError()
    {
        QJsonObject data = detailObject();
        data.insert(QStringLiteral("publishTime"), 1700000000000.0);
        const auto parsed = parseMvDetail(rootWith(data));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QVERIFY(parsed.error.message.contains(QStringLiteral("publishTime: expected string")));
    }

    void malformedSubedIsParseError()
    {
        const QJsonObject root{{"code", 200},
                               {"data", detailObject()},
                               {"subed", QStringLiteral("true")}};
        const auto parsed = parseMvDetail(root);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("mv.detail.subed"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("subed: expected boolean")));
    }
};

QTEST_GUILESS_MAIN(MvPageTest)
#include "mv_page_test.moc"
