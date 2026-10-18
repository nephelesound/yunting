// Typed dynamic cover SDK: alias fields, unavailable success and malformed data.
#include "sdk/dynamic_cover.h"

#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkDynamicCoverTest : public QObject {
    Q_OBJECT

private slots:
    void parsesKnownAndAliasFields()
    {
        QJsonObject data{
            {"videoPlayUrl", "https://cdn.example/cover.mp4"},
            {"weight", 1080}, {"height", 1920}, {"needTransition", true},
        };
        const auto result = parseDynamicCover(
            QJsonObject{{"code", 200}, {"data", data}}, 7);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->songId, qint64(7));
        QVERIFY(result.value->available);
        QCOMPARE(result.value->url, QStringLiteral("https://cdn.example/cover.mp4"));
        QCOMPARE(result.value->width, 1080);
        QCOMPARE(result.value->height, 1920);
        QVERIFY(result.value->needTransition);
    }

    void nullDataMeansNoCover()
    {
        const auto result = parseDynamicCover(
            QJsonObject{{"code", 200}, {"data", QJsonValue::Null}}, 7);
        QVERIFY(result.ok());
        QVERIFY(!result.value->available);
    }

    void wrongDataShapeIsParseError()
    {
        const auto result = parseDynamicCover(
            QJsonObject{{"code", 200}, {"data", QStringLiteral("bad")}}, 7);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/songplay/dynamic-cover"));
    }

    void invalidSongIdIsRejected()
    {
        const auto result = parseDynamicCover(QJsonObject{{"code", 200}}, 0);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::InvalidRequest);
    }
};

QTEST_GUILESS_MAIN(SdkDynamicCoverTest)
#include "sdk_dynamic_cover_test.moc"
