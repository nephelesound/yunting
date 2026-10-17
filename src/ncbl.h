// SPDX-License-Identifier: GPL-3.0-or-later
// 构造网易云客户端日志（NCBL v3）容器：ChaCha20 加密、zstd 分帧和 multipart 上传体。
// 本模块只生成字节，上传由 NcmClient::postClientLog 负责。
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QtGlobal>

namespace Ncbl {

struct Song {
    double id = 0;
    QString name;
    QString artist;
    int bitrate = 320000;
    QString level = QStringLiteral("exhigh");
    int fee = 0;
    int duration = 0;
    QString sourceId;
    QString sourceType = QStringLiteral("track");
    QString sourceName = QStringLiteral("track");
};

struct Context {
    QString cookie;
    QByteArray meta;
    QString cid;
};

struct Bytes {
    QByteArray body;
    QString boundary;
    QString fileName;
};

// key 恰好 32 字节、nonce 恰好 12 字节；counter 是首个 64 字节块的计数器。
QByteArray chacha20(const QByteArray &key, quint32 counter, const QByteArray &nonce,
                    const QByteArray &data);

// 无填充 RSA：把 32 字节 key 当成大端整数做 m^65537 mod N，结果固定 32 字节。
QByteArray rsaWrap(const QByteArray &key);

// meta 用 RSA 包装后的密钥加密，body 压缩后分帧用原始密钥加密。
QByteArray encrypt(const QByteArray &meta, const QByteArray &body);

Bytes multipart(const QByteArray &payload);

// jar 只读取 MUSIC_U、__csrf、NMTID；cid 每个进程只生成一次。
Context context(const QMap<QString, QString> &jar);

// 每条记录 = 十进制毫秒时间 + 0x01 + 动作字节。
QByteArray records(const QList<QPair<qint64, QByteArray>> &items);

QJsonObject plv(const Song &song, const Context &ctx);

QJsonObject pld(const Song &song, const Context &ctx, int playedSeconds);

// 仅供测试：密钥、UUID、起始序号由调用方注入，生产路径由 encrypt() 随机生成。
namespace Detail {
struct ContainerInput {
    QByteArray keyA;
    QByteArray uuid;
    quint32 sequence = 0;
};

QByteArray container(const QByteArray &meta, const QByteArray &body,
                     const ContainerInput &input);
} // namespace Detail

} // namespace Ncbl
