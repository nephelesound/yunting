#include "ncbl.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QStringList>
#include <QtEndian>

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace Ncbl {

namespace {

constexpr char kModulusHex[] =
    "fd90bd466ff9bc8a3fec2fbcf263b90d5c564879fa5d7aab89b31c1d5cb4139d";
constexpr int kFrameSize = 0x8000;
constexpr quint16 kMetaType = 0x4343;
constexpr int kHeaderSize = 70;

using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using BnCtxPtr = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;

quint32 loadLittle32(const char *data)
{
    return qFromLittleEndian<quint32>(data);
}

void appendLittle16(QByteArray &out, quint16 value)
{
    char buffer[2];
    qToLittleEndian(value, buffer);
    out.append(buffer, 2);
}

void appendLittle32(QByteArray &out, quint32 value)
{
    char buffer[4];
    qToLittleEndian(value, buffer);
    out.append(buffer, 4);
}

void writeLittle16(QByteArray &out, int offset, quint16 value)
{
    qToLittleEndian(value, out.data() + offset);
}

void writeLittle32(QByteArray &out, int offset, quint32 value)
{
    qToLittleEndian(value, out.data() + offset);
}

QByteArray randomBytes(int count)
{
    QByteArray out(count, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(out.data()), count) != 1)
        return {};
    return out;
}

inline quint32 rotateLeft(quint32 value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

inline void quarterRound(quint32 &a, quint32 &b, quint32 &c, quint32 &d)
{
    a += b;
    d ^= a;
    d = rotateLeft(d, 16);
    c += d;
    b ^= c;
    b = rotateLeft(b, 12);
    a += b;
    d ^= a;
    d = rotateLeft(d, 8);
    c += d;
    b ^= c;
    b = rotateLeft(b, 7);
}

void chacha20Block(const quint32 input[16], quint32 output[16])
{
    quint32 x[16];
    for (int i = 0; i < 16; ++i)
        x[i] = input[i];

    for (int round = 0; round < 10; ++round) {
        quarterRound(x[0], x[4], x[8], x[12]);
        quarterRound(x[1], x[5], x[9], x[13]);
        quarterRound(x[2], x[6], x[10], x[14]);
        quarterRound(x[3], x[7], x[11], x[15]);
        quarterRound(x[0], x[5], x[10], x[15]);
        quarterRound(x[1], x[6], x[11], x[12]);
        quarterRound(x[2], x[7], x[8], x[13]);
        quarterRound(x[3], x[4], x[9], x[14]);
    }

    for (int i = 0; i < 16; ++i)
        output[i] = x[i] + input[i];
}

// 曲目 id 是整数，但 QString::number(double) 默认会写成科学计数法。
QString decimalId(double id)
{
    return QString::number(id, 'f', 0);
}

} // namespace

QByteArray chacha20(const QByteArray &key, quint32 counter, const QByteArray &nonce,
                    const QByteArray &data)
{
    if (key.size() != 32 || nonce.size() != 12)
        return {};

    quint32 state[16];
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;
    for (int i = 0; i < 8; ++i)
        state[4 + i] = loadLittle32(key.constData() + 4 * i);
    for (int i = 0; i < 3; ++i)
        state[13 + i] = loadLittle32(nonce.constData() + 4 * i);

    QByteArray out = data;
    quint32 block[16];
    for (qsizetype offset = 0; offset < out.size(); offset += 64) {
        state[12] = counter++;
        chacha20Block(state, block);

        char stream[64];
        for (int i = 0; i < 16; ++i)
            qToLittleEndian(block[i], stream + 4 * i);

        const qsizetype count = std::min<qsizetype>(64, out.size() - offset);
        for (qsizetype i = 0; i < count; ++i)
            out[offset + i] = char(out.at(offset + i) ^ stream[i]);
    }
    return out;
}

QByteArray rsaWrap(const QByteArray &key)
{
    if (key.size() != 32)
        return {};

    BIGNUM *modulusRaw = nullptr;
    if (BN_hex2bn(&modulusRaw, kModulusHex) == 0) {
        BN_free(modulusRaw);
        return {};
    }
    BnPtr modulus(modulusRaw, &BN_free);
    BnPtr message(BN_bin2bn(reinterpret_cast<const unsigned char *>(key.constData()),
                            int(key.size()), nullptr),
                  &BN_free);
    BnPtr exponent(BN_new(), &BN_free);
    BnPtr result(BN_new(), &BN_free);
    BnCtxPtr ctx(BN_CTX_new(), &BN_CTX_free);
    if (!message || !exponent || !result || !ctx)
        return {};
    if (BN_set_word(exponent.get(), 65537) != 1)
        return {};
    if (BN_mod_exp(result.get(), message.get(), exponent.get(), modulus.get(), ctx.get()) != 1)
        return {};

    QByteArray out(32, Qt::Uninitialized);
    if (BN_bn2binpad(result.get(), reinterpret_cast<unsigned char *>(out.data()), 32) != 32)
        return {};
    return out;
}

QByteArray Detail::container(const QByteArray &meta, const QByteArray &body,
                             const ContainerInput &input)
{
    if (input.keyA.size() != 32 || input.uuid.size() != 16)
        return {};

    const QByteArray keyB = rsaWrap(input.keyA);
    if (keyB.size() != 32)
        return {};

    const QByteArray nonce = input.uuid.left(12);
    const quint32 counter = loadLittle32(input.uuid.constData() + 12) >> 2;

    // 偏移字段是 uint16，ciphertext 长度加上 70 字节头与 4 字节块头后必须放得下。
    if (meta.size() > 0xFFFF - kHeaderSize - 4)
        return {};

    const QByteArray metaCipher = chacha20(keyB, counter, nonce, meta);
    if (metaCipher.size() != meta.size())
        return {};
    QByteArray metaBlock;
    metaBlock.reserve(metaCipher.size() + 4);
    appendLittle16(metaBlock, kMetaType);
    appendLittle16(metaBlock, static_cast<quint16>(metaCipher.size()));
    metaBlock.append(metaCipher);

    QByteArray compressed;
    if (!body.isEmpty()) {
        const size_t bound = ZSTD_compressBound(size_t(body.size()));
        if (bound > size_t(std::numeric_limits<int>::max()))
            return {};
        compressed.resize(int(bound));
        const size_t written = ZSTD_compress(compressed.data(), size_t(compressed.size()),
                                             body.constData(), size_t(body.size()), 3);
        if (ZSTD_isError(written))
            return {};
        compressed.resize(int(written));
    }

    QByteArray frames;
    quint32 sequence = input.sequence;
    qsizetype offset = 0;
    // 空 body 也必须写一个 0 长度帧（只有帧头），不能直接跳过。
    do {
        const qsizetype chunk = std::min<qsizetype>(kFrameSize, compressed.size() - offset);
        const QByteArray cipher = chacha20(input.keyA, counter, nonce,
                                           compressed.mid(offset, chunk));
        if (cipher.size() != chunk)
            return {};
        appendLittle16(frames, static_cast<quint16>(cipher.size()));
        appendLittle32(frames, sequence);
        frames.append(cipher);
        offset += chunk;
        ++sequence;
    } while (offset < compressed.size());

    if (quint64(frames.size()) > 0xFFFFFFFFu)
        return {};
    const quint32 firstSequence = input.sequence;
    const quint32 lastSequence = sequence - 1;
    const int metaOffset = kHeaderSize + int(metaBlock.size());
    if (metaOffset > 0xFFFF)
        return {};

    QByteArray out(kHeaderSize, '\0');
    std::memcpy(out.data(), "NCBL", 4);
    writeLittle32(out, 4, 3);
    writeLittle16(out, 8, static_cast<quint16>(metaOffset));
    std::memcpy(out.data() + 10, input.uuid.constData(), 16);
    std::memcpy(out.data() + 26, keyB.constData(), 32);
    writeLittle32(out, 58, firstSequence);
    writeLittle32(out, 62, lastSequence);
    writeLittle32(out, 66, static_cast<quint32>(frames.size()));
    out.append(metaBlock);
    out.append(frames);
    return out;
}

QByteArray encrypt(const QByteArray &meta, const QByteArray &body)
{
    QByteArray keyA = randomBytes(32);
    if (keyA.size() != 32)
        return {};
    // m 必须小于模数；模数首字节是 0xfd，把首字节压到 0xA2 即可保证。
    if (static_cast<quint8>(keyA.at(0)) >= 0xA3)
        keyA[0] = char(0xA2);

    QByteArray uuid = randomBytes(16);
    if (uuid.size() != 16)
        return {};
    uuid[6] = char((static_cast<quint8>(uuid.at(6)) & 0x0F) | 0x40);
    uuid[8] = char((static_cast<quint8>(uuid.at(8)) & 0x3F) | 0x80);

    const quint32 sequence = QRandomGenerator::system()->bounded(0x10000u);
    return Detail::container(meta, body, {keyA, uuid, sequence});
}

Bytes multipart(const QByteArray &payload)
{
    const QByteArray random = randomBytes(16);
    if (random.size() != 16)
        return {};

    Bytes out;
    out.boundary = QString::fromLatin1(random.toHex());

    QRandomGenerator *generator = QRandomGenerator::system();
    out.fileName = QStringLiteral("op_%1_0_%2")
                       .arg(generator->bounded(10000u, 100000u))
                       .arg(generator->generate());

    const QByteArray boundary = out.boundary.toLatin1();
    const QByteArray fileName = out.fileName.toLatin1();
    QByteArray body;
    body += QByteArrayLiteral("--") + boundary + QByteArrayLiteral("\r\n");
    body += QByteArrayLiteral("Content-Disposition: form-data; name=\"file\"; filename=\"")
        + fileName + QByteArrayLiteral("\"\r\n");
    body += QByteArrayLiteral("Content-Type: multipart/form-data\r\n\r\n");
    body += payload;
    body += QByteArrayLiteral("\r\n--") + boundary + QByteArrayLiteral("--\r\n");
    out.body = body;
    return out;
}

Context context(const QMap<QString, QString> &jar)
{
    // 同一个运行中的客户端只上报一个 cid；static 初始化在 C++ 里是线程安全的。
    static const QString cid = [] {
        const quint32 prefix = QRandomGenerator::system()->generate() & 0xFFFFFFu;
        return QStringLiteral("%1.%2.01.0")
            .arg(QStringLiteral("%1").arg(prefix, 6, 16, QLatin1Char('0')),
                 QString::number(QDateTime::currentMSecsSinceEpoch()));
    }();

    const QList<QPair<QString, QString>> entries = {
        {QStringLiteral("JSESSIONID-WYYY"), QString()},
        {QStringLiteral("MUSIC_U"), jar.value(QStringLiteral("MUSIC_U"))},
        {QStringLiteral("NMTID"), jar.value(QStringLiteral("NMTID"))},
        {QStringLiteral("WEVNSM"), QStringLiteral("1.0.0")},
        {QStringLiteral("WNMCID"), cid},
        {QStringLiteral("__csrf"), jar.value(QStringLiteral("__csrf"))},
        {QStringLiteral("__remember_me"), QStringLiteral("true")},
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

    QStringList header;
    QJsonObject meta;
    for (const auto &entry : entries) {
        header << entry.first + QLatin1Char('=') + entry.second;
        if (entry.first != QStringLiteral("__remember_me"))
            meta.insert(entry.first, entry.second);
    }

    Context out;
    out.cid = cid;
    out.cookie = header.join(QStringLiteral("; "));
    out.meta = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    return out;
}

QByteArray records(const QList<QPair<qint64, QByteArray>> &items)
{
    QByteArray out;
    for (const auto &item : items) {
        out += QByteArray::number(item.first);
        out.append('\x01');
        out += item.second;
    }
    return out;
}

QJsonObject plv(const Song &song, const Context &ctx)
{
    Q_UNUSED(ctx);

    return QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("circulation")},
        {QStringLiteral("download"), 0},
        {QStringLiteral("alg"), QString()},
        {QStringLiteral("status"), QStringLiteral("front")},
        {QStringLiteral("id"), decimalId(song.id)},
        {QStringLiteral("bitrate"), song.bitrate},
        {QStringLiteral("type"), QStringLiteral("song")},
        {QStringLiteral("is_listentogether"), 0},
        {QStringLiteral("source"), song.sourceName},
        {QStringLiteral("is_heart"), 0},
        {QStringLiteral("resource_ratio"), QString()},
        {QStringLiteral("resource_time"), song.duration},
        {QStringLiteral("musiceffect_id"), QString()},
        {QStringLiteral("app_mode"), 2},
        {QStringLiteral("bitrate_level"), song.level},
        {QStringLiteral("vipType"), QString()},
        {QStringLiteral("fee"), song.fee},
        {QStringLiteral("file"), 4},
        {QStringLiteral("rightSource"), 0},
        {QStringLiteral("sourceId"), song.sourceId},
        {QStringLiteral("sourcetype"), song.sourceType},
        {QStringLiteral("libra_abt"), QString()},
        {QStringLiteral("channel"), QStringLiteral("netease")},
        {QStringLiteral("curStartChannel"), QString()},
    };
}

QJsonObject pld(const Song &song, const Context &ctx, int playedSeconds)
{
    QJsonObject out = plv(song, ctx);
    out.insert(QStringLiteral("time"), playedSeconds);
    out.insert(QStringLiteral("realtime"), playedSeconds);
    out.insert(QStringLiteral("musiceffect_id"), QStringLiteral("1001"));
    out.insert(QStringLiteral("app_mode"), 1);
    out.insert(QStringLiteral("lyriceffect"), QStringLiteral("default"));
    out.insert(QStringLiteral("displayMode"), QStringLiteral("classic"));
    out.insert(QStringLiteral("end"), QStringLiteral("interrupt"));
    out.insert(QStringLiteral("source"), song.sourceName);
    return out;
}

} // namespace Ncbl
