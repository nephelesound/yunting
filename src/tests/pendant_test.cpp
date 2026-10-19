#include "sdk/pendant.h"

#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class PendantTest : public QObject {
    Q_OBJECT
private slots:
    void parsesPendant()
    {
        const auto parsed = parseAvatarPendant(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"pendantData", QJsonObject{
                {"id", 1303046}, {"name", "跃龙门"},
                {"imageUrl", "http://p1.music.126.net/main.png"},
                {"imageAndroidUrl", "http://p1.music.126.net/android.png"},
                {"imageIosUrl", "http://p1.music.126.net/ios.png"},
                {"previewImageUrl", "http://p1.music.126.net/preview.png"},
                {"type", 0}, {"status", 1}}}}},
        });
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->id, qint64(1303046));
        QCOMPARE(parsed.value->name, QStringLiteral("跃龙门"));
        QCOMPARE(parsed.value->imageUrl, QStringLiteral("http://p1.music.126.net/main.png"));
        QCOMPARE(parsed.value->androidUrl, QStringLiteral("http://p1.music.126.net/android.png"));
        QCOMPARE(parsed.value->previewUrl, QStringLiteral("http://p1.music.126.net/preview.png"));
    }

    void nullPendantIsApiError()
    {
        const auto parsed = parseAvatarPendant(
            QJsonObject{{"data", QJsonObject{{"pendantData", QJsonValue::Null}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Api);
    }

    void wrongFieldIsParseError()
    {
        const auto parsed = parseAvatarPendant(
            QJsonObject{{"data", QJsonObject{{"pendantData",
                                             QJsonObject{{"id", 1}, {"imageUrl", 2}}}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
    }
};

QTEST_GUILESS_MAIN(PendantTest)
#include "pendant_test.moc"
