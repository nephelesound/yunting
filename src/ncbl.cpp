#include "ncbl.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtEndian>
#include <zstd.h>

#include <openssl/bn.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr char kModulus[] =
    "fd90bd466ff9bc8a3fec2fbcf263b90d5c564879fa5d7aab89b31c1d5cb4139d";
constexpr int kHeaderFixed = 70;
constexpr int kMetaBlockType = 0x4343;
constexpr int kMaxFrame = 0x8000;

void put16(QByteArray &b, int at, quint16 v) { qToLittleEndian(v, b.data() + at); }
void put32(QByteArray &b, int at, quint32 v) { qToLittleEndian(v, b.data() + at); }

QByteArray randomBytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    RAND_bytes(reinterpret_cast<unsigned char *>(out.data()), n);
    return out;
}

QByteArray compress(const QByteArray &in)
{
    const size_t bound = ZSTD_compressBound(size_t(in.size()));
    QByteArray out(int(bound), Qt::Uninitialized);
    const size_t n = ZSTD_compress(out.data(), bound, in.constData(), size_t(in.size()), 3);
    if (ZSTD_isError(n))
        return {};
    out.resize(int(n));
    return out;
}

quint32 load32(const QByteArray &b, int at)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + at));
}

void quarter(quint32 *s, int a, int b, int c, int d)
{
    auto rotl = [](quint32 x, int n) { return (x << n) | (x >> (32 - n)); };
    s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl(s[d], 16);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl(s[b], 12);
    s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl(s[d], 8);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl(s[b], 7);
}

QByteArray chachaBlock(const QByteArray &key, quint32 counter, const QByteArray &nonce)
{
    quint32 state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        0, 0, 0, 0, 0, 0, 0, 0,
        counter, 0, 0, 0,
    };
    for (int i = 0; i < 8; ++i)
        state[4 + i] = load32(key, i * 4);
    state[13] = load32(nonce, 0);
    state[14] = load32(nonce, 4);
    state[15] = load32(nonce, 8);
    quint32 work[16];
    std::memcpy(work, state, sizeof(work));
    for (int i = 0; i < 10; ++i) {
        quarter(work, 0, 4, 8, 12); quarter(work, 1, 5, 9, 13);
        quarter(work, 2, 6, 10, 14); quarter(work, 3, 7, 11, 15);
        quarter(work, 0, 5, 10, 15); quarter(work, 1, 6, 11, 12);
        quarter(work, 2, 7, 8, 13); quarter(work, 3, 4, 9, 14);
    }
    QByteArray out(64, Qt::Uninitialized);
    for (int i = 0; i < 16; ++i)
        qToLittleEndian(work[i] + state[i], out.data() + i * 4);
    return out;
}
} // namespace

namespace Ncbl {

QByteArray chacha20(const QByteArray &key, quint32 counter, const QByteArray &nonce,
                    const QByteArray &data)
{
    QByteArray out(data.size(), Qt::Uninitialized);
    for (int off = 0; off < data.size(); off += 64) {
        const QByteArray ks = chachaBlock(key, counter + quint32(off / 64), nonce);
        const int end = std::min(off + 64, int(data.size()));
        for (int i = off; i < end; ++i)
            out[i] = data[i] ^ ks[i - off];
    }
    return out;
}

QByteArray rsaWrap(const QByteArray &key)
{
    BIGNUM *n = nullptr, *e = nullptr, *base = nullptr, *result = nullptr;
    BN_CTX *ctx = BN_CTX_new();
    BN_hex2bn(&n, kModulus);
    e = BN_new();
    BN_set_word(e, 65537);
    base = BN_bin2bn(reinterpret_cast<const unsigned char *>(key.constData()), key.size(), nullptr);
    result = BN_new();
    BN_mod_exp(result, base, e, n, ctx);
    QByteArray out(32, 0);
    BN_bn2binpad(result, reinterpret_cast<unsigned char *>(out.data()), 32);
    BN_free(n); BN_free(e); BN_free(base); BN_free(result); BN_CTX_free(ctx);
    return out;
}

QByteArray encrypt(const QByteArray &meta, const QByteArray &body)
{
    QByteArray keyA = randomBytes(32);
    if (static_cast<unsigned char>(keyA[0]) >= 0xa3)
        keyA[0] = char(0xa2);
    const QByteArray keyB = rsaWrap(keyA);

    QByteArray uuid = randomBytes(16);
    uuid[6] = char((uuid[6] & 0x0f) | 0x40);
    uuid[8] = char((uuid[8] & 0x3f) | 0x80);
    const QByteArray nonce = uuid.mid(0, 12);
    const quint32 counter = load32(uuid, 12) >> 2;
    const quint16 baseSeq = quint16(load32(randomBytes(4), 0));

    const QByteArray metaCipher = chacha20(keyB, counter, nonce, meta);
    QByteArray metaBlock;
    metaBlock.resize(4 + metaCipher.size());
    put16(metaBlock, 0, quint16(kMetaBlockType));
    put16(metaBlock, 2, quint16(metaCipher.size()));
    metaBlock.replace(4, metaCipher.size(), metaCipher);

    const QByteArray compressed = compress(body);
    QByteArray trailing;
    quint16 seq = baseSeq;
    if (compressed.isEmpty()) {
        QByteArray head(6, 0);
        put16(head, 0, 0);
        put32(head, 2, seq);
        trailing += head;
        ++seq;
    } else {
        for (int off = 0; off < compressed.size(); off += kMaxFrame) {
            const QByteArray slice = compressed.mid(off, kMaxFrame);
            const QByteArray cipher = chacha20(keyA, counter, nonce, slice);
            QByteArray head(6, 0);
            put16(head, 0, quint16(cipher.size()));
            put32(head, 2, seq);
            trailing += head;
            trailing += cipher;
            ++seq;
        }
    }
    const quint16 frames = quint16(seq - baseSeq);
    QByteArray header(kHeaderFixed, 0);
    header.replace(0, 4, QByteArrayLiteral("NCBL"));
    put32(header, 4, 3);
    put16(header, 8, quint16(kHeaderFixed + metaBlock.size()));
    header.replace(10, 16, uuid);
    header.replace(26, 32, keyB);
    put32(header, 58, baseSeq);
    put32(header, 62, quint32(baseSeq + frames - 1));
    put32(header, 66, quint32(trailing.size()));
    return header + metaBlock + trailing;
}

Bytes multipart(const QByteArray &payload)
{
    const QByteArray random = randomBytes(16).toHex();
    const QString boundary = QString::fromLatin1(random);
    const QString fileName = QStringLiteral("op_%1_0_%2")
        .arg(QRandomGenerator::global()->bounded(90000) + 10000)
        .arg(QRandomGenerator::global()->generate());
    const QString head = QStringLiteral("--%1\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%2\"\r\n"
                                        "Content-Type: multipart/form-data\r\n\r\n")
                             .arg(boundary, fileName);
    const QByteArray footer = QStringLiteral("\r\n--%1--\r\n").arg(boundary).toUtf8();
    return {head.toUtf8() + payload + footer, boundary, fileName};
}

Context context(const QMap<QString, QString> &jar)
{
    Context c;
    const QString musicU = jar.value(QStringLiteral("MUSIC_U"));
    const QString csrf = jar.value(QStringLiteral("__csrf"));
    const QString nmtid = jar.value(QStringLiteral("NMTID"));
    c.cid = QStringLiteral("%1.%2.01.0")
                .arg(QString::fromLatin1(randomBytes(3).toHex()))
                .arg(QDateTime::currentMSecsSinceEpoch());
    const QString appver = QStringLiteral("3.1.37.205354");
    const QString osver = QStringLiteral("Microsoft-Windows-10-Professional-build-19045-64bit");
    c.cookie = QStringLiteral(
        "JSESSIONID-WYYY=; MUSIC_U=%1; NMTID=%2; WEVNSM=1.0.0; WNMCID=%3; __csrf=%4; "
        "__remember_me=true; _iuqxldmzr_=33; _ntes_nnid=,; _ntes_nuid=; "
        "appver=%5; channel=netease; clientSign=; deviceId=; mode=; ntes_kaola_ad=1; "
        "os=pc; osver=%6").arg(musicU, nmtid, c.cid, csrf, appver, osver);
    c.meta = QJsonDocument(QJsonObject{
        {"JSESSIONID-WYYY", QString{}},
        {"MUSIC_U", musicU},
        {"NMTID", nmtid},
        {"WEVNSM", QStringLiteral("1.0.0")},
        {"WNMCID", c.cid},
        {"__csrf", csrf},
        {"_iuqxldmzr_", QStringLiteral("33")},
        {"_ntes_nnid", QStringLiteral(",")},
        {"_ntes_nuid", QString{}},
        {"appver", appver},
        {"channel", QStringLiteral("netease")},
        {"clientSign", QString{}},
        {"deviceId", QString{}},
        {"mode", QString{}},
        {"ntes_kaola_ad", QStringLiteral("1")},
        {"os", QStringLiteral("pc")},
        {"osver", osver},
    }).toJson(QJsonDocument::Compact);
    return c;
}

QByteArray records(const QList<QPair<qint64, QByteArray>> &items)
{
    QByteArray out;
    for (const auto &item : items) {
        out += QByteArray::number(item.first);
        out += '\x01';
        out += item.second;
    }
    return out;
}

QJsonObject plv(const Song &s, const Context &ctx)
{
    const QJsonObject o{
        {"mode", "circulation"},
        {"download", 0},
        {"alg", ""},
        {"status", "front"},
        {"id", QString::number(qlonglong(s.id))},
        {"bitrate", s.bitrate},
        {"type", "song"},
        {"is_listentogether", 0},
        {"source", s.sourceName},
        {"is_heart", 0},
        {"resource_ratio", ""},
        {"resource_time", s.duration},
        {"musiceffect_id", ""},
        {"app_mode", 2},
        {"bitrate_level", s.level},
        {"vipType", ""},
        {"fee", s.fee},
        {"file", 4},
        {"rightSource", 0},
        {"sourceId", s.sourceId},
        {"sourcetype", s.sourceType},
        {"libra_abt", ""},
        {"channel", "netease"},
        {"curStartChannel", ""},
    };
    Q_UNUSED(ctx);
    return o;
}

QJsonObject pld(const Song &s, const Context &ctx, int playedSeconds)
{
    QJsonObject o = plv(s, ctx);
    o.remove(QStringLiteral("_multirefers"));
    o.insert(QStringLiteral("time"), playedSeconds);
    o.insert(QStringLiteral("realtime"), playedSeconds);
    o.insert(QStringLiteral("musiceffect_id"), QStringLiteral("1001"));
    o.insert(QStringLiteral("app_mode"), 1);
    o.insert(QStringLiteral("lyriceffect"), QStringLiteral("default"));
    o.insert(QStringLiteral("displayMode"), QStringLiteral("classic"));
    o.insert(QStringLiteral("end"), QStringLiteral("interrupt"));
    o.insert(QStringLiteral("source"), s.sourceName);
    return o;
}

} // namespace Ncbl
