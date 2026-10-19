// User membership level from the real vipRights/vipType shapes in
// docs/reference/official/netease-cloudmusic/assets/comment-track-demote-data-*.
#include "sdk/user_vip.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class UserVipTest : public QObject {
    Q_OBJECT

private slots:
    // redplus.rights 为 true 是黑胶 SVIP，即使 associator 也在
    void redplusRightsIsSvip()
    {
        const QJsonObject user{
            {"vipType", 11},
            {"vipRights", QJsonObject{
                 {"associator", QJsonObject{{"vipCode", 100}, {"rights", true}}},
                 {"redplus", QJsonObject{{"vipCode", 300}, {"rights", true}}}}},
        };
        QCOMPARE(vipLevel(user), QStringLiteral("svip"));
    }

    // 只有 associator（黑胶 VIP）：redplus 为 null 的旧响应
    void associatorRightsIsVip()
    {
        const QJsonObject user{
            {"vipType", 0},
            {"vipRights", QJsonObject{
                 {"associator", QJsonObject{{"vipCode", 100}, {"rights", true}}},
                 {"redplus", QJsonValue::Null}}},
        };
        QCOMPARE(vipLevel(user), QStringLiteral("vip"));
    }

    // vipRights 缺失或没给对应权益时退回 vipType > 0
    void vipTypeAloneIsVip()
    {
        QCOMPARE(vipLevel(QJsonObject{{"vipType", 11}}), QStringLiteral("vip"));
    }

    void noMembershipIsEmpty()
    {
        const QJsonObject user{
            {"vipType", 0},
            {"vipRights", QJsonObject{
                 {"associator", QJsonObject{{"vipCode", 100}, {"rights", false}}},
                 {"musicPackage", QJsonObject{{"vipCode", 220}, {"rights", true}}},
                 {"redplus", QJsonValue::Null}}},
        };
        QCOMPARE(vipLevel(user), QString());
    }

    void missingVipRightsIsEmpty()
    {
        QCOMPARE(vipLevel(QJsonObject{{"userId", 7}, {"nickname", "u"}}), QString());
    }

    // redplus 存在但 rights 为 false 不算 SVIP，仍可按 associator 算 VIP
    void redplusWithoutRightsFallsBackToAssociator()
    {
        const QJsonObject user{
            {"vipType", 0},
            {"vipRights", QJsonObject{
                 {"associator", QJsonObject{{"vipCode", 100}, {"rights", true}}},
                 {"redplus", QJsonObject{{"vipCode", 300}, {"rights", false}}}}},
        };
        QCOMPARE(vipLevel(user), QStringLiteral("vip"));
    }
};

QTEST_GUILESS_MAIN(UserVipTest)
#include "user_vip_test.moc"
