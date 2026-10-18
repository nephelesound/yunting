#include "sdk/download.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class DownloadTest : public QObject {
    Q_OBJECT
private slots:
    void parsesDownload()
    {
        const QJsonObject root{
            {"code", 200},
            {"data", QJsonArray{QJsonObject{
                {"id", 42}, {"url", "https://cdn.example/a.flac"}, {"size", 1234},
                {"br", 999000}, {"md5", "abc"}, {"type", "flac"}, {"level", "lossless"},
                {"expi", 1200},
            }}},
        };
        const auto parsed = parseSongDownload(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->id, qint64(42));
        QCOMPARE(parsed.value->url, QStringLiteral("https://cdn.example/a.flac"));
        QCOMPARE(parsed.value->size, qint64(1234));
        QCOMPARE(parsed.value->type, QStringLiteral("flac"));
        QCOMPARE(parsed.value->level, QStringLiteral("lossless"));
        QCOMPARE(parsed.value->expires, qint64(1200));
    }

    void parsesObjectData()
    {
        const auto parsed = parseSongDownload(QJsonObject{
            {"data", QJsonObject{{"id", 7}, {"url", "https://cdn.example/a.mp3"},
                                 {"size", 1}, {"type", "mp3"}}},
        });
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->id, qint64(7));
        QCOMPARE(parsed.value->type, QStringLiteral("mp3"));
    }

    void emptyDataIsParseError()
    {
        const auto parsed = parseSongDownload(QJsonObject{{"data", QJsonArray{}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
    }

    void wrongTypeIsParseError()
    {
        const auto parsed = parseSongDownload(QJsonObject{
            {"data", QJsonArray{QJsonObject{{"id", 42}, {"url", 7}}}},
        });
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QVERIFY(parsed.error.path.contains(QStringLiteral("data[0]")));
    }
};

QTEST_GUILESS_MAIN(DownloadTest)
#include "download_test.moc"
