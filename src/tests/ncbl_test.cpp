#include "../ncbl.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTest>
#include <QtEndian>

#include <zstd.h>

namespace {

quint16 le16(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint16>(data.constData() + offset);
}

quint32 le32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

struct Frame {
    quint16 length = 0;
    quint32 sequence = 0;
    QByteArray cipher;
};

QList<Frame> readFrames(const QByteArray &data, int offset)
{
    QList<Frame> frames;
    while (offset < data.size()) {
        if (offset + 6 > data.size())
            return {};
        const quint16 length = le16(data, offset);
        const quint32 sequence = le32(data, offset + 2);
        offset += 6;
        if (offset + length > data.size())
            return {};
        frames.append({length, sequence, data.mid(offset, length)});
        offset += length;
    }
    return frames;
}

QByteArray decryptFrames(const QList<Frame> &frames, const QByteArray &key, quint32 counter,
                         const QByteArray &nonce)
{
    QByteArray out;
    for (const Frame &frame : frames)
        out += Ncbl::chacha20(key, counter, nonce, frame.cipher);
    return out;
}

Ncbl::Song sampleSong()
{
    Ncbl::Song song;
    song.id = 1234567890123.0;
    song.name = QStringLiteral("unused name");
    song.artist = QStringLiteral("unused artist");
    song.bitrate = 320000;
    song.level = QStringLiteral("exhigh");
    song.fee = 8;
    song.duration = 245;
    song.sourceId = QStringLiteral("id-from-playlist");
    song.sourceType = QStringLiteral("track");
    song.sourceName = QStringLiteral("list");
    return song;
}

} // namespace

class NcblTest : public QObject {
    Q_OBJECT

private slots:
    void chachaRfc8439Vector();
    void chachaLengthAndKeyRules();
    void rsaWrapKnownValues();
    void containerRoundTrip();
    void containerFramesLargeBody();
    void containerEmptyBody();
    void encryptRandomContainer();
    void multipartShape();
    void contextShape();
    void contextCidStable();
    void recordsShape();
    void plvFields();
    void pldFields();
};

void NcblTest::chachaRfc8439Vector()
{
    const QByteArray key = QByteArray::fromHex(
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const QByteArray nonce = QByteArray::fromHex("000000000000004a00000000");
    const QByteArray plain =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip "
        "for the future, sunscreen would be it.";
    const QByteArray expected = QByteArray::fromHex(
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
        "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
        "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
        "5af90bbf74a35be6b40b8eedf2785e42874d");
    QCOMPARE(int(expected.size()), int(plain.size()));
    QCOMPARE(Ncbl::chacha20(key, 1, nonce, plain), expected);
}

void NcblTest::chachaLengthAndKeyRules()
{
    const QByteArray key(32, 'k');
    const QByteArray nonce(12, 'n');
    const QByteArray plain = QByteArrayLiteral("0123456789 abcdefghijklmnopqrstuvwxyz");
    const QByteArray cipher = Ncbl::chacha20(key, 7, nonce, plain);
    QCOMPARE(int(cipher.size()), int(plain.size()));
    QCOMPARE(Ncbl::chacha20(key, 7, nonce, cipher), plain);
    QCOMPARE(int(Ncbl::chacha20(key, 7, nonce, QByteArray()).size()), 0);
    QCOMPARE(int(Ncbl::chacha20(QByteArray(31, 'k'), 7, nonce, plain).size()), 0);
    QCOMPARE(int(Ncbl::chacha20(key, 7, QByteArray(11, 'n'), plain).size()), 0);
}

void NcblTest::rsaWrapKnownValues()
{
    QCOMPARE(int(Ncbl::rsaWrap(QByteArray(31, '\0')).size()), 0);
    QCOMPARE(Ncbl::rsaWrap(QByteArray(32, '\0')).toHex(),
             QByteArray("0000000000000000000000000000000000000000000000000000000000000000"));

    QByteArray one(32, '\0');
    one[31] = '\x01';
    QCOMPARE(Ncbl::rsaWrap(one).toHex(),
             QByteArray("0000000000000000000000000000000000000000000000000000000000000001"));

    QByteArray twoTo248(32, '\0');
    twoTo248[0] = '\x01';
    QCOMPARE(Ncbl::rsaWrap(twoTo248).toHex(),
             QByteArray("2383728d8a577bb96baadf19009e3ad08688cbf04a74aa45ffb8889c2da30560"));

    QByteArray sample(32, '\0');
    for (int i = 0; i < 32; ++i)
        sample[i] = char(i);
    QCOMPARE(Ncbl::rsaWrap(sample).toHex(),
             QByteArray("dda20437ce173c34273cb03bffb85db8f3ce53bc2f1334a752303d26890094af"));
}

void NcblTest::containerRoundTrip()
{
    const QByteArray meta =
        QByteArrayLiteral("{\"MUSIC_U\":\"fake\",\"WNMCID\":\"abcdef.1.01.0\"}");
    const QByteArray body = QByteArrayLiteral("listening record payload ").repeated(400);

    Ncbl::Detail::ContainerInput input;
    input.keyA = QByteArray::fromHex(
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    input.uuid = QByteArray::fromHex("00112233445546778899aabbccddeeff");
    input.sequence = 0x4321;

    const QByteArray container = Ncbl::Detail::container(meta, body, input);
    QVERIFY(container.size() > 70);

    QCOMPARE(container.left(4), QByteArrayLiteral("NCBL"));
    QCOMPARE(le32(container, 4), 3u);
    const int metaOffset = le16(container, 8);
    QCOMPARE(metaOffset, 70 + 4 + int(meta.size()));
    QCOMPARE(container.mid(10, 16), input.uuid);
    const QByteArray expectedKeyB = Ncbl::rsaWrap(input.keyA);
    QCOMPARE(container.mid(26, 32), expectedKeyB);

    const quint32 counter = qFromLittleEndian<quint32>(input.uuid.constData() + 12) >> 2;
    const QByteArray nonce = input.uuid.left(12);

    QCOMPARE(le16(container, 70), quint16(0x4343));
    const int metaLength = le16(container, 72);
    QCOMPARE(metaLength, int(meta.size()));
    const QByteArray decryptedMeta =
        Ncbl::chacha20(expectedKeyB, counter, nonce, container.mid(74, metaLength));
    QCOMPARE(decryptedMeta, meta);

    const QList<Frame> frames = readFrames(container, metaOffset);
    QCOMPARE(int(frames.size()), 1);
    QCOMPARE(frames.first().sequence, quint32(input.sequence));
    QCOMPARE(le32(container, 58), quint32(input.sequence));
    QCOMPARE(le32(container, 62), quint32(input.sequence));
    QCOMPARE(int(le32(container, 66)), int(container.size()) - metaOffset);

    const QByteArray compressed = decryptFrames(frames, input.keyA, counter, nonce);
    const unsigned long long contentSize =
        ZSTD_getFrameContentSize(compressed.constData(), size_t(compressed.size()));
    QCOMPARE(contentSize, static_cast<unsigned long long>(body.size()));
    QByteArray restored(int(contentSize), Qt::Uninitialized);
    const size_t written = ZSTD_decompress(restored.data(), size_t(restored.size()),
                                           compressed.constData(), size_t(compressed.size()));
    QVERIFY(!ZSTD_isError(written));
    QCOMPARE(restored, body);
}

void NcblTest::containerFramesLargeBody()
{
    const QByteArray meta = QByteArrayLiteral("{\"os\":\"pc\"}");
    QByteArray body(2 * 0x8000 + 777, Qt::Uninitialized);
    QRandomGenerator random(0x5EED1234u);
    for (char &byte : body)
        byte = char(random.generate() & 0xFF);

    Ncbl::Detail::ContainerInput input;
    input.keyA = QByteArray(32, 'A');
    input.uuid = QByteArray::fromHex("00112233445546778899aabbccddeeff");
    input.sequence = 0xFFFF;

    const QByteArray container = Ncbl::Detail::container(meta, body, input);
    const int metaOffset = le16(container, 8);
    const QList<Frame> frames = readFrames(container, metaOffset);

    const quint32 counter = qFromLittleEndian<quint32>(input.uuid.constData() + 12) >> 2;
    const QByteArray nonce = input.uuid.left(12);
    const QByteArray compressed = decryptFrames(frames, input.keyA, counter, nonce);

    QCOMPARE(int(frames.size()), 3);
    for (int i = 0; i < int(frames.size()) - 1; ++i) {
        QCOMPARE(int(frames.at(i).length), 0x8000);
        QCOMPARE(frames.at(i).sequence, 0xFFFFu + quint32(i));
    }
    QVERIFY(int(frames.last().length) > 0);
    QVERIFY(int(frames.last().length) <= 0x8000);
    QCOMPARE(frames.last().sequence, 0xFFFFu + quint32(frames.size() - 1));
    QCOMPARE(le32(container, 58), 0xFFFFu);
    QCOMPARE(le32(container, 62), 0xFFFFu + quint32(frames.size() - 1));
    QCOMPARE(int(le32(container, 66)), int(container.size()) - metaOffset);

    const unsigned long long contentSize =
        ZSTD_getFrameContentSize(compressed.constData(), size_t(compressed.size()));
    QCOMPARE(contentSize, static_cast<unsigned long long>(body.size()));
    QByteArray restored(int(contentSize), Qt::Uninitialized);
    const size_t written = ZSTD_decompress(restored.data(), size_t(restored.size()),
                                           compressed.constData(), size_t(compressed.size()));
    QVERIFY(!ZSTD_isError(written));
    QCOMPARE(restored, body);
}

void NcblTest::containerEmptyBody()
{
    const QByteArray meta = QByteArrayLiteral("{\"os\":\"pc\"}");
    Ncbl::Detail::ContainerInput input;
    input.keyA = QByteArray(32, 'A');
    input.uuid = QByteArray::fromHex("00112233445546778899aabbccddeeff");
    input.sequence = 0x0102;

    const QByteArray container = Ncbl::Detail::container(meta, QByteArray(), input);
    const int metaOffset = le16(container, 8);
    const int metaLength = le16(container, 72);
    const QList<Frame> frames = readFrames(container, metaOffset);

    QCOMPARE(int(frames.size()), 1);
    QCOMPARE(int(frames.first().length), 0);
    QCOMPARE(int(frames.first().cipher.size()), 0);
    QCOMPARE(frames.first().sequence, quint32(0x0102));
    QCOMPARE(le32(container, 58), quint32(0x0102));
    QCOMPARE(le32(container, 62), quint32(0x0102));
    QCOMPARE(int(le32(container, 66)), 6);
    QCOMPARE(int(container.size()) - metaOffset, 6);
    QCOMPARE(metaOffset, 70 + 4 + int(meta.size()));
    QCOMPARE(metaLength, int(meta.size()));
}

void NcblTest::encryptRandomContainer()
{
    const QByteArray meta = QByteArrayLiteral("{\"os\":\"pc\",\"appver\":\"3.1.37.205354\"}");
    const QByteArray body = QByteArrayLiteral("record").repeated(100);
    const QByteArray container = Ncbl::encrypt(meta, body);
    QVERIFY(container.size() > 70);

    QCOMPARE(container.left(4), QByteArrayLiteral("NCBL"));
    QCOMPARE(le32(container, 4), 3u);

    const QByteArray uuid = container.mid(10, 16);
    QCOMPARE(int(static_cast<quint8>(uuid.at(6)) & 0xF0), 0x40);
    QCOMPARE(int(static_cast<quint8>(uuid.at(8)) & 0xC0), 0x80);

    const QByteArray keyB = container.mid(26, 32);
    const int metaOffset = le16(container, 8);
    QCOMPARE(metaOffset, 70 + 4 + int(meta.size()));
    QCOMPARE(le16(container, 70), quint16(0x4343));
    const int metaLength = le16(container, 72);
    QCOMPARE(metaLength, int(meta.size()));

    const quint32 counter = qFromLittleEndian<quint32>(uuid.constData() + 12) >> 2;
    const QByteArray decryptedMeta =
        Ncbl::chacha20(keyB, counter, uuid.left(12), container.mid(74, metaLength));
    QCOMPARE(decryptedMeta, meta);

    QCOMPARE(le32(container, 58), le32(container, 62));
    QCOMPARE(int(le32(container, 66)), int(container.size()) - metaOffset);

    const QByteArray other = Ncbl::encrypt(meta, body);
    QVERIFY(other.mid(10, 16) != uuid || other.mid(26, 32) != keyB);
}

void NcblTest::multipartShape()
{
    QByteArray payload;
    payload.append(char(0));
    payload.append(char(1));
    payload += QByteArrayLiteral("fake-encrypted-payload");

    const Ncbl::Bytes bytes = Ncbl::multipart(payload);
    const QString boundary = bytes.boundary;
    QVERIFY(!boundary.isEmpty());
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(boundary).hasMatch());

    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("^op_(\\d{5})_0_(\\d+)$")).match(bytes.fileName);
    QVERIFY(match.hasMatch());
    const int number = match.captured(1).toInt();
    QVERIFY(number >= 10000 && number <= 99999);
    bool ok = false;
    const qulonglong suffix = match.captured(2).toULongLong(&ok);
    QVERIFY(ok);
    QVERIFY(suffix <= 0xFFFFFFFFull);

    const QByteArray rawBoundary = boundary.toLatin1();
    QByteArray expected = QByteArrayLiteral("--") + rawBoundary + QByteArrayLiteral("\r\n");
    expected += QByteArrayLiteral("Content-Disposition: form-data; name=\"file\"; filename=\"")
        + bytes.fileName.toLatin1() + QByteArrayLiteral("\"\r\n");
    expected += QByteArrayLiteral("Content-Type: multipart/form-data\r\n\r\n");
    expected += payload;
    expected += QByteArrayLiteral("\r\n--") + rawBoundary + QByteArrayLiteral("--\r\n");
    QCOMPARE(bytes.body, expected);
}

void NcblTest::contextShape()
{
    // 测试只使用明显的假值，绝不能放进真实 cookie。
    const QMap<QString, QString> jar{
        {QStringLiteral("MUSIC_U"), QStringLiteral("fake-music-u")},
        {QStringLiteral("__csrf"), QStringLiteral("fake-csrf")},
        {QStringLiteral("NMTID"), QStringLiteral("fake-nmtid")},
    };
    const Ncbl::Context ctx = Ncbl::context(jar);

    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{6}\\.\\d+\\.01\\.0$"))
                .match(ctx.cid)
                .hasMatch());
    const QString expectedCookie = QStringLiteral(
        "JSESSIONID-WYYY=; MUSIC_U=fake-music-u; NMTID=fake-nmtid; WEVNSM=1.0.0; "
        "WNMCID=%1; __csrf=fake-csrf; __remember_me=true; _iuqxldmzr_=33; "
        "_ntes_nnid=,; _ntes_nuid=; appver=3.1.37.205354; channel=netease; "
        "clientSign=; deviceId=; mode=; ntes_kaola_ad=1; os=pc; "
        "osver=Microsoft-Windows-10-Professional-build-19045-64bit")
        .arg(ctx.cid);
    QCOMPARE(ctx.cookie, expectedCookie);

    const QJsonObject expectedMeta{
        {QStringLiteral("JSESSIONID-WYYY"), QString()},
        {QStringLiteral("MUSIC_U"), QStringLiteral("fake-music-u")},
        {QStringLiteral("NMTID"), QStringLiteral("fake-nmtid")},
        {QStringLiteral("WEVNSM"), QStringLiteral("1.0.0")},
        {QStringLiteral("WNMCID"), ctx.cid},
        {QStringLiteral("__csrf"), QStringLiteral("fake-csrf")},
        {QStringLiteral("_iuqxldmzr_"), QStringLiteral("33")},
        {QStringLiteral("_ntes_nnid"), QStringLiteral(",")},
        {QStringLiteral("_ntes_nuid"), QString()},
        {QStringLiteral("appver"), QStringLiteral("3.1.37.205354")},
        {QStringLiteral("channel"), QStringLiteral("netease")},
        {QStringLiteral("clientSign"), QString()},
        {QStringLiteral("deviceId"), QString()},
        {QStringLiteral("mode"), QString()},
        {QStringLiteral("ntes_kaola_ad"), QStringLiteral("1")},
        {QStringLiteral("os"), QStringLiteral("pc")},
        {QStringLiteral("osver"),
         QStringLiteral("Microsoft-Windows-10-Professional-build-19045-64bit")},
    };
    QCOMPARE(QJsonDocument::fromJson(ctx.meta).object(), expectedMeta);
    QCOMPARE(ctx.meta, QJsonDocument(expectedMeta).toJson(QJsonDocument::Compact));
    QVERIFY(!expectedMeta.contains(QStringLiteral("__remember_me")));

    const qint64 stamp = ctx.cid.section(u'.', 1, 1).toLongLong();
    QVERIFY(qAbs(stamp - QDateTime::currentMSecsSinceEpoch()) < 5000);
}

void NcblTest::contextCidStable()
{
    const QMap<QString, QString> jar{
        {QStringLiteral("MUSIC_U"), QStringLiteral("fake-music-u")},
    };
    const Ncbl::Context first = Ncbl::context(jar);
    const Ncbl::Context second = Ncbl::context(jar);
    QCOMPARE(first.cid, second.cid);
    QCOMPARE(second.cookie, first.cookie);
    QVERIFY(first.cookie.contains(QStringLiteral("WNMCID=") + first.cid));
    QCOMPARE(QJsonDocument::fromJson(first.meta)
                 .object()
                 .value(QStringLiteral("WNMCID"))
                 .toString(),
             first.cid);
}

void NcblTest::recordsShape()
{
    const QByteArray firstAction =
        QByteArrayLiteral("_plv") + QByteArray(1, '\x01') + QByteArrayLiteral("{}");
    const QByteArray secondAction =
        QByteArrayLiteral("_pld") + QByteArray(1, '\x01') + QByteArrayLiteral("[]");
    const QByteArray out = Ncbl::records({
        {1700000000123, firstAction},
        {1700000000456, secondAction},
    });

    QByteArray expected = QByteArrayLiteral("1700000000123") + QByteArray(1, '\x01') + firstAction;
    expected += QByteArrayLiteral("1700000000456") + QByteArray(1, '\x01') + secondAction;
    QCOMPARE(out, expected);
}

void NcblTest::plvFields()
{
    const Ncbl::Song song = sampleSong();
    const Ncbl::Context ctx;

    const QJsonObject expected{
        {QStringLiteral("mode"), QStringLiteral("circulation")},
        {QStringLiteral("download"), 0},
        {QStringLiteral("alg"), QString()},
        {QStringLiteral("status"), QStringLiteral("front")},
        {QStringLiteral("id"), QStringLiteral("1234567890123")},
        {QStringLiteral("bitrate"), 320000},
        {QStringLiteral("type"), QStringLiteral("song")},
        {QStringLiteral("is_listentogether"), 0},
        {QStringLiteral("source"), QStringLiteral("list")},
        {QStringLiteral("is_heart"), 0},
        {QStringLiteral("resource_ratio"), QString()},
        {QStringLiteral("resource_time"), 245},
        {QStringLiteral("musiceffect_id"), QString()},
        {QStringLiteral("app_mode"), 2},
        {QStringLiteral("bitrate_level"), QStringLiteral("exhigh")},
        {QStringLiteral("vipType"), QString()},
        {QStringLiteral("fee"), 8},
        {QStringLiteral("file"), 4},
        {QStringLiteral("rightSource"), 0},
        {QStringLiteral("sourceId"), QStringLiteral("id-from-playlist")},
        {QStringLiteral("sourcetype"), QStringLiteral("track")},
        {QStringLiteral("libra_abt"), QString()},
        {QStringLiteral("channel"), QStringLiteral("netease")},
        {QStringLiteral("curStartChannel"), QString()},
    };
    QCOMPARE(Ncbl::plv(song, ctx), expected);
}

void NcblTest::pldFields()
{
    const Ncbl::Song song = sampleSong();
    const Ncbl::Context ctx;
    const QJsonObject expected{
        {QStringLiteral("mode"), QStringLiteral("circulation")},
        {QStringLiteral("download"), 0},
        {QStringLiteral("alg"), QString()},
        {QStringLiteral("status"), QStringLiteral("front")},
        {QStringLiteral("id"), QStringLiteral("1234567890123")},
        {QStringLiteral("bitrate"), 320000},
        {QStringLiteral("type"), QStringLiteral("song")},
        {QStringLiteral("is_listentogether"), 0},
        {QStringLiteral("source"), QStringLiteral("list")},
        {QStringLiteral("is_heart"), 0},
        {QStringLiteral("resource_ratio"), QString()},
        {QStringLiteral("resource_time"), 245},
        {QStringLiteral("musiceffect_id"), QStringLiteral("1001")},
        {QStringLiteral("app_mode"), 1},
        {QStringLiteral("bitrate_level"), QStringLiteral("exhigh")},
        {QStringLiteral("vipType"), QString()},
        {QStringLiteral("fee"), 8},
        {QStringLiteral("file"), 4},
        {QStringLiteral("rightSource"), 0},
        {QStringLiteral("sourceId"), QStringLiteral("id-from-playlist")},
        {QStringLiteral("sourcetype"), QStringLiteral("track")},
        {QStringLiteral("libra_abt"), QString()},
        {QStringLiteral("channel"), QStringLiteral("netease")},
        {QStringLiteral("curStartChannel"), QString()},
        {QStringLiteral("time"), 180},
        {QStringLiteral("realtime"), 180},
        {QStringLiteral("lyriceffect"), QStringLiteral("default")},
        {QStringLiteral("displayMode"), QStringLiteral("classic")},
        {QStringLiteral("end"), QStringLiteral("interrupt")},
    };
    QCOMPARE(Ncbl::pld(song, ctx, 180), expected);
}

QTEST_MAIN(NcblTest)
#include "ncbl_test.moc"
