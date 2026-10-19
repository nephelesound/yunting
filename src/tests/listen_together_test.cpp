// Typed listen-together SDK: room/status/check envelopes, sync command and
// playlist shapes, and action results. All payloads are synthetic.
#include "sdk/listen_together.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

namespace {

const QString CreateEndpoint = QStringLiteral("api/listen/together/room/create");
const QString StatusEndpoint = QStringLiteral("api/listen/together/status/get");
const QString CheckEndpoint = QStringLiteral("api/listen/together/room/check");
const QString SyncEndpoint = QStringLiteral("api/listen/together/sync/playlist/get");
const QString ResultEndpoint = QStringLiteral("api/listen/together");

QJsonObject envelope(const QJsonObject &data)
{
    return QJsonObject{{QStringLiteral("code"), 200}, {QStringLiteral("data"), data}};
}

QJsonObject userJson(qint64 id, const QString &nickname)
{
    return QJsonObject{
        {QStringLiteral("userId"), id},
        {QStringLiteral("nickname"), nickname},
        {QStringLiteral("avatarUrl"), QStringLiteral("https://cdn.example/%1.jpg").arg(id)},
        {QStringLiteral("identityName"), QStringLiteral("member")},
        {QStringLiteral("vipType"), 0},
        {QStringLiteral("vipRights"),
         QJsonObject{{QStringLiteral("associator"), QJsonObject{{QStringLiteral("rights"), true}}}}},
    };
}

QJsonObject roomInfo()
{
    return QJsonObject{
        {QStringLiteral("roomId"), QStringLiteral("room-42")},
        {QStringLiteral("creatorId"), 7},
        {QStringLiteral("roomUsers"),
         QJsonArray{userJson(7, QStringLiteral("Alice")),
                    userJson(42, QStringLiteral("Bob"))}},
    };
}

} // namespace

class ListenTogetherTest : public QObject {
    Q_OBJECT

private slots:
    void parsesRoomCreate()
    {
        const auto result = parseRoomCreate(envelope(
            QJsonObject{{QStringLiteral("roomInfo"), roomInfo()}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->roomId, QStringLiteral("room-42"));
        QCOMPARE(result.value->creatorId, qint64(7));
        QVERIFY(result.value->valid());
        QCOMPARE(result.value->users.size(), 2);
        QCOMPARE(result.value->users.at(0).userId, qint64(7));
        QCOMPARE(result.value->users.at(0).nickname, QStringLiteral("Alice"));
        QCOMPARE(result.value->users.at(0).avatar,
                 QStringLiteral("https://cdn.example/7.jpg"));
        QCOMPARE(result.value->users.at(0).identity, QStringLiteral("member"));
        QCOMPARE(result.value->users.at(0).vipLevel, QStringLiteral("vip"));
        QCOMPARE(result.value->users.at(1).userId, qint64(42));
    }

    void roomCreateToleratesMissingUsersAndCreator()
    {
        const auto result = parseRoomCreate(envelope(QJsonObject{
            {QStringLiteral("roomInfo"),
             QJsonObject{{QStringLiteral("roomId"), QStringLiteral("room-7")}}}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->creatorId, qint64(0));
        QVERIFY(result.value->users.isEmpty());
        QVERIFY(result.value->valid());
    }

    void roomCreateRequiresDataAndRoomInfo()
    {
        const auto noData = parseRoomCreate(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!noData.ok());
        QCOMPARE(noData.error.kind, ErrorKind::Parse);
        QCOMPARE(noData.error.endpoint, CreateEndpoint);
        QCOMPARE(noData.error.path, QStringLiteral("listenTogether.create.data"));

        const auto nullData = parseRoomCreate(
            QJsonObject{{QStringLiteral("code"), 200},
                        {QStringLiteral("data"), QJsonValue::Null}});
        QVERIFY(!nullData.ok());
        QCOMPARE(nullData.error.path, QStringLiteral("listenTogether.create.data"));

        const auto wrongData = parseRoomCreate(
            QJsonObject{{QStringLiteral("code"), 200},
                        {QStringLiteral("data"), QStringLiteral("bad")}});
        QVERIFY(!wrongData.ok());
        QCOMPARE(wrongData.error.path, QStringLiteral("listenTogether.create.data"));

        const auto noRoom = parseRoomCreate(envelope(QJsonObject{}));
        QVERIFY(!noRoom.ok());
        QCOMPARE(noRoom.error.kind, ErrorKind::Parse);
        QCOMPARE(noRoom.error.endpoint, CreateEndpoint);
        QCOMPARE(noRoom.error.path,
                 QStringLiteral("listenTogether.create.data.roomInfo"));
    }

    void roomCreateRequiresNonEmptyRoomId()
    {
        const auto empty = parseRoomCreate(envelope(QJsonObject{
            {QStringLiteral("roomInfo"),
             QJsonObject{{QStringLiteral("roomId"), QString()}}}}));
        QVERIFY(!empty.ok());
        QCOMPARE(empty.error.kind, ErrorKind::Parse);
        QCOMPARE(empty.error.endpoint, CreateEndpoint);
        QCOMPARE(empty.error.path,
                 QStringLiteral("listenTogether.create.data.roomInfo.roomId"));

        const auto wrongType = parseRoomCreate(envelope(QJsonObject{
            {QStringLiteral("roomInfo"),
             QJsonObject{{QStringLiteral("roomId"), 42}}}}));
        QVERIFY(!wrongType.ok());
        QCOMPARE(wrongType.error.kind, ErrorKind::Parse);
        QCOMPARE(wrongType.error.path,
                 QStringLiteral("listenTogether.create.data.roomInfo.roomId"));
    }

    void roomCreateRejectsWrongFieldTypes()
    {
        QJsonObject usersRoom{{QStringLiteral("roomId"), QStringLiteral("room-42")}};
        usersRoom.insert(QStringLiteral("roomUsers"), QStringLiteral("bad"));
        const auto users = parseRoomCreate(
            envelope(QJsonObject{{QStringLiteral("roomInfo"), usersRoom}}));
        QVERIFY(!users.ok());
        QCOMPARE(users.error.endpoint, CreateEndpoint);
        QCOMPARE(users.error.path,
                 QStringLiteral("listenTogether.create.data.roomInfo.roomUsers"));

        QJsonObject rowRoom{{QStringLiteral("roomId"), QStringLiteral("room-42")}};
        rowRoom.insert(QStringLiteral("roomUsers"),
                       QJsonArray{QJsonObject{}, QStringLiteral("bad")});
        const auto row = parseRoomCreate(
            envelope(QJsonObject{{QStringLiteral("roomInfo"), rowRoom}}));
        QVERIFY(!row.ok());
        QCOMPARE(row.error.kind, ErrorKind::Parse);
        QCOMPARE(row.error.path,
                 QStringLiteral("listenTogether.create.data.roomInfo.roomUsers[1]"));

        QJsonObject creatorRoom{{QStringLiteral("roomId"), QStringLiteral("room-42")}};
        creatorRoom.insert(QStringLiteral("creatorId"), QStringLiteral("7"));
        const auto creator = parseRoomCreate(
            envelope(QJsonObject{{QStringLiteral("roomInfo"), creatorRoom}}));
        QVERIFY(!creator.ok());
        QCOMPARE(creator.error.path,
                 QStringLiteral("listenTogether.create.data.roomInfo.creatorId"));
    }

    void parsesRoomStatus()
    {
        const auto result = parseRoomStatus(envelope(QJsonObject{
            {QStringLiteral("inRoom"), true},
            {QStringLiteral("roomInfo"), roomInfo()},
            {QStringLiteral("status"), QStringLiteral("PLAYING")}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->inRoom);
        QCOMPARE(result.value->status, QStringLiteral("PLAYING"));
        QCOMPARE(result.value->room.roomId, QStringLiteral("room-42"));
        QCOMPARE(result.value->room.users.size(), 2);
    }

    void statusNotInRoomIsSuccessful()
    {
        const auto result = parseRoomStatus(envelope(QJsonObject{
            {QStringLiteral("inRoom"), false},
            {QStringLiteral("roomInfo"), QJsonValue::Null}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(!result.value->inRoom);
        QVERIFY(result.value->status.isEmpty());
        QVERIFY(!result.value->room.valid());
    }

    void statusRejectsMissingOrWrongData()
    {
        const auto missing = parseRoomStatus(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!missing.ok());
        QCOMPARE(missing.error.endpoint, StatusEndpoint);
        QCOMPARE(missing.error.path, QStringLiteral("listenTogether.status.data"));

        const auto wrongInRoom = parseRoomStatus(envelope(QJsonObject{
            {QStringLiteral("inRoom"), QStringLiteral("yes")}}));
        QVERIFY(!wrongInRoom.ok());
        QCOMPARE(wrongInRoom.error.path,
                 QStringLiteral("listenTogether.status.data.inRoom"));

        const auto wrongRoom = parseRoomStatus(envelope(QJsonObject{
            {QStringLiteral("inRoom"), true},
            {QStringLiteral("roomInfo"), QStringLiteral("bad")}}));
        QVERIFY(!wrongRoom.ok());
        QCOMPARE(wrongRoom.error.path,
                 QStringLiteral("listenTogether.status.data.roomInfo"));
    }

    void parsesRoomCheck()
    {
        const auto result = parseRoomCheck(envelope(QJsonObject{
            {QStringLiteral("joinable"), true},
            {QStringLiteral("type"), QStringLiteral("song")},
            {QStringLiteral("status"), QStringLiteral("PLAYING")},
            {QStringLiteral("copywriting"), QStringLiteral("join now")}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->joinable);
        QCOMPARE(result.value->type, QStringLiteral("song"));
        QCOMPARE(result.value->status, QStringLiteral("PLAYING"));
        QCOMPARE(result.value->copywriting, QStringLiteral("join now"));
    }

    void roomCheckAcceptsNullCopywriting()
    {
        const auto result = parseRoomCheck(envelope(QJsonObject{
            {QStringLiteral("joinable"), false},
            {QStringLiteral("copywriting"), QJsonValue::Null}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(!result.value->joinable);
        QVERIFY(result.value->copywriting.isEmpty());
    }

    void roomCheckRejectsMalformedFields()
    {
        const auto missing = parseRoomCheck(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!missing.ok());
        QCOMPARE(missing.error.kind, ErrorKind::Parse);
        QCOMPARE(missing.error.endpoint, CheckEndpoint);
        QCOMPARE(missing.error.path, QStringLiteral("listenTogether.check.data"));

        const auto wrong = parseRoomCheck(envelope(QJsonObject{
            {QStringLiteral("joinable"), QStringLiteral("true")}}));
        QVERIFY(!wrong.ok());
        QCOMPARE(wrong.error.endpoint, CheckEndpoint);
        QCOMPARE(wrong.error.path,
                 QStringLiteral("listenTogether.check.data.joinable"));
    }

    void parsesEmptySyncPlaylist()
    {
        const auto result = parseSyncPlaylist(envelope(QJsonObject{}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(!result.value->command.has_value());
        QVERIFY(result.value->playlist.displayList.isEmpty());
        QVERIFY(result.value->playlist.randomList.isEmpty());
        QVERIFY(!result.value->playlist.replaced);
    }

    void syncPlaylistAcceptsNullCommandAndPlaylist()
    {
        const auto result = parseSyncPlaylist(envelope(QJsonObject{
            {QStringLiteral("playCommand"), QJsonValue::Null},
            {QStringLiteral("playlist"), QJsonValue::Null}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(!result.value->command.has_value());
        QVERIFY(result.value->playlist.displayList.isEmpty());
    }

    void parsesSyncPlaylistActivity()
    {
        const QJsonObject command{
            {QStringLiteral("userId"), 7},
            {QStringLiteral("commandType"), QStringLiteral("PLAY")},
            {QStringLiteral("progress"), 1500},
            {QStringLiteral("playStatus"), QStringLiteral("PLAYING")},
            {QStringLiteral("formerSongId"), QStringLiteral("42")},
            {QStringLiteral("targetSongId"), 7},
            {QStringLiteral("clientSeq"), 3},
            {QStringLiteral("serverSeq"), 4},
        };
        const QJsonObject displayList{
            {QStringLiteral("result"),
             QJsonArray{QStringLiteral("42"), 7, QStringLiteral("0")}}};
        const QJsonObject playlist{
            {QStringLiteral("displayList"), displayList},
            {QStringLiteral("randomList"),
             QJsonArray{QStringLiteral("7"), 42}},
            {QStringLiteral("replaced"), true},
        };
        const auto result = parseSyncPlaylist(envelope(QJsonObject{
            {QStringLiteral("playCommand"), command},
            {QStringLiteral("playlist"), playlist}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));

        QVERIFY(result.value->command.has_value());
        const ListenTogetherCommand &parsed = *result.value->command;
        QCOMPARE(parsed.userId, qint64(7));
        QCOMPARE(parsed.commandType, QStringLiteral("PLAY"));
        QCOMPARE(parsed.progress, qint64(1500));
        QCOMPARE(parsed.playStatus, QStringLiteral("PLAYING"));
        QCOMPARE(parsed.formerSongId, qint64(42));
        QCOMPARE(parsed.targetSongId, qint64(7));
        QCOMPARE(parsed.clientSeq, qint64(3));
        QCOMPARE(parsed.serverSeq, qint64(4));

        QCOMPARE(result.value->playlist.displayList,
                 (QList<qint64>{42, 7, 0}));
        QCOMPARE(result.value->playlist.randomList,
                 (QList<qint64>{7, 42}));
        QVERIFY(result.value->playlist.replaced);
    }

    // 官方客户端的 playlist 形状：replace + 两个 {changed,result} 对象 + 版本/模式。
    void parsesOfficialSyncPlaylistShape()
    {
        const QJsonObject playlist{
            {QStringLiteral("replace"), true},
            {QStringLiteral("displayList"),
             QJsonObject{{QStringLiteral("changed"), true},
                         {QStringLiteral("result"), QJsonArray{101, QStringLiteral("102")}}}},
            {QStringLiteral("randomList"),
             QJsonObject{{QStringLiteral("changed"), false},
                         {QStringLiteral("result"), QJsonArray{QStringLiteral("102"), 101}}}},
            {QStringLiteral("version"),
             QJsonArray{QJsonObject{{QStringLiteral("userId"), 7}, {QStringLiteral("version"), 5}},
                        QJsonObject{{QStringLiteral("userId"), 42}, {QStringLiteral("version"), 9}}}},
            {QStringLiteral("playMode"), QStringLiteral("SINGLE_LOOP")},
        };
        const auto result = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), playlist}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->playlist.replaced);
        QVERIFY(result.value->playlist.displayChanged);
        QVERIFY(!result.value->playlist.randomChanged);
        QCOMPARE(result.value->playlist.displayList, (QList<qint64>{101, 102}));
        QCOMPARE(result.value->playlist.randomList, (QList<qint64>{102, 101}));
        QCOMPARE(result.value->playlist.playMode, QStringLiteral("SINGLE_LOOP"));
        QCOMPARE(result.value->playlist.versions.size(), 2);
        QCOMPARE(result.value->playlist.versions.at(0).userId, qint64(7));
        QCOMPARE(result.value->playlist.versions.at(0).version, qint64(5));
        QCOMPARE(result.value->playlist.versions.at(1).userId, qint64(42));
        QCOMPARE(result.value->playlist.versions.at(1).version, qint64(9));
    }

    void syncCommandNormalizesAbsentSongIds()
    {
        const QJsonObject command{
            {QStringLiteral("userId"), 7},
            {QStringLiteral("formerSongId"), QJsonValue::Null}};
        const auto result = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playCommand"), command}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->command.has_value());
        QCOMPARE(result.value->command->formerSongId, qint64(0));
        QCOMPARE(result.value->command->targetSongId, qint64(0));
    }

    void syncPlaylistRejectsNonNumericCommandSongId()
    {
        const QJsonObject former{
            {QStringLiteral("userId"), 7},
            {QStringLiteral("formerSongId"), QStringLiteral("abc")}};
        const auto result = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playCommand"), former}}));
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, SyncEndpoint);
        QCOMPARE(result.error.path,
                 QStringLiteral("listenTogether.sync.data.playCommand.formerSongId"));

        const QJsonObject target{
            {QStringLiteral("targetSongId"), QStringLiteral("not-a-number")}};
        const auto targetResult = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playCommand"), target}}));
        QVERIFY(!targetResult.ok());
        QCOMPARE(targetResult.error.path,
                 QStringLiteral("listenTogether.sync.data.playCommand.targetSongId"));
    }

    void syncPlaylistRejectsWrongCommandShape()
    {
        const auto command = parseSyncPlaylist(envelope(QJsonObject{
            {QStringLiteral("playCommand"), QStringLiteral("bad")}}));
        QVERIFY(!command.ok());
        QCOMPARE(command.error.kind, ErrorKind::Parse);
        QCOMPARE(command.error.endpoint, SyncEndpoint);
        QCOMPARE(command.error.path,
                 QStringLiteral("listenTogether.sync.data.playCommand"));

        const QJsonObject progressCommand{
            {QStringLiteral("progress"), QStringLiteral("1500")}};
        const auto progress = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playCommand"), progressCommand}}));
        QVERIFY(!progress.ok());
        QCOMPARE(progress.error.path,
                 QStringLiteral("listenTogether.sync.data.playCommand.progress"));
    }

    void syncPlaylistRejectsMalformedPlaylist()
    {
        const auto playlist = parseSyncPlaylist(envelope(QJsonObject{
            {QStringLiteral("playlist"), QStringLiteral("bad")}}));
        QVERIFY(!playlist.ok());
        QCOMPARE(playlist.error.endpoint, SyncEndpoint);
        QCOMPARE(playlist.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist"));

        const QJsonObject badDisplay{
            {QStringLiteral("displayList"), QStringLiteral("bad")}};
        const auto display = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), badDisplay}}));
        QVERIFY(!display.ok());
        QCOMPARE(display.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist.displayList"));

        const QJsonObject badDisplayValue{
            {QStringLiteral("result"), QStringLiteral("bad")}};
        const QJsonObject badDisplayResult{
            {QStringLiteral("displayList"), badDisplayValue}};
        const auto displayResult = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), badDisplayResult}}));
        QVERIFY(!displayResult.ok());
        QCOMPARE(displayResult.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist.displayList.result"));

        const QJsonObject badRandom{
            {QStringLiteral("randomList"),
             QJsonObject{{QStringLiteral("result"), QStringLiteral("bad")}}}};
        const auto random = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), badRandom}}));
        QVERIFY(!random.ok());
        QCOMPARE(random.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist.randomList.result"));

        const QJsonObject badReplaced{
            {QStringLiteral("replaced"), QStringLiteral("yes")}};
        const auto replaced = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), badReplaced}}));
        QVERIFY(!replaced.ok());
        QCOMPARE(replaced.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist.replaced"));
    }

    void syncPlaylistRejectsBadSongIdElements()
    {
        const QJsonObject displayResult{
            {QStringLiteral("result"), QJsonArray{true}}};
        const QJsonObject badDisplay{
            {QStringLiteral("displayList"), displayResult}};
        const auto display = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), badDisplay}}));
        QVERIFY(!display.ok());
        QCOMPARE(display.error.kind, ErrorKind::Parse);
        QCOMPARE(display.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist.displayList.result[0]"));

        const QJsonObject badRandom{
            {QStringLiteral("randomList"),
             QJsonArray{QStringLiteral("42"), QStringLiteral("x")}}};
        const auto random = parseSyncPlaylist(
            envelope(QJsonObject{{QStringLiteral("playlist"), badRandom}}));
        QVERIFY(!random.ok());
        QCOMPARE(random.error.path,
                 QStringLiteral("listenTogether.sync.data.playlist.randomList[1]"));
    }

    void syncPlaylistRequiresData()
    {
        const auto result = parseSyncPlaylist(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, SyncEndpoint);
        QCOMPARE(result.error.path, QStringLiteral("listenTogether.sync.data"));
    }

    void parsesPrivileges()
    {
        const auto result = parsePrivileges(envelope(QJsonObject{
            {QStringLiteral("result"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("songId"), QStringLiteral("101")},
                     {QStringLiteral("playable"), false},
                     {QStringLiteral("unplayableType"), QStringLiteral("vip")},
                     {QStringLiteral("unplayableUserIds"), QJsonArray{7, QStringLiteral("42")}},
                     {QStringLiteral("unplayableOuterIds"), QJsonArray{QStringLiteral("9")}},
                 },
                 QJsonObject{
                     {QStringLiteral("songId"), 102},
                     {QStringLiteral("playable"), true},
                 },
             }}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 2);
        QCOMPARE(result.value->at(0).songId, qint64(101));
        QVERIFY(!result.value->at(0).playable);
        QCOMPARE(result.value->at(0).unplayableType, QStringLiteral("vip"));
        QCOMPARE(result.value->at(0).unplayableUserIds, (QList<qint64>{7, 42}));
        QCOMPARE(result.value->at(0).unplayableOuterIds, (QList<qint64>{9}));
        QCOMPARE(result.value->at(1).songId, qint64(102));
        QVERIFY(result.value->at(1).playable);
        QVERIFY(result.value->at(1).unplayableUserIds.isEmpty());
    }

    void privilegesAcceptMissingOrEmptyResult()
    {
        const auto missing = parsePrivileges(envelope(QJsonObject{}));
        QVERIFY2(missing.ok(), qPrintable(missing.error.message));
        QVERIFY(missing.value->isEmpty());

        const auto nullResult = parsePrivileges(envelope(QJsonObject{
            {QStringLiteral("result"), QJsonValue::Null}}));
        QVERIFY2(nullResult.ok(), qPrintable(nullResult.error.message));
        QVERIFY(nullResult.value->isEmpty());
    }

    void privilegesRejectMalformedRows()
    {
        const QString endpoint = QStringLiteral("api/listen/together/privilege/get");
        const auto wrongResult = parsePrivileges(envelope(QJsonObject{
            {QStringLiteral("result"), QStringLiteral("bad")}}));
        QVERIFY(!wrongResult.ok());
        QCOMPARE(wrongResult.error.endpoint, endpoint);
        QCOMPARE(wrongResult.error.path,
                 QStringLiteral("listenTogether.privileges.data.result"));

        const auto wrongRow = parsePrivileges(envelope(QJsonObject{
            {QStringLiteral("result"), QJsonArray{QStringLiteral("bad")}}}));
        QVERIFY(!wrongRow.ok());
        QCOMPARE(wrongRow.error.path,
                 QStringLiteral("listenTogether.privileges.data.result[0]"));

        const auto wrongPlayable = parsePrivileges(envelope(QJsonObject{
            {QStringLiteral("result"),
             QJsonArray{QJsonObject{{QStringLiteral("playable"), QStringLiteral("yes")}}}}}));
        QVERIFY(!wrongPlayable.ok());
        QCOMPARE(wrongPlayable.error.path,
                 QStringLiteral("listenTogether.privileges.data.result[0].playable"));

        const auto wrongUsers = parsePrivileges(envelope(QJsonObject{
            {QStringLiteral("result"),
             QJsonArray{QJsonObject{{QStringLiteral("playable"), true},
                                    {QStringLiteral("unplayableUserIds"),
                                     QStringLiteral("bad")}}}}}));
        QVERIFY(!wrongUsers.ok());
        QCOMPARE(wrongUsers.error.path,
                 QStringLiteral("listenTogether.privileges.data.result[0].unplayableUserIds"));

        const auto missingData = parsePrivileges(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!missingData.ok());
        QCOMPARE(missingData.error.path, QStringLiteral("listenTogether.privileges.data"));
    }

    void parsesRelationStatistics()
    {
        const auto result = parseRelationStatistics(envelope(QJsonObject{
            {QStringLiteral("listenCount"), 12},
            {QStringLiteral("totalConnectionTime"), 345}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->listenCount, qint64(12));
        QCOMPARE(result.value->totalConnectionTime, qint64(345));
    }

    void relationStatisticsToleratesMissingCounters()
    {
        const auto result = parseRelationStatistics(envelope(QJsonObject{}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->listenCount, qint64(0));
        QCOMPARE(result.value->totalConnectionTime, qint64(0));
    }

    void relationStatisticsRejectsWrongTypes()
    {
        const QString endpoint =
            QStringLiteral("api/listen/together/relation/statistics/get/v2");
        const auto wrongCount = parseRelationStatistics(envelope(QJsonObject{
            {QStringLiteral("listenCount"), QStringLiteral("12")}}));
        QVERIFY(!wrongCount.ok());
        QCOMPARE(wrongCount.error.endpoint, endpoint);
        QCOMPARE(wrongCount.error.path,
                 QStringLiteral("listenTogether.statistics.data.listenCount"));

        const auto wrongTime = parseRelationStatistics(envelope(QJsonObject{
            {QStringLiteral("totalConnectionTime"), true}}));
        QVERIFY(!wrongTime.ok());
        QCOMPARE(wrongTime.error.path,
                 QStringLiteral("listenTogether.statistics.data.totalConnectionTime"));

        const auto missingData = parseRelationStatistics(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!missingData.ok());
        QCOMPARE(missingData.error.path,
                 QStringLiteral("listenTogether.statistics.data"));
    }

    void parsesActionResults()
    {
        const auto rootMessage = parseActionResult(QJsonObject{
            {QStringLiteral("code"), 200},
            {QStringLiteral("message"), QStringLiteral("invited")}});
        QVERIFY2(rootMessage.ok(), qPrintable(rootMessage.error.message));
        QVERIFY(rootMessage.value->success);
        QCOMPARE(rootMessage.value->message, QStringLiteral("invited"));

        const auto dataMessage = parseActionResult(QJsonObject{
            {QStringLiteral("code"), 200},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("message"),
                                                  QStringLiteral("gifted")}}}});
        QVERIFY2(dataMessage.ok(), qPrintable(dataMessage.error.message));
        QCOMPARE(dataMessage.value->message, QStringLiteral("gifted"));

        const auto wrongMessage = parseActionResult(QJsonObject{
            {QStringLiteral("code"), 200},
            {QStringLiteral("message"), 42}});
        QVERIFY(!wrongMessage.ok());
        QCOMPARE(wrongMessage.error.path,
                 QStringLiteral("listenTogether.action.message"));

        const auto wrongData = parseActionResult(QJsonObject{
            {QStringLiteral("code"), 200},
            {QStringLiteral("data"), QStringLiteral("bad")}});
        QVERIFY(!wrongData.ok());
        QCOMPARE(wrongData.error.path,
                 QStringLiteral("listenTogether.action.data"));
    }

    void parsesResultFlags()
    {
        const auto viaResult = parseListenResult(envelope(QJsonObject{
            {QStringLiteral("result"), true},
            {QStringLiteral("message"), QStringLiteral("ok")}}));
        QVERIFY2(viaResult.ok(), qPrintable(viaResult.error.message));
        QVERIFY(viaResult.value->success);
        QCOMPARE(viaResult.value->message, QStringLiteral("ok"));

        const auto viaSuccess = parseListenResult(envelope(QJsonObject{
            {QStringLiteral("success"), true}}));
        QVERIFY2(viaSuccess.ok(), qPrintable(viaSuccess.error.message));
        QVERIFY(viaSuccess.value->success);

        const auto rootMessage = parseListenResult(QJsonObject{
            {QStringLiteral("code"), 200},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("result"), true}}},
            {QStringLiteral("message"), QStringLiteral("done")}});
        QVERIFY2(rootMessage.ok(), qPrintable(rootMessage.error.message));
        QCOMPARE(rootMessage.value->message, QStringLiteral("done"));

        const auto dataMessage = parseListenResult(QJsonObject{
            {QStringLiteral("code"), 200},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("result"), true},
                                                 {QStringLiteral("message"),
                                                  QStringLiteral("inner")}}},
            {QStringLiteral("message"), QStringLiteral("outer")}});
        QVERIFY2(dataMessage.ok(), qPrintable(dataMessage.error.message));
        QCOMPARE(dataMessage.value->message, QStringLiteral("inner"));

        const auto failure = parseListenResult(envelope(QJsonObject{
            {QStringLiteral("result"), false},
            {QStringLiteral("message"), QStringLiteral("room closed")}}));
        QVERIFY2(failure.ok(), qPrintable(failure.error.message));
        QVERIFY(!failure.value->success);
        QCOMPARE(failure.value->message, QStringLiteral("room closed"));
    }

    void resultRequiresDataAndFlag()
    {
        const auto missing = parseListenResult(
            QJsonObject{{QStringLiteral("code"), 200}});
        QVERIFY(!missing.ok());
        QCOMPARE(missing.error.kind, ErrorKind::Parse);
        QCOMPARE(missing.error.endpoint, ResultEndpoint);
        QCOMPARE(missing.error.path, QStringLiteral("listenTogether.result.data"));

        const auto noFlag = parseListenResult(envelope(QJsonObject{}));
        QVERIFY(!noFlag.ok());
        QCOMPARE(noFlag.error.path, QStringLiteral("listenTogether.result.data"));

        const auto wrongData = parseListenResult(
            QJsonObject{{QStringLiteral("code"), 200},
                        {QStringLiteral("data"), QStringLiteral("bad")}});
        QVERIFY(!wrongData.ok());
        QCOMPARE(wrongData.error.path, QStringLiteral("listenTogether.result.data"));
    }

    void resultRejectsWrongFlagTypes()
    {
        const auto result = parseListenResult(envelope(QJsonObject{
            {QStringLiteral("result"), QStringLiteral("true")}}));
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path,
                 QStringLiteral("listenTogether.result.data.result"));

        const auto success = parseListenResult(envelope(QJsonObject{
            {QStringLiteral("success"), 1}}));
        QVERIFY(!success.ok());
        QCOMPARE(success.error.path,
                 QStringLiteral("listenTogether.result.data.success"));

        const auto message = parseListenResult(envelope(QJsonObject{
            {QStringLiteral("result"), true},
            {QStringLiteral("message"), 42}}));
        QVERIFY(!message.ok());
        QCOMPARE(message.error.path,
                 QStringLiteral("listenTogether.result.data.message"));
    }
};

QTEST_GUILESS_MAIN(ListenTogetherTest)
#include "listen_together_test.moc"
