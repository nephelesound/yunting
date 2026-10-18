// Typed Notification SDK contract: system notices, comment notifications and @me.
#include "sdk/notification.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class SdkNotificationTest : public QObject {
    Q_OBJECT

    static QString encode(const QJsonObject &object)
    {
        return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    static QJsonObject userObject(qint64 id, const QString &name, const QString &avatar)
    {
        return QJsonObject{{"userId", id}, {"nickname", name}, {"avatarUrl", avatar}};
    }

    static QJsonObject playlistPayload()
    {
        return QJsonObject{
            {"type", 2},
            {"user", userObject(1001, "收藏者", "http://avatar")},
            {"playlist", QJsonObject{
                 {"id", 153503871},
                 {"name", "我喜欢的音乐"},
                 {"coverImgUrl", "http://cover"},
             }},
        };
    }

    static QJsonObject generalPayload()
    {
        return QJsonObject{
            {"type", 1},
            {"user", userObject(1001, "通知用户", "http://avatar2")},
            {"comment", QJsonObject{
                 {"commentId", 777},
                 {"content", "被赞的评论"},
                 {"time", 1589466289378.0},
                 {"parentCommentId", 5},
             }},
            {"generalNotice", QJsonObject{
                 {"actionDesc", "赞了你的评论"},
                 {"content", "通知正文"},
                 {"webUrl", "https://music.163.com/notice"},
                 {"nativeUrl", "orpheus://notice/1"},
                 {"type", 3},
                 {"resourceId", 456},
                 {"specialResourceType", QJsonValue::Null},
             }},
        };
    }

    static QJsonObject noticeRow(const QJsonObject &payload, const QJsonValue &id,
                                 const QJsonValue &userId, qint64 time)
    {
        return QJsonObject{
            {"queued", false},
            {"id", id},
            {"userId", userId},
            {"time", time},
            {"notice", encode(payload)},
            {"pushMsg", QJsonValue::Null},
            {"type", 0},
        };
    }

    static QJsonObject commentObject()
    {
        return QJsonObject{
            {"user", userObject(1, "回复者", "http://a")},
            {"beRepliedUser", userObject(2, "我", "http://b")},
            {"beRepliedContent", "原评论"},
            {"time", 1700000000000.0},
            {"content", "回复内容"},
            {"combindId", "1700000000000_2_4"},
            {"expressionUrl", "http://expr"},
            {"commentLocationType", 1},
            {"parentCommentId", 99},
            {"resourceType", 4},
        };
    }

    static QJsonObject songObject()
    {
        return QJsonObject{
            {"id", 111},
            {"name", "分享的歌"},
            {"artists", QJsonArray{QJsonObject{{"name", "歌手"}}}},
            {"album", QJsonObject{{"picUrl", "http://songcover"}}},
        };
    }

    static QJsonObject sharedEvent(const QJsonObject &payload)
    {
        return QJsonObject{
            {"id", 9001},
            {"type", 13},
            {"user", userObject(77, "动态作者", "http://event")},
            {"json", encode(payload)},
        };
    }

    static QJsonObject forwardRow(const QJsonObject &payload, const QJsonValue &id,
                                  qint64 time)
    {
        return QJsonObject{
            {"id", id},
            {"userId", 42},
            {"time", time},
            {"type", 1},
            {"json", encode(payload)},
        };
    }

private slots:
    void parsesSystemNotices()
    {
        const QJsonObject root{
            {"code", 200},
            {"more", true},
            {"notices", QJsonArray{
                 noticeRow(playlistPayload(), QStringLiteral("49595824594"),
                           QStringLiteral("1001"), 1589466289377),
                 noticeRow(generalPayload(), QJsonValue(qint64(49595824595)),
                           QJsonValue(qint64(1001)), 1589466289378),
             }},
        };

        const auto result = parseSystemNotices(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const SystemNoticePage &page = *result.value;
        QCOMPARE(page.more, true);
        QCOMPARE(page.items.size(), 2);
        QCOMPARE(page.nextTime, qint64(1589466289378));

        const SystemNotice &playlistNotice = page.items.at(0);
        QCOMPARE(playlistNotice.id, qint64(49595824594));
        QCOMPARE(playlistNotice.userId, qint64(1001));
        QCOMPARE(playlistNotice.time, qint64(1589466289377));
        QCOMPARE(playlistNotice.queued, false);
        QCOMPARE(playlistNotice.type, 0);
        QVERIFY(playlistNotice.payloadValid);
        QCOMPARE(playlistNotice.payload.type, 2);
        QCOMPARE(playlistNotice.payload.user.id, qint64(1001));
        QCOMPARE(playlistNotice.payload.user.name, QStringLiteral("收藏者"));
        QCOMPARE(playlistNotice.payload.user.avatar, QStringLiteral("http://avatar"));
        QCOMPARE(playlistNotice.payload.playlistId, qint64(153503871));
        QCOMPARE(playlistNotice.payload.playlistName, QStringLiteral("我喜欢的音乐"));
        QCOMPARE(playlistNotice.payload.playlistCover, QStringLiteral("http://cover"));

        const SystemNotice &generalNotice = page.items.at(1);
        QCOMPARE(generalNotice.id, qint64(49595824595));
        QVERIFY(generalNotice.payloadValid);
        QCOMPARE(generalNotice.payload.commentId, qint64(777));
        QCOMPARE(generalNotice.payload.commentContent, QStringLiteral("被赞的评论"));
        QCOMPARE(generalNotice.payload.commentTime, qint64(1589466289378));
        QCOMPARE(generalNotice.payload.commentParentId, qint64(5));
        QCOMPARE(generalNotice.payload.actionDesc, QStringLiteral("赞了你的评论"));
        QCOMPARE(generalNotice.payload.content, QStringLiteral("通知正文"));
        QCOMPARE(generalNotice.payload.webUrl,
                 QStringLiteral("https://music.163.com/notice"));
        QCOMPARE(generalNotice.payload.nativeUrl, QStringLiteral("orpheus://notice/1"));
        QCOMPARE(generalNotice.payload.generalType, 3);
        QCOMPARE(generalNotice.payload.resourceId, qint64(456));
        QCOMPARE(generalNotice.payload.specialResourceType, 0);
    }

    void parsesCommentNotifications()
    {
        const QJsonObject root{
            {"code", 200},
            {"total", 12},
            {"more", true},
            {"comments", QJsonArray{commentObject()}},
        };

        const auto result = parseCommentNotifications(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const CommentNotificationPage &page = *result.value;
        QCOMPARE(page.more, true);
        QCOMPARE(page.total, qint64(12));
        QCOMPARE(page.items.size(), 1);
        QCOMPARE(page.nextBeforeTime, qint64(1700000000000));

        const CommentNotification &notification = page.items.first();
        QCOMPARE(notification.user.id, qint64(1));
        QCOMPARE(notification.user.name, QStringLiteral("回复者"));
        QCOMPARE(notification.user.avatar, QStringLiteral("http://a"));
        QCOMPARE(notification.repliedUser.id, qint64(2));
        QCOMPARE(notification.repliedUser.name, QStringLiteral("我"));
        QCOMPARE(notification.repliedContent, QStringLiteral("原评论"));
        QCOMPARE(notification.content, QStringLiteral("回复内容"));
        QCOMPARE(notification.time, qint64(1700000000000));
        QCOMPARE(notification.parentCommentId, qint64(99));
        QCOMPARE(notification.resourceType, 4);
        QCOMPARE(notification.commentLocationType, 1);
        QCOMPARE(notification.combinedId, QStringLiteral("1700000000000_2_4"));
        QCOMPARE(notification.expressionUrl, QStringLiteral("http://expr"));
        QVERIFY(notification.repliedExpressionUrl.isEmpty());
    }

    void parsesForwardNotifications()
    {
        const QJsonObject firstPayload{
            {"user", userObject(42, "阿婆主", "http://u")},
            {"type", 1},
            {"json", encode(QJsonObject{
                 {"msg", "@了你"},
                 {"event", sharedEvent(QJsonObject{
                      {"msg", "事件正文"},
                      {"song", songObject()},
                  })},
             })},
        };
        const QJsonObject playlist{
            {"id", 321},
            {"name", "分享歌单名"},
            {"coverImgUrl", "http://pl"},
            {"creator", userObject(5, "歌单主", QString())},
        };
        const QJsonObject secondPayload{
            {"json", encode(QJsonObject{
                 {"msg", "分享歌单"},
                 {"playlist", playlist},
             })},
        };
        const QJsonObject root{
            {"code", 200},
            {"newCount", 3},
            {"lasttime", 1700000000000.0},
            {"more", true},
            {"forwards", QJsonArray{
                 forwardRow(firstPayload, 777, 1700000000000),
                 forwardRow(secondPayload, QStringLiteral("888"), 1700000000001),
             }},
        };

        const auto result = parseForwardNotifications(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const ForwardNotificationPage &page = *result.value;
        QCOMPARE(page.more, true);
        QCOMPARE(page.newCount, 3);
        QCOMPARE(page.lasttime, qint64(1700000000000));
        QCOMPARE(page.offset, 0);
        QCOMPARE(page.items.size(), 2);

        const ForwardNotification &shared = page.items.at(0);
        QCOMPARE(shared.id, qint64(777));
        QCOMPARE(shared.userId, qint64(42));
        QCOMPARE(shared.time, qint64(1700000000000));
        QCOMPARE(shared.type, 1);
        QVERIFY(shared.payloadValid);
        QCOMPARE(shared.message, QStringLiteral("@了你"));
        QCOMPARE(shared.user.id, qint64(42));
        QCOMPARE(shared.user.name, QStringLiteral("阿婆主"));
        QCOMPARE(shared.eventType, 1);
        QCOMPARE(shared.resourceKind, QStringLiteral("song"));
        QCOMPARE(shared.resourceId, qint64(111));
        QCOMPARE(shared.resourceTitle, QStringLiteral("分享的歌"));
        QCOMPARE(shared.resourceCover, QStringLiteral("http://songcover"));
        QCOMPARE(shared.resourceArtist, QStringLiteral("歌手"));

        const ForwardNotification &direct = page.items.at(1);
        QCOMPARE(direct.id, qint64(888));
        QVERIFY(direct.payloadValid);
        QCOMPARE(direct.message, QStringLiteral("分享歌单"));
        QVERIFY(direct.user.name.isEmpty());
        QCOMPARE(direct.eventType, 0);
        QCOMPARE(direct.resourceKind, QStringLiteral("playlist"));
        QCOMPARE(direct.resourceId, qint64(321));
        QCOMPARE(direct.resourceTitle, QStringLiteral("分享歌单名"));
        QCOMPARE(direct.resourceCover, QStringLiteral("http://pl"));
        QCOMPARE(direct.resourceArtist, QStringLiteral("歌单主"));
    }

    void forwardEventFallbacks()
    {
        const QJsonObject event{
            {"id", 9001},
            {"type", 13},
            {"user", userObject(77, "动态作者", "http://event")},
            {"json", encode(QJsonObject{{"msg", "事件正文"}})},
        };
        const QJsonObject payload{
            {"json", encode(QJsonObject{{"event", event}})},
        };
        const auto result = parseForwardNotifications(QJsonObject{
            {"code", 200},
            {"forwards", QJsonArray{forwardRow(payload, 999, 1700000000002)}},
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const ForwardNotification &notification = result.value->items.first();
        QVERIFY(notification.payloadValid);
        QCOMPARE(notification.message, QStringLiteral("事件正文"));
        QCOMPARE(notification.user.id, qint64(77));
        QCOMPARE(notification.user.name, QStringLiteral("动态作者"));
        QCOMPARE(notification.eventType, 13);
        QCOMPARE(notification.resourceKind, QStringLiteral("event"));
        QCOMPARE(notification.resourceId, qint64(9001));
    }

    void missingArraysAreParseErrors()
    {
        const auto notices = parseSystemNotices(QJsonObject{{"code", 200}});
        QVERIFY(!notices.ok());
        QCOMPARE(notices.error.kind, ErrorKind::Parse);
        QCOMPARE(notices.error.endpoint, QStringLiteral("msg/notices"));
        QCOMPARE(notices.error.path, QStringLiteral("notification.notices.notices"));
        QVERIFY(notices.error.message.contains(QStringLiteral("missing array")));

        const auto comments = parseCommentNotifications(QJsonObject{{"code", 200}});
        QVERIFY(!comments.ok());
        QCOMPARE(comments.error.kind, ErrorKind::Parse);
        QCOMPARE(comments.error.endpoint, QStringLiteral("v1/user/comments/"));
        QCOMPARE(comments.error.path, QStringLiteral("notification.comments.comments"));
        QVERIFY(comments.error.message.contains(QStringLiteral("missing array")));

        const auto forwards = parseForwardNotifications(QJsonObject{{"code", 200}});
        QVERIFY(!forwards.ok());
        QCOMPARE(forwards.error.kind, ErrorKind::Parse);
        QCOMPARE(forwards.error.endpoint, QStringLiteral("forwards/get"));
        QCOMPARE(forwards.error.path, QStringLiteral("notification.forwards.forwards"));
        QVERIFY(forwards.error.message.contains(QStringLiteral("missing array")));
    }

    void wrongArrayAndItemTypesAreParseErrors()
    {
        const auto notices = parseSystemNotices(
            QJsonObject{{"code", 200}, {"notices", QStringLiteral("bad")}});
        QVERIFY(!notices.ok());
        QCOMPARE(notices.error.kind, ErrorKind::Parse);
        QCOMPARE(notices.error.path, QStringLiteral("notification.notices.notices"));
        QVERIFY(notices.error.message.contains(QStringLiteral("expected array")));

        const auto comments = parseCommentNotifications(
            QJsonObject{{"code", 200}, {"comments", QJsonArray{1}}});
        QVERIFY(!comments.ok());
        QCOMPARE(comments.error.kind, ErrorKind::Parse);
        QCOMPARE(comments.error.path, QStringLiteral("notification.comments.comments[0]"));
        QVERIFY(comments.error.message.contains(QStringLiteral("expected object")));

        const auto forwards = parseForwardNotifications(
            QJsonObject{{"code", 200}, {"forwards", QStringLiteral("bad")}});
        QVERIFY(!forwards.ok());
        QCOMPARE(forwards.error.kind, ErrorKind::Parse);
        QCOMPARE(forwards.error.path, QStringLiteral("notification.forwards.forwards"));
        QVERIFY(forwards.error.message.contains(QStringLiteral("expected array")));
    }

    void fieldTypeErrorsCarryEndpointAndPath()
    {
        const QJsonObject badNotice{
            {"id", 1},
            {"userId", 2},
            {"time", 3},
            {"queued", QStringLiteral("not-a-bool")},
        };
        const auto notices = parseSystemNotices(
            QJsonObject{{"code", 200}, {"notices", QJsonArray{badNotice}}});
        QVERIFY(!notices.ok());
        QCOMPARE(notices.error.kind, ErrorKind::Parse);
        QCOMPARE(notices.error.endpoint, QStringLiteral("msg/notices"));
        QCOMPARE(notices.error.path, QStringLiteral("notification.notices.notices[0]"));
        QVERIFY(notices.error.message.contains(QStringLiteral("queued")));

        const QJsonObject badComment{
            {"content", "回复"},
            {"time", QStringLiteral("not-a-number")},
        };
        const auto comments = parseCommentNotifications(
            QJsonObject{{"code", 200}, {"comments", QJsonArray{badComment}}});
        QVERIFY(!comments.ok());
        QCOMPARE(comments.error.kind, ErrorKind::Parse);
        QCOMPARE(comments.error.endpoint, QStringLiteral("v1/user/comments/"));
        QCOMPARE(comments.error.path, QStringLiteral("notification.comments.comments[0]"));
        QVERIFY(comments.error.message.contains(QStringLiteral("time")));

        const QJsonObject badForward{
            {"id", 9},
            {"userId", 10},
            {"time", QStringLiteral("not-a-number")},
        };
        const auto forwards = parseForwardNotifications(
            QJsonObject{{"code", 200}, {"forwards", QJsonArray{badForward}}});
        QVERIFY(!forwards.ok());
        QCOMPARE(forwards.error.kind, ErrorKind::Parse);
        QCOMPARE(forwards.error.endpoint, QStringLiteral("forwards/get"));
        QCOMPARE(forwards.error.path, QStringLiteral("notification.forwards.forwards[0]"));
        QVERIFY(forwards.error.message.contains(QStringLiteral("time")));
    }

    void optionalFieldsAreTolerated()
    {
        const QJsonObject notice{
            {"id", 1},
            {"userId", 2},
            {"time", 3},
        };
        const auto notices = parseSystemNotices(
            QJsonObject{{"code", 200}, {"notices", QJsonArray{notice}}});
        QVERIFY2(notices.ok(), qPrintable(notices.error.message));
        QCOMPARE(notices.value->more, false);
        QCOMPARE(notices.value->nextTime, qint64(3));
        const SystemNotice &systemNotice = notices.value->items.first();
        QVERIFY(!systemNotice.payloadValid);
        QCOMPARE(systemNotice.queued, false);
        QCOMPARE(systemNotice.payload.type, 0);
        QVERIFY(systemNotice.payload.user.name.isEmpty());
        QCOMPARE(systemNotice.payload.playlistId, qint64(0));

        const QJsonObject comment{{"time", 5}};
        const auto comments = parseCommentNotifications(
            QJsonObject{{"code", 200}, {"comments", QJsonArray{comment}}});
        QVERIFY2(comments.ok(), qPrintable(comments.error.message));
        QCOMPARE(comments.value->more, false);
        QCOMPARE(comments.value->total, qint64(0));
        QCOMPARE(comments.value->nextBeforeTime, qint64(5));
        const CommentNotification &commentNotification = comments.value->items.first();
        QVERIFY(commentNotification.user.name.isEmpty());
        QVERIFY(commentNotification.repliedUser.name.isEmpty());
        QVERIFY(commentNotification.repliedContent.isEmpty());
        QVERIFY(commentNotification.content.isEmpty());
        QVERIFY(commentNotification.combinedId.isEmpty());

        const QJsonObject forward{
            {"id", 9},
            {"userId", 10},
            {"time", 11},
        };
        const auto forwards = parseForwardNotifications(
            QJsonObject{{"code", 200}, {"forwards", QJsonArray{forward}}});
        QVERIFY2(forwards.ok(), qPrintable(forwards.error.message));
        QCOMPARE(forwards.value->more, false);
        QCOMPARE(forwards.value->newCount, 0);
        QCOMPARE(forwards.value->lasttime, qint64(0));
        QCOMPARE(forwards.value->offset, 0);
        const ForwardNotification &forwardNotification = forwards.value->items.first();
        QVERIFY(!forwardNotification.payloadValid);
        QVERIFY(forwardNotification.message.isEmpty());
        QVERIFY(forwardNotification.user.name.isEmpty());
        QVERIFY(forwardNotification.resourceKind.isEmpty());
    }

    void malformedEmbeddedPayloadsAreTolerated()
    {
        const QJsonObject notice{
            {"id", 1},
            {"userId", 2},
            {"time", 3},
            {"notice", QStringLiteral("not-json")},
        };
        const auto notices = parseSystemNotices(
            QJsonObject{{"code", 200}, {"notices", QJsonArray{notice}}});
        QVERIFY2(notices.ok(), qPrintable(notices.error.message));
        QVERIFY(!notices.value->items.first().payloadValid);
        QCOMPARE(notices.value->nextTime, qint64(3));

        const QJsonObject forward{
            {"id", 9},
            {"userId", 10},
            {"time", 11},
            {"json", QStringLiteral("not-json")},
        };
        const auto forwards = parseForwardNotifications(
            QJsonObject{{"code", 200}, {"forwards", QJsonArray{forward}}});
        QVERIFY2(forwards.ok(), qPrintable(forwards.error.message));
        QVERIFY(!forwards.value->items.first().payloadValid);
        QVERIFY(forwards.value->items.first().resourceKind.isEmpty());
    }

    void embeddedPayloadTypeErrorsAreIgnored()
    {
        const QJsonObject payload{
            {"type", 2},
            {"user", QStringLiteral("bad")},
            {"playlist", QStringLiteral("bad")},
        };
        const QJsonObject notice{
            {"id", 1},
            {"userId", 2},
            {"time", 3},
            {"notice", encode(payload)},
        };
        const auto result = parseSystemNotices(
            QJsonObject{{"code", 200}, {"notices", QJsonArray{notice}}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const SystemNotice &parsed = result.value->items.first();
        QVERIFY(parsed.payloadValid);
        QCOMPARE(parsed.payload.type, 2);
        QVERIFY(parsed.payload.user.name.isEmpty());
        QCOMPARE(parsed.payload.playlistId, qint64(0));
    }

    void emptyArraysAreValidPages()
    {
        const auto notices = parseSystemNotices(
            QJsonObject{{"code", 200}, {"notices", QJsonArray{}}});
        QVERIFY2(notices.ok(), qPrintable(notices.error.message));
        QVERIFY(notices.value->items.isEmpty());
        QCOMPARE(notices.value->nextTime, qint64(-1));

        const auto comments = parseCommentNotifications(
            QJsonObject{{"code", 200}, {"comments", QJsonArray{}}});
        QVERIFY2(comments.ok(), qPrintable(comments.error.message));
        QVERIFY(comments.value->items.isEmpty());
        QCOMPARE(comments.value->nextBeforeTime, qint64(-1));

        const auto forwards = parseForwardNotifications(
            QJsonObject{{"code", 200}, {"forwards", QJsonArray{}}});
        QVERIFY2(forwards.ok(), qPrintable(forwards.error.message));
        QVERIFY(forwards.value->items.isEmpty());
        QCOMPARE(forwards.value->offset, 0);
    }
};

QTEST_GUILESS_MAIN(SdkNotificationTest)
#include "sdk_notification_test.moc"
