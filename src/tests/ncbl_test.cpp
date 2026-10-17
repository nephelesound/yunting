// NCBL 编码层的结构测试：ChaCha20 对称性、帧头、multipart 与 PLV/PLD 字段。
// 不上传、不接触账号；服务器接受度只能在真实会话里验证。
#include "../ncbl.h"

#include <QTest>

class NcblTest : public QObject {
    Q_OBJECT
private slots:
    void chachaRoundTrip()
    {
        const QByteArray key(32, '\x01');
        const QByteArray nonce(12, '\x02');
        const QByteArray plain = "cloud listening";
        const QByteArray cipher = Ncbl::chacha20(key, 7, nonce, plain);
        QCOMPARE(Ncbl::chacha20(key, 7, nonce, cipher), plain);
        QVERIFY(cipher != plain);
    }

    void encryptedFrameHasHeader()
    {
        const QByteArray meta = "{\"MUSIC_U\":\"x\"}";
        const QByteArray payload = "123\x01_plv\x01{}";
        const QByteArray frame = Ncbl::encrypt(meta, payload);
        QVERIFY(frame.size() > 70);
        QCOMPARE(frame.left(4), QByteArrayLiteral("NCBL"));
        const int headerLen = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar *>(frame.constData() + 8));
        QVERIFY(headerLen > 70);
        QVERIFY(headerLen < frame.size());
    }

    void multipartHasFileAndBoundary()
    {
        const QByteArray payload = "NCBLDATA";
        const Ncbl::Bytes body = Ncbl::multipart(payload);
        QVERIFY(!body.boundary.isEmpty());
        QVERIFY(body.body.contains("filename=\"" + body.fileName.toUtf8() + "\""));
        QVERIFY(body.body.contains(payload));
        QVERIFY(body.body.endsWith("--" + body.boundary.toUtf8() + "--\r\n"));
    }

    void plvAndPldCarryIdentityAndTime()
    {
        Ncbl::Song song;
        song.id = 42;
        song.name = "song";
        song.artist = "artist";
        song.duration = 200;
        song.sourceId = QStringLiteral("42");
        const Ncbl::Context ctx = Ncbl::context({});
        const QJsonObject plv = Ncbl::plv(song, ctx);
        const QJsonObject pld = Ncbl::pld(song, ctx, 120);
        QCOMPARE(plv.value("id").toString(), QStringLiteral("42"));
        QCOMPARE(plv.value("sourceId").toString(), QStringLiteral("42"));
        QCOMPARE(pld.value("time").toInt(), 120);
        QCOMPARE(pld.value("realtime").toInt(), 120);
        QCOMPARE(pld.value("end").toString(), QStringLiteral("interrupt"));
    }
};

QTEST_GUILESS_MAIN(NcblTest)
#include "ncbl_test.moc"
