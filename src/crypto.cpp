#include "crypto.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QUrl>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <memory>

namespace ncm {

namespace {
constexpr auto PresetKey = "0CoJUm6Qyw8W8jud";
constexpr auto Iv = "0102030405060708";
constexpr auto Base62 = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr auto PublicKeyPem =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDgtQn2JZ34ZC28NWYpAUd98iZ3"
    "7BUrX/aKzmFbt7clFSs6sXqHauqKWqdtLkF2KexO40H1YTX8z2lSgBBOAxLsvakl"
    "V8k4cBFK9snQXE9/DDaFt6Rr7iVZMldczhC0JNgTz+SHXT6CBHuX3e9SdB1Ua44o"
    "ncaTWz7OBGLbCiK45wIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using CtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
} // namespace

QByteArray aes128CbcEncrypt(const QByteArray &plain, const QByteArray &key, const QByteArray &iv)
{
    if (key.size() != 16 || iv.size() != 16)
        return {};

    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                                   reinterpret_cast<const unsigned char *>(key.constData()),
                                   reinterpret_cast<const unsigned char *>(iv.constData())) != 1)
        return {};

    QByteArray out(plain.size() + EVP_CIPHER_block_size(EVP_aes_128_cbc()), Qt::Uninitialized);
    int written = 0;
    int total = 0;
    if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(out.data()), &written,
                          reinterpret_cast<const unsigned char *>(plain.constData()), plain.size())
        != 1)
        return {};
    total = written;
    if (EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(out.data()) + total, &written) != 1)
        return {};
    total += written;
    out.resize(total);
    return out;
}

QByteArray rsaNoPadding(const QByteArray &block)
{
    BioPtr bio(BIO_new_mem_buf(PublicKeyPem, -1), &BIO_free);
    if (!bio)
        return {};
    PkeyPtr key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), &EVP_PKEY_free);
    if (!key)
        return {};
    CtxPtr ctx(EVP_PKEY_CTX_new(key.get(), nullptr), &EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_encrypt_init(ctx.get()) != 1)
        return {};
    if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_NO_PADDING) != 1)
        return {};

    size_t outLen = 0;
    if (EVP_PKEY_encrypt(ctx.get(), nullptr, &outLen,
                         reinterpret_cast<const unsigned char *>(block.constData()), block.size())
        != 1)
        return {};
    QByteArray out(int(outLen), Qt::Uninitialized);
    if (EVP_PKEY_encrypt(ctx.get(), reinterpret_cast<unsigned char *>(out.data()), &outLen,
                         reinterpret_cast<const unsigned char *>(block.constData()), block.size())
        != 1)
        return {};
    out.resize(int(outLen));
    return out;
}

QString weapiForm(const QJsonObject &payload)
{
    // Browser client: 16 random bytes reduced to base62, then reversed for RSA.
    QByteArray secret(16, Qt::Uninitialized);
    for (int i = 0; i < secret.size(); ++i)
        secret[i] = Base62[QRandomGenerator::system()->bounded(62)];

    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray first = aes128CbcEncrypt(json, PresetKey, Iv).toBase64();
    const QByteArray second = aes128CbcEncrypt(first, secret, Iv).toBase64();
    if (second.isEmpty())
        return {};

    QByteArray reversed = secret;
    std::reverse(reversed.begin(), reversed.end());
    QByteArray padded(128 - reversed.size(), '\0');
    padded += reversed;
    const QByteArray encSecKey = rsaNoPadding(padded).toHex();
    if (encSecKey.isEmpty())
        return {};

    // QUrlQuery leaves '+' and '/' unescaped in a form body; '+' would arrive as a space.
    return QStringLiteral("params=") + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(second)))
        + QStringLiteral("&encSecKey=") + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(encSecKey)));
}

} // namespace ncm
