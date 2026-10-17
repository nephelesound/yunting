#include "../json_util.h"

#include <QTest>
#include <QUrlQuery>

class FormTest : public QObject {
    Q_OBJECT
private slots:
    void encodesPlusSpaceAndSeparators()
    {
        QUrlQuery form;
        form.addQueryItem("logs", "a+b c&d=e");
        QCOMPARE(QString::fromLatin1(ncm::formEncoded(form)),
                 QStringLiteral("logs=a%2Bb%20c%26d%3De"));
    }

    void keepsEmptyValue()
    {
        QUrlQuery form;
        form.addQueryItem("x", "");
        QCOMPARE(QString::fromLatin1(ncm::formEncoded(form)), QStringLiteral("x="));
    }
};

QTEST_GUILESS_MAIN(FormTest)
#include "form_test.moc"
