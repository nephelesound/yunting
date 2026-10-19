#include "sdk/friend_listening.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class FriendListeningTest : public QObject {
    Q_OBJECT
private slots:
    void parsesFriends()
    {
        const QJsonObject userInfo{
            {"userId", 42}, {"nickname", "Alice"},
            {"avatarUrl", "http://p1/a.jpg"},
            {"avatarDetail", QJsonObject{{"identityIconUrl", "http://p1/id.png"}}},
            {"signature", "hi"}, {"mutual", true}, {"followed", true},
            {"vipType", 11}, {"vipRights", QJsonObject{
                {"associator", QJsonObject{{"rights", true}}}}},
        };
        const QJsonObject friendObject{
            {"userInfo", userInfo},
            {"pendant", QJsonObject{{"id", 7}, {"image", "http://p1/p.png"}}},
            {"lastListen", true}, {"listenStatus", 1}, {"lastTime", 1234},
            {"online", true}, {"inRoom", false}, {"description", "在线"},
        };
        const QJsonObject root{
            {"code", 200},
            {"data", QJsonObject{
                {"mutualFollowLists", QJsonArray{friendObject}},
                {"streakGlobalSwitch", 1}, {"streakUserSwitch", 1},
                {"matchButtonText", "匹配"}}},
        };
        const auto parsed = parseFriendListening(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->items.size(), 1);
        const FriendListening &friendItem = parsed.value->items.first();
        QCOMPARE(friendItem.user.userId, qint64(42));
        QCOMPARE(friendItem.user.name, QStringLiteral("Alice"));
        QCOMPARE(friendItem.user.avatarPendant, QStringLiteral("http://p1/p.png"));
        QCOMPARE(friendItem.user.identityIcon, QStringLiteral("http://p1/id.png"));
        QCOMPARE(friendItem.user.vipLevel, QStringLiteral("vip"));
        QCOMPARE(friendItem.online, true);
        QCOMPARE(friendItem.lastListen, true);
        QCOMPARE(friendItem.lastTime, qint64(1234));
        QCOMPARE(friendItem.description, QStringLiteral("在线"));
        QCOMPARE(parsed.value->matchButtonText, QStringLiteral("匹配"));
    }

    // 真实回包里备注名在行上，没设备注时是 null
    void remarkOnTheRowWinsOverNickname()
    {
        const auto row = [](const QJsonValue &remark) {
            return QJsonObject{{"userInfo", QJsonObject{{"userId", 1}, {"nickname", "Alice"}}},
                               {"customNickname", remark}};
        };
        const auto parsed = parseFriendListening(QJsonObject{{"data", QJsonObject{
            {"mutualFollowLists", QJsonArray{row("小艾"), row(QJsonValue::Null)}}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->items.at(0).user.name, QStringLiteral("小艾"));
        QCOMPARE(parsed.value->items.at(1).user.name, QStringLiteral("Alice"));
    }

    void emptyListIsSuccess()
    {
        const auto parsed = parseFriendListening(
            QJsonObject{{"data", QJsonObject{{"mutualFollowLists", QJsonArray{}}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QVERIFY(parsed.value->items.isEmpty());
    }

    void wrongFieldIsParseError()
    {
        const auto parsed = parseFriendListening(QJsonObject{
            {"data", QJsonObject{{"mutualFollowLists", QJsonArray{QJsonObject{
                {"userInfo", QJsonObject{{"userId", 1}}}, {"online", "yes"}}}}}},
        });
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
    }
};

QTEST_GUILESS_MAIN(FriendListeningTest)
#include "friend_listening_test.moc"
