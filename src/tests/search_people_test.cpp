// Typed Cloudsearch people/radio SDK contract.
#include "sdk/search_people.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class SearchPeopleTest : public QObject {
    Q_OBJECT

    static QJsonObject userObject()
    {
        return QJsonObject{
            {"userId", 42},
            {"nickname", "用户甲"},
            {"avatarUrl", "http://p1.music.126.net/avatar.jpg"},
            {"followed", true},
            {"followeds", 7},
            {"follows", 3},
            {"playlistCount", 5},
            {"signature", "签名"},
            {"vipType", 0},
        };
    }

    static QJsonObject radioObject()
    {
        return QJsonObject{
            {"id", 7},
            {"name", "电台甲"},
            {"picUrl", "http://p1.music.126.net/radio.jpg"},
            {"desc", "电台简介"},
            {"dj", QJsonObject{{"nickname", "主播甲"}, {"userId", 99}}},
            {"programCount", 12},
            {"subCount", 34},
            {"playCount", 56},
            {"categoryId", 3},
        };
    }

    static QJsonObject usersRoot(const QJsonObject &result)
    {
        return QJsonObject{{"code", 200}, {"result", result}};
    }

    static QJsonObject radiosRoot(const QJsonObject &result)
    {
        return QJsonObject{{"code", 200}, {"result", result}};
    }

private slots:
    void parsesUsers()
    {
        const QJsonObject result{
            {"userprofiles", QJsonArray{userObject(),
                                        QJsonObject{{"userId", 7},
                                                    {"nickname", QStringLiteral("用户乙")}}}}};
        const auto parsed = parseSearchUsers(usersRoot(result), QStringLiteral("用户"));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->query, QStringLiteral("用户"));
        QCOMPARE(parsed.value->items.size(), 2);

        const SearchUser &user = parsed.value->items.first();
        QCOMPARE(user.id, qint64(42));
        QCOMPARE(user.name, QStringLiteral("用户甲"));
        QCOMPARE(user.cover, QStringLiteral("http://p1.music.126.net/avatar.jpg"));
        QCOMPARE(user.followed, true);
        QCOMPARE(user.fans, qint64(7));
        QCOMPARE(user.follows, qint64(3));
        QCOMPARE(user.playlists, qint64(5));
        QCOMPARE(user.signature, QStringLiteral("签名"));
        // result order is preserved verbatim.
        QCOMPARE(parsed.value->items.at(1).id, qint64(7));
        QCOMPARE(parsed.value->items.at(1).name, QStringLiteral("用户乙"));
    }

    void userMissingFieldsUseDefaults()
    {
        const QJsonObject result{{"userprofiles", QJsonArray{QJsonObject{{"userId", 42}}}}};
        const auto parsed = parseSearchUsers(usersRoot(result));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SearchUser &user = parsed.value->items.first();
        QCOMPARE(user.id, qint64(42));
        QCOMPARE(user.name, QString());
        QCOMPARE(user.cover, QString());
        QCOMPARE(user.followed, false);
        QCOMPARE(user.fans, qint64(0));
        QCOMPARE(user.follows, qint64(0));
        QCOMPARE(user.playlists, qint64(0));
        QCOMPARE(user.signature, QString());
    }

    void nullUserFieldsUseDefaults()
    {
        const QJsonObject item{{"userId", 42},
                               {"nickname", QJsonValue::Null},
                               {"avatarUrl", QJsonValue::Null},
                               {"followed", QJsonValue::Null},
                               {"signature", QJsonValue::Null}};
        const QJsonObject result{{"userprofiles", QJsonArray{item}}};
        const auto parsed = parseSearchUsers(usersRoot(result));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SearchUser &user = parsed.value->items.first();
        QCOMPARE(user.name, QString());
        QCOMPARE(user.cover, QString());
        QCOMPARE(user.followed, false);
        QCOMPARE(user.signature, QString());
    }

    void emptyUsersResultIsSuccess()
    {
        // No matches: the endpoint may omit `result` entirely or return {}.
        for (const QJsonObject &root : {usersRoot(QJsonObject{}),
                                        QJsonObject{{"code", 200}},
                                        QJsonObject{{"code", 200},
                                                    {"result", QJsonValue::Null}}}) {
            const auto parsed = parseSearchUsers(root);
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->items.isEmpty());
        }
    }

    void absentOrNullUserArrayIsEmpty()
    {
        for (const QJsonObject &result : {QJsonObject{},
                                          QJsonObject{{"userprofiles", QJsonArray{}}},
                                          QJsonObject{{"userprofiles", QJsonValue::Null}}}) {
            const auto parsed = parseSearchUsers(usersRoot(result));
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->items.isEmpty());
        }
    }

    void malformedUserResultIsParseError()
    {
        const auto parsed = parseSearchUsers(
            QJsonObject{{"code", 200}, {"result", QJsonArray{}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.endpoint, QStringLiteral("api/cloudsearch/pc"));
        QCOMPARE(parsed.error.path, QStringLiteral("searchUsers.result"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void malformedUserArrayIsParseError()
    {
        const QJsonObject result{{"userprofiles", QJsonObject{}}};
        const auto parsed = parseSearchUsers(usersRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchUsers.result.userprofiles"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }

    void nonObjectUserItemIsParseError()
    {
        const QJsonObject result{{"userprofiles", QJsonArray{42}}};
        const auto parsed = parseSearchUsers(usersRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("searchUsers.result.userprofiles[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void malformedUserFieldIsParseError()
    {
        QJsonObject item = userObject();
        item.insert(QStringLiteral("userId"), QStringLiteral("42"));
        const QJsonObject result{{"userprofiles", QJsonArray{item}}};
        const auto parsed = parseSearchUsers(usersRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path,
                 QStringLiteral("searchUsers.result.userprofiles[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("userId: expected number")));

        item = userObject();
        item.insert(QStringLiteral("followed"), QStringLiteral("yes"));
        const auto badBool = parseSearchUsers(
            usersRoot(QJsonObject{{"userprofiles", QJsonArray{item}}}));
        QVERIFY(!badBool.ok());
        QCOMPARE(badBool.error.kind, ErrorKind::Parse);
        QVERIFY(badBool.error.message.contains(QStringLiteral("followed: expected boolean")));
    }

    void parsesRadios()
    {
        const QJsonObject result{{"djRadios", QJsonArray{radioObject()}}};
        const auto parsed = parseSearchRadios(radiosRoot(result), QStringLiteral("电台"));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->query, QStringLiteral("电台"));
        QCOMPARE(parsed.value->items.size(), 1);

        const SearchRadio &radio = parsed.value->items.first();
        QCOMPARE(radio.id, qint64(7));
        QCOMPARE(radio.name, QStringLiteral("电台甲"));
        QCOMPARE(radio.cover, QStringLiteral("http://p1.music.126.net/radio.jpg"));
        QCOMPARE(radio.description, QStringLiteral("电台简介"));
        QCOMPARE(radio.host, QStringLiteral("主播甲"));
        QCOMPARE(radio.programCount, qint64(12));
        QCOMPARE(radio.subCount, qint64(34));
        QCOMPARE(radio.playCount, qint64(56));
    }

    void radioMissingFieldsUseDefaults()
    {
        const QJsonObject result{{"djRadios", QJsonArray{QJsonObject{{"id", 7}}}}};
        const auto parsed = parseSearchRadios(radiosRoot(result));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SearchRadio &radio = parsed.value->items.first();
        QCOMPARE(radio.id, qint64(7));
        QCOMPARE(radio.name, QString());
        QCOMPARE(radio.cover, QString());
        QCOMPARE(radio.description, QString());
        QCOMPARE(radio.host, QString());
        QCOMPARE(radio.programCount, qint64(0));
        QCOMPARE(radio.subCount, qint64(0));
        QCOMPARE(radio.playCount, qint64(0));
    }

    void nullRadioFieldsUseDefaults()
    {
        const QJsonObject item{{"id", 7},
                               {"name", QJsonValue::Null},
                               {"picUrl", QJsonValue::Null},
                               {"desc", QJsonValue::Null},
                               {"dj", QJsonValue::Null},
                               {"playCount", QJsonValue::Null}};
        const QJsonObject result{{"djRadios", QJsonArray{item}}};
        const auto parsed = parseSearchRadios(radiosRoot(result));
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const SearchRadio &radio = parsed.value->items.first();
        QCOMPARE(radio.name, QString());
        QCOMPARE(radio.cover, QString());
        QCOMPARE(radio.description, QString());
        QCOMPARE(radio.host, QString());
        QCOMPARE(radio.playCount, qint64(0));
    }

    void emptyRadiosResultIsSuccess()
    {
        for (const QJsonObject &root : {radiosRoot(QJsonObject{}),
                                        QJsonObject{{"code", 200}},
                                        QJsonObject{{"code", 200},
                                                    {"result", QJsonValue::Null}}}) {
            const auto parsed = parseSearchRadios(root);
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->items.isEmpty());
        }
    }

    void absentOrNullRadioArrayIsEmpty()
    {
        for (const QJsonObject &result : {QJsonObject{},
                                          QJsonObject{{"djRadios", QJsonArray{}}},
                                          QJsonObject{{"djRadios", QJsonValue::Null}}}) {
            const auto parsed = parseSearchRadios(radiosRoot(result));
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            QVERIFY(parsed.value->items.isEmpty());
        }
    }

    void malformedRadioResultIsParseError()
    {
        const auto parsed = parseSearchRadios(
            QJsonObject{{"code", 200}, {"result", false}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchRadios.result"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void malformedRadioArrayIsParseError()
    {
        const QJsonObject result{{"djRadios", 7}};
        const auto parsed = parseSearchRadios(radiosRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchRadios.result.djRadios"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected array")));
    }

    void nonObjectRadioItemIsParseError()
    {
        const QJsonObject result{{"djRadios", QJsonArray{QJsonValue::Null}}};
        const auto parsed = parseSearchRadios(radiosRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchRadios.result.djRadios[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));
    }

    void malformedRadioFieldIsParseError()
    {
        QJsonObject item = radioObject();
        item.insert(QStringLiteral("programCount"), QStringLiteral("12"));
        const QJsonObject result{{"djRadios", QJsonArray{item}}};
        const auto parsed = parseSearchRadios(radiosRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchRadios.result.djRadios[0]"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("programCount: expected number")));
    }

    void malformedDjIsParseError()
    {
        const QJsonObject item{{"id", 7}, {"dj", QStringLiteral("主播")}};
        const QJsonObject result{{"djRadios", QJsonArray{item}}};
        const auto parsed = parseSearchRadios(radiosRoot(result));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QCOMPARE(parsed.error.path, QStringLiteral("searchRadios.result.djRadios[0].dj"));
        QVERIFY(parsed.error.message.contains(QStringLiteral("expected object")));

        const QJsonObject badNickname{{"id", 7},
                                      {"dj", QJsonObject{{"nickname", 9}}}};
        const auto nickname = parseSearchRadios(
            radiosRoot(QJsonObject{{"djRadios", QJsonArray{badNickname}}}));
        QVERIFY(!nickname.ok());
        QCOMPARE(nickname.error.path,
                 QStringLiteral("searchRadios.result.djRadios[0].dj"));
        QVERIFY(nickname.error.message.contains(QStringLiteral("nickname: expected string")));
    }
};

QTEST_GUILESS_MAIN(SearchPeopleTest)
#include "search_people_test.moc"
