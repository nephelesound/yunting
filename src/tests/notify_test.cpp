// Typed notification-counter SDK contract: GET /api/pl/count.
#include "sdk/notify.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class NotifyTest : public QObject {
    Q_OBJECT

private slots:
    void parsesAllCounters()
    {
        const auto result = parseNotificationCount(QJsonObject{
            {"code", 200},
            {"msg", 2},
            {"comment", 3},
            {"event", 4},
            {"follow", 5},
            {"notice", 6},
            {"forward", 7},
            {"newProgramCount", 42},
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->msg, 2);
        QCOMPARE(result.value->comment, 3);
        QCOMPARE(result.value->event, 4);
        QCOMPARE(result.value->follow, 5);
        QCOMPARE(result.value->notice, 6);
        QCOMPARE(result.value->forward, 7);
        QCOMPARE(result.value->newProgramCount, 42);
    }

    void emptyObjectIsAllZero()
    {
        const auto result = parseNotificationCount(QJsonObject{});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->msg, 0);
        QCOMPARE(result.value->comment, 0);
        QCOMPARE(result.value->event, 0);
        QCOMPARE(result.value->follow, 0);
        QCOMPARE(result.value->notice, 0);
        QCOMPARE(result.value->forward, 0);
        QCOMPARE(result.value->newProgramCount, 0);
    }

    void partialPayloadDefaultsMissingAndNullToZero()
    {
        const auto result = parseNotificationCount(QJsonObject{
            {"msg", 7},
            {"notice", QJsonValue::Null},
            {"newProgramCount", 42},
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->msg, 7);
        QCOMPARE(result.value->comment, 0);
        QCOMPARE(result.value->event, 0);
        QCOMPARE(result.value->follow, 0);
        QCOMPARE(result.value->notice, 0);
        QCOMPARE(result.value->forward, 0);
        QCOMPARE(result.value->newProgramCount, 42);
    }

    void wrongTypeReportsEndpointAndFieldPath()
    {
        const auto result =
            parseNotificationCount(QJsonObject{{"msg", QStringLiteral("2")}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/pl/count"));
        QCOMPARE(result.error.path, QStringLiteral("msg"));
        QVERIFY(result.error.message.contains(QStringLiteral("msg: expected number")));
    }

    void wrongTypeOnLaterCounterReportsThatField()
    {
        const auto result = parseNotificationCount(
            QJsonObject{{"msg", 1}, {"newProgramCount", true}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("newProgramCount"));
        QVERIFY(result.error.message.contains(
            QStringLiteral("newProgramCount: expected number")));
    }
};

QTEST_GUILESS_MAIN(NotifyTest)
#include "notify_test.moc"
