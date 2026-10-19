// Typed user-social SDK contract: follows/followeds, level and subcount.
#include "sdk/user_social.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class UserSocialTest : public QObject {
    Q_OBJECT

    static QJsonObject userObject(qint64 id, const QString &name)
    {
        return QJsonObject{{"userId", id},
                           {"nickname", name},
                           {"avatarUrl", "http://p1.music.126.net/avatar.jpg"},
                           {"pendantData", QJsonObject{{"imageUrl", "http://p1.music.126.net/p.png"}}},
                           {"avatarDetail", QJsonObject{{"identityIconUrl", "http://p1.music.126.net/i.png"}}},
                           {"signature", "signature"},
                           {"gender", 1},
                           {"followed", true},
                           {"mutual", true},
                           {"vipType", 11},
                           {"vipRights", QJsonObject{{"associator", QJsonObject{{"rights", true}}},
                                                     {"redplus", QJsonObject{{"rights", true}}}}},
                           {"unknownField", "ignored"}};
    }

private slots:
    void parsesFollows()
    {
        const QJsonObject root{
            {"code", 200},
            {"more", true},
            {"follow", QJsonArray{userObject(42, QStringLiteral("Alice")),
                                  userObject(7, QStringLiteral("Bob"))}},
            {"followeds", QJsonArray{userObject(99, QStringLiteral("WrongKey"))}}};

        const auto parsed = parseUserFollows(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->more, true);
        QCOMPARE(parsed.value->items.size(), 2);

        const SocialUser &user = parsed.value->items.first();
        QCOMPARE(user.id, qint64(42));
        QCOMPARE(user.name, QStringLiteral("Alice"));
        QCOMPARE(user.avatar, QStringLiteral("http://p1.music.126.net/avatar.jpg"));
        QCOMPARE(user.pendant, QStringLiteral("http://p1.music.126.net/p.png"));
        QCOMPARE(user.identityIcon, QStringLiteral("http://p1.music.126.net/i.png"));
        QCOMPARE(user.signature, QStringLiteral("signature"));
        QCOMPARE(user.gender, 1);
        QCOMPARE(user.followed, true);
        QCOMPARE(user.mutual, true);
        QCOMPARE(user.vipType, 11);
        QCOMPARE(user.vipLevel, QStringLiteral("svip"));
        QCOMPARE(parsed.value->items.at(1).id, qint64(7));
    }

    void parsesFolloweds()
    {
        const QJsonObject root{
            {"code", 200},
            {"follow", QJsonArray{userObject(99, QStringLiteral("WrongKey"))}},
            {"followeds", QJsonArray{userObject(7, QStringLiteral("Follower"))}}};

        const auto parsed = parseUserFolloweds(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->more, false);
        QCOMPARE(parsed.value->items.size(), 1);
        QCOMPARE(parsed.value->items.first().id, qint64(7));
        QCOMPARE(parsed.value->items.first().name, QStringLiteral("Follower"));
    }

    void emptyAndMissingListsAreEmptyPages()
    {
        const auto missingFollows = parseUserFollows(QJsonObject{{"code", 200}});
        QVERIFY2(missingFollows.ok(), qPrintable(missingFollows.error.message));
        QVERIFY(missingFollows.value->items.isEmpty());
        QCOMPARE(missingFollows.value->more, false);

        const auto nullFollows =
            parseUserFollows(QJsonObject{{"code", 200}, {"follow", QJsonValue::Null}});
        QVERIFY2(nullFollows.ok(), qPrintable(nullFollows.error.message));
        QVERIFY(nullFollows.value->items.isEmpty());

        const auto emptyFolloweds = parseUserFolloweds(
            QJsonObject{{"code", 200}, {"followeds", QJsonArray{}}});
        QVERIFY2(emptyFolloweds.ok(), qPrintable(emptyFolloweds.error.message));
        QVERIFY(emptyFolloweds.value->items.isEmpty());
        QCOMPARE(emptyFolloweds.value->more, false);
    }

    void wrongListTypeIsParseError()
    {
        const auto follows =
            parseUserFollows(QJsonObject{{"code", 200}, {"follow", QStringLiteral("nope")}});
        QVERIFY(!follows.ok());
        QCOMPARE(follows.error.kind, ErrorKind::Parse);
        QCOMPARE(follows.error.endpoint, QString::fromLatin1("api/user/getfollows/"));
        QCOMPARE(follows.error.path, QStringLiteral("userFollows.follow"));
        QVERIFY(follows.error.message.contains(QStringLiteral("expected array")));

        const auto followeds = parseUserFolloweds(
            QJsonObject{{"code", 200}, {"followeds", QJsonObject{}}});
        QVERIFY(!followeds.ok());
        QCOMPARE(followeds.error.kind, ErrorKind::Parse);
        QCOMPARE(followeds.error.endpoint, QString::fromLatin1("api/user/getfolloweds/"));
        QCOMPARE(followeds.error.path, QStringLiteral("userFolloweds.followeds"));
    }

    void wrongMoreTypeIsParseError()
    {
        const auto parsed =
            parseUserFollows(QJsonObject{{"code", 200}, {"follow", QJsonArray{}},
                                         {"more", QStringLiteral("yes")}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("userFollows.more"));
    }

    void malformedUserRowIsParseError()
    {
        const auto parsed = parseUserFollows(
            QJsonObject{{"code", 200}, {"follow", QJsonArray{42}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("userFollows.follow[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void wrongUserFieldTypeIsParseError()
    {
        QJsonObject broken = userObject(42, QStringLiteral("Alice"));
        broken.insert(QStringLiteral("mutual"), QStringLiteral("yes"));

        const auto parsed = parseUserFollows(
            QJsonObject{{"code", 200}, {"follow", QJsonArray{broken}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("userFollows.follow[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("mutual")));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected boolean")));
    }

    void missingUserFieldsUseDefaults()
    {
        const auto parsed = parseUserFollows(QJsonObject{
            {"code", 200},
            {"follow", QJsonArray{QJsonObject{{"userId", 7}}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SocialUser &user = parsed.value->items.first();
        QCOMPARE(user.id, qint64(7));
        QVERIFY(user.name.isEmpty());
        QVERIFY(user.avatar.isEmpty());
        QVERIFY(user.signature.isEmpty());
        QCOMPARE(user.gender, 0);
        QCOMPARE(user.followed, false);
        QCOMPARE(user.mutual, false);
        QCOMPARE(user.vipType, 0);
    }

    void parsesLevel()
    {
        const QJsonObject root{
            {"code", 200},
            {"data", QJsonObject{{"level", 9},
                                 {"nowPlayCount", 10},
                                 {"nextPlayCount", 20},
                                 {"nowLoginCount", 3},
                                 {"nextLoginCount", 4},
                                 {"progress", 0.5},
                                 {"info", QStringLiteral("next level")}}}};

        const auto parsed = parseUserLevel(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const UserLevel &level = *parsed.value;
        QCOMPARE(level.level, 9);
        QCOMPARE(level.nowPlayCount, 10);
        QCOMPARE(level.nextPlayCount, 20);
        QCOMPARE(level.nowLoginCount, 3);
        QCOMPARE(level.nextLoginCount, 4);
        QCOMPARE(level.progress, 0.5);
        QCOMPARE(level.info, QStringLiteral("next level"));
    }

    void missingOrEmptyLevelDataIsParseError()
    {
        for (const QJsonObject &root :
             {QJsonObject{{"code", 200}},
              QJsonObject{{"code", 200}, {"data", QJsonObject{}}},
              QJsonObject{{"code", 200}, {"data", QJsonValue::Null}}}) {
            const auto parsed = parseUserLevel(root);
            QVERIFY(!parsed.ok());
            QCOMPARE(parsed.error.kind, ErrorKind::Parse);
            QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/user/level"));
            QCOMPARE(parsed.error.path, QStringLiteral("userLevel.data"));
        }
    }

    void wrongLevelDataTypeIsParseError()
    {
        const auto parsed = parseUserLevel(
            QJsonObject{{"code", 200}, {"data", QStringLiteral("nope")}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("userLevel.data"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void levelMissingFieldsUseDefaults()
    {
        const auto parsed = parseUserLevel(
            QJsonObject{{"code", 200}, {"data", QJsonObject{{"level", 3}}}});
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const UserLevel &level = *parsed.value;
        QCOMPARE(level.level, 3);
        QCOMPARE(level.nowPlayCount, 0);
        QCOMPARE(level.nextPlayCount, 0);
        QCOMPARE(level.nowLoginCount, 0);
        QCOMPARE(level.nextLoginCount, 0);
        QCOMPARE(level.progress, 0.0);
        QVERIFY(level.info.isEmpty());
    }

    void wrongLevelFieldTypeIsParseError()
    {
        const auto parsed = parseUserLevel(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"nowPlayCount", QStringLiteral("many")}}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("userLevel.data.nowPlayCount"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected number")));
    }

    void parsesSubcount()
    {
        const QJsonObject root{{"code", 200},
                               {"artistCount", 1},
                               {"createdPlaylistCount", 2},
                               {"subPlaylistCount", 3},
                               {"mvCount", 4},
                               {"djRadioCount", 5},
                               {"ignored", true}};

        const auto parsed = parseUserSubcount(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->artistCount, 1);
        QCOMPARE(parsed.value->createdPlaylistCount, 2);
        QCOMPARE(parsed.value->subPlaylistCount, 3);
        QCOMPARE(parsed.value->mvCount, 4);
        QCOMPARE(parsed.value->djRadioCount, 5);
    }

    void subcountMissingAndNullFieldsUseDefaults()
    {
        const auto empty = parseUserSubcount(QJsonObject{{"code", 200}});
        QVERIFY2(empty.ok(), qPrintable(empty.error.message));
        QCOMPARE(empty.value->artistCount, 0);
        QCOMPARE(empty.value->createdPlaylistCount, 0);
        QCOMPARE(empty.value->subPlaylistCount, 0);
        QCOMPARE(empty.value->mvCount, 0);
        QCOMPARE(empty.value->djRadioCount, 0);

        const auto nullFields = parseUserSubcount(
            QJsonObject{{"code", 200}, {"artistCount", QJsonValue::Null}});
        QVERIFY2(nullFields.ok(), qPrintable(nullFields.error.message));
        QCOMPARE(nullFields.value->artistCount, 0);
    }

    void wrongSubcountFieldTypeIsParseError()
    {
        const auto parsed = parseUserSubcount(
            QJsonObject{{"code", 200}, {"djRadioCount", true}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QString::fromLatin1("api/subcount"));
        QCOMPARE(parsed.error.path, QStringLiteral("userSubcount.djRadioCount"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected number")));
    }
};

QTEST_GUILESS_MAIN(UserSocialTest)
#include "user_social_test.moc"
