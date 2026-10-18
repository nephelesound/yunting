// Typed Social SDK contract for a user's event feed.
#include "sdk/social.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkSocialTest : public QObject {
    Q_OBJECT

    static QJsonObject eventObject()
    {
        QJsonObject user{{"userId", 1}, {"nickname", "网易云音乐"},
                         {"avatarUrl", "http://avatar"}};
        QJsonObject resource{{"id", 42}, {"name", "分享歌单"}, {"imgUrl", "http://cover"},
                             {"webUrl", "https://music.163.com/x"}, {"eventType", 13}};
        QJsonObject thread{{"id", "A_EV"}, {"resourceType", 2}, {"resourceInfo", resource}};
        QJsonObject info{{"title", "标题"}, {"description", "描述"},
                         {"likedCount", 7}, {"commentCount", 2}, {"shareCount", 1},
                         {"commentThread", thread}};
        return QJsonObject{{"id", 9}, {"actName", "宝藏音乐"}, {"eventTime", 1700000000000.0},
                           {"user", user}, {"info", info}};
    }

private slots:
    void parsesEvents()
    {
        const QJsonObject root{{"code", 200}, {"more", true}, {"size", 10},
                               {"lasttime", 1234},
                               {"events", QJsonArray{eventObject()}}};
        const auto result = parseUserEvents(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->more, true);
        QCOMPARE(result.value->lasttime, qint64(1234));
        const UserEvent &event = result.value->items.first();
        QCOMPARE(event.id, qint64(9));
        QCOMPARE(event.actionName, QStringLiteral("宝藏音乐"));
        QCOMPARE(event.user.name, QStringLiteral("网易云音乐"));
        QCOMPARE(event.title, QStringLiteral("标题"));
        QCOMPARE(event.likeCount, qint64(7));
        QCOMPARE(event.resource.id, qint64(42));
        QCOMPARE(event.resource.resourceType, 2);
    }

    void missingEventsIsParseError()
    {
        const auto result = parseUserEvents(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QVERIFY(result.error.message.contains(QStringLiteral("events")));
    }
};

QTEST_GUILESS_MAIN(SdkSocialTest)
#include "sdk_social_test.moc"
