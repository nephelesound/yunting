// Typed lyrics SDK contract: raw LRC/YRC texts, fallback inputs and malformed payloads.
#include "sdk/lyrics.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class LyricsTest : public QObject {
    Q_OBJECT

    static QJsonObject lyricObject(const QString &text)
    {
        return QJsonObject{{"version", 1}, {"lyric", text}};
    }

private slots:
    void parsesAllRawLyricTexts()
    {
        const QString lrc = QStringLiteral("[00:01.00]la la\n[00:05.50]na na\n");
        const QString tlyric = QStringLiteral("[00:01.00]啦啦\n[00:05.50]呐呐\n");
        const QString yrc = QStringLiteral(
            "{\"t\":0,\"c\":[{\"tx\":\"作词: \"}]}\n"
            "[1000,1200](1000,200,0)la (1200,300,0)la\n");
        const QString ytlrc = QStringLiteral("[1000,1200]啦啦\n");
        const QJsonObject root{
            {"code", 200},
            {"lrc", lyricObject(lrc)},
            {"tlyric", lyricObject(tlyric)},
            {"yrc", lyricObject(yrc)},
            {"ytlrc", lyricObject(ytlrc)},
            {"klyric", lyricObject(QStringLiteral("ignored"))},
            {"romalrc", lyricObject(QStringLiteral("ignored"))},
        };

        const auto parsed = parseSongLyrics(root, 42);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SongLyrics &lyrics = *parsed.value;
        QCOMPARE(lyrics.songId, qint64(42));
        QCOMPARE(lyrics.lrc, lrc);
        QCOMPARE(lyrics.tlyric, tlyric);
        QCOMPARE(lyrics.yrc, yrc);
        QCOMPARE(lyrics.ytlrc, ytlrc);
    }

    // No word-level lyric means the Store takes the LRC path; the parser must
    // hand over the LRC inputs untouched and leave ytlrc empty instead of
    // applying the Store's ytlrc->tlyric fallback itself.
    void missingYrcKeepsLrcInputsAndRawYtlrc()
    {
        const QJsonObject root{
            {"code", 200},
            {"lrc", lyricObject(QStringLiteral("[00:02.00]fallback"))},
            {"tlyric", lyricObject(QStringLiteral("[00:02.00]译文"))},
            {"yrc", lyricObject(QString{})},
        };

        const auto parsed = parseSongLyrics(root, 7);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->songId, qint64(7));
        QVERIFY(parsed.value->yrc.isEmpty());
        QCOMPARE(parsed.value->lrc, QStringLiteral("[00:02.00]fallback"));
        QCOMPARE(parsed.value->tlyric, QStringLiteral("[00:02.00]译文"));
        QVERIFY(parsed.value->ytlrc.isEmpty());
    }

    void missingLyricContainersAreEmpty()
    {
        const auto parsed = parseSongLyrics(QJsonObject{{"code", 200}}, 7);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->songId, qint64(7));
        QVERIFY(parsed.value->lrc.isEmpty());
        QVERIFY(parsed.value->tlyric.isEmpty());
        QVERIFY(parsed.value->yrc.isEmpty());
        QVERIFY(parsed.value->ytlrc.isEmpty());
    }

    void nullContainersAndStringsAreEmpty()
    {
        const QJsonObject root{
            {"code", 200},
            {"lrc", QJsonValue::Null},
            {"tlyric", QJsonObject{{"lyric", QJsonValue::Null}}},
            {"yrc", QJsonObject{}},
            {"ytlrc", QJsonObject{{"version", 1}}},
        };

        const auto parsed = parseSongLyrics(root, 7);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QVERIFY(parsed.value->lrc.isEmpty());
        QVERIFY(parsed.value->tlyric.isEmpty());
        QVERIFY(parsed.value->yrc.isEmpty());
        QVERIFY(parsed.value->ytlrc.isEmpty());
    }

    void wrongContainerTypeIsParseError()
    {
        const auto parsed = parseSongLyrics(
            QJsonObject{{"code", 200}, {"lrc", QStringLiteral("not-an-object")}}, 7);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/song/lyric/v1"));
        QCOMPARE(parsed.error.path, QStringLiteral("lrc"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongLyricTypeIsParseError()
    {
        const auto parsed = parseSongLyrics(
            QJsonObject{{"code", 200}, {"yrc", QJsonObject{{"lyric", 7}}}}, 7);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/song/lyric/v1"));
        QCOMPARE(parsed.error.path, QStringLiteral("yrc.lyric"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected string")));
    }

    void legacyPureMusicFlagsAreCarried()
    {
        const auto direct = parseSongLyrics(
            QJsonObject{{"code", 200}, {"lrc", lyricObject(QString{})},
                        {"tlyric", lyricObject(QString{})}, {"pureMusic", true}}, 7);
        QVERIFY2(direct.ok(), qPrintable(direct.error.message));
        QVERIFY(direct.value->pureMusic);

        const auto legacy = parseSongLyrics(
            QJsonObject{{"code", 200}, {"lrc", lyricObject(QString{})}, {"nolyric", true}}, 7);
        QVERIFY2(legacy.ok(), qPrintable(legacy.error.message));
        QVERIFY(legacy.value->pureMusic);

        const auto normal = parseSongLyrics(
            QJsonObject{{"code", 200}, {"lrc", lyricObject(QStringLiteral("[00:01.00]x"))}}, 7);
        QVERIFY2(normal.ok(), qPrintable(normal.error.message));
        QVERIFY(!normal.value->pureMusic);
        QVERIFY(!normal.value->uncollected);

        // 有歌词但网易没有收录：空歌词是确定结果，不是瞬时空包
        const auto uncollected = parseSongLyrics(
            QJsonObject{{"code", 200}, {"uncollected", true}, {"lrc", lyricObject(QString{})}}, 7);
        QVERIFY2(uncollected.ok(), qPrintable(uncollected.error.message));
        QVERIFY(!uncollected.value->pureMusic);
        QVERIFY(uncollected.value->uncollected);
    }

    void nonOkCodeIsApiError()
    {
        const auto parsed = parseSongLyrics(QJsonObject{{"code", 301}}, 7);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Api);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/song/lyric/v1"));
        QCOMPARE(parsed.error.code, 301);
    }

    // The old Store treated a missing code as failure; keep that behaviour now
    // that the envelope check lives in the parser.
    void missingCodeIsApiError()
    {
        const auto parsed = parseSongLyrics(QJsonObject{}, 7);
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Api);
        QCOMPARE(parsed.error.code, 0);
    }
};

QTEST_GUILESS_MAIN(LyricsTest)
#include "lyrics_test.moc"
