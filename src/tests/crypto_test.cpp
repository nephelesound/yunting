#include "../crypto.h"

#include <QJsonObject>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

class CryptoTest : public QObject {
    Q_OBJECT
private slots:
    void aesKnownVector()
    {
        const QByteArray key = QByteArray::fromHex("2b7e151628aed2a6abf7158809cf4f3c");
        const QByteArray iv = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
        const QByteArray plain = QByteArray::fromHex("6bc1bee22e409f96e93d7e117393172a");
        const QByteArray cipher = ncm::aes128CbcEncrypt(plain, key, iv);
        // NIST vector for the first block; PKCS#7 adds the second block.
        QCOMPARE(cipher.toHex(), QByteArray("7649abac8119b246cee98e9b12e9197d"
                                            "8964e0b149c10b7b682e6e39aaeb731c"));
    }

    void rsaProducesOneBlock()
    {
        QByteArray block(128, '\0');
        block[127] = 'x';
        const QByteArray out = ncm::rsaNoPadding(block);
        QCOMPARE(out.size(), 128);
        QCOMPARE(out.toHex(), QByteArray("87e6fb38f8dba791d1ca1e29d469b647805c59a1827640ff4571a30c32338047"
                                         "068c223bdcc5bf0782e91cfd8b3ab218e604891b7d64d7a0fc5389347783757"
                                         "fc7fbe0fabaef0389ad79ade13e0fae36b60e70597075120da68a9511f5594af"
                                         "6dab829b1712910be45e66ae2eda4f6ad27b88839ef738c162c9bba75c18a513f"));
    }

    void weapiFormShape()
    {
        const QString encoded = ncm::weapiForm(QJsonObject{{"id", 1}});
        // 表单值必须百分号编码：base64 的 '+' 在 application/x-www-form-urlencoded 里会被读成空格。
        const QString paramsPart = encoded.section(u'&', 0, 0).section(u'=', 1);
        QVERIFY(!paramsPart.contains(u'+'));
        QVERIFY(!paramsPart.contains(u'/'));
        QUrlQuery form(encoded);
        QCOMPARE(form.queryItemValue("encSecKey").size(), 256);
        const QByteArray params = QByteArray::fromBase64(
            QUrl::fromPercentEncoding(paramsPart.toUtf8()).toLatin1());
        QVERIFY(!params.isEmpty());
        QCOMPARE(params.size() % 16, 0);
    }
};

QTEST_MAIN(CryptoTest)
#include "crypto_test.moc"
