#pragma once
// NCBL 客户端日志（官方 PC 端 PLV/PLD）的纯编码层：ChaCha20、RSA 包装、zstd、帧格式、
// PLV/PLD 字段与 multipart。网络发送在 NcmClient::postClientLog；本文件可单测。
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QPair>

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
    QString cookie;   // 完整 Cookie 头（含设备/客户端字段）
    QByteArray meta; // 加密前的 meta JSON
    QString cid;
};

QByteArray chacha20(const QByteArray &key, quint32 counter, const QByteArray &nonce,
                    const QByteArray &data);
QByteArray rsaWrap(const QByteArray &key);
QByteArray encrypt(const QByteArray &meta, const QByteArray &body);

struct Bytes {
    QByteArray body;
    QString boundary;
    QString fileName;
};
Bytes multipart(const QByteArray &payload);

Context context(const QMap<QString, QString> &jar);
QByteArray records(const QList<QPair<qint64, QByteArray>> &records); // (time, action\x01json)
QJsonObject plv(const Song &song, const Context &ctx);
QJsonObject pld(const Song &song, const Context &ctx, int playedSeconds);

} // namespace Ncbl
