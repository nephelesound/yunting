#pragma once
// NetEase weapi request encoding: AES-128-CBC twice + raw RSA of the per-request key.
// Only the transport uses this; callers pass JSON objects and receive a form body.
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace ncm {

// AES-128-CBC with PKCS#7 padding; key and iv are exactly 16 bytes.
QByteArray aes128CbcEncrypt(const QByteArray &plain, const QByteArray &key, const QByteArray &iv);

// Encodes a weapi payload as `params=<base64>&encSecKey=<hex>` (already URL-encoded).
// Each call picks a fresh random 16-byte base62 key, as the browser client does.
QString weapiForm(const QJsonObject &payload);

// Exposed for tests: raw RSA_NO_PADDING encryption of a 128-byte block.
QByteArray rsaNoPadding(const QByteArray &block);

} // namespace ncm
