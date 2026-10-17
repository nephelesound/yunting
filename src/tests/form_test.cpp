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

    // GET 的 query 走 QUrlQuery：'+' 必须自己编码，否则服务端按表单规则解成空格
    void getQueryEncodesPlus()
    {
        QUrl url("https://music.163.com/api/search/get");
        url.setQuery(ncm::query({{"s", "C++ a+b"}, {"x", "1"}}));
        QCOMPARE(url.toString(QUrl::FullyEncoded),
                 QStringLiteral("https://music.163.com/api/search/get?s=C%2B%2B%20a%2Bb&x=1"));
    }
};

QTEST_GUILESS_MAIN(FormTest)
#include "form_test.moc"
