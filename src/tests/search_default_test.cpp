#include "sdk/search_default.h"

#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SearchDefaultTest : public QObject {
    Q_OBJECT
private slots:
    void parsesDefault()
    {
        const auto parsed = parseDefaultSearch(QJsonObject{
            {"data", QJsonObject{{"showKeyword", "显示"}, {"realkeyword", "真实"},
                                 {"searchType", 1}, {"action", 0},
                                 {"alg", "alg"}, {"imageUrl", "http://img"}}},
        });
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->showKeyword, QStringLiteral("显示"));
        QCOMPARE(parsed.value->realKeyword, QStringLiteral("真实"));
        QCOMPARE(parsed.value->searchType, 1);
        QCOMPARE(parsed.value->imageUrl, QStringLiteral("http://img"));
    }

    void missingDataIsParseError()
    {
        const auto parsed = parseDefaultSearch(QJsonObject{});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
    }

    void wrongFieldIsParseError()
    {
        const auto parsed = parseDefaultSearch(
            QJsonObject{{"data", QJsonObject{{"showKeyword", 1}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
    }
};

QTEST_GUILESS_MAIN(SearchDefaultTest)
#include "search_default_test.moc"
