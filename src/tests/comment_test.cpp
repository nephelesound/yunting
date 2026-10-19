// Typed Comment SDK contract: hot comments, cursor pages and floor replies.
#include "sdk/comment.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace ncm::sdk;

class SdkCommentTest : public QObject {
    Q_OBJECT

    static QJsonObject commentObject()
    {
        return QJsonObject{
            {"user", QJsonObject{{"userId", 7}, {"nickname", "u"},
                                 {"avatarUrl", "http://cover/u.jpg"},
                                 {"avatarDetail", QJsonObject{{"identityIconUrl",
                                                              "http://cover/id.png"}}}}},
            {"pendantData", QJsonObject{{"imageUrl", "http://cover/p.png"}}},
            {"content", "hello"},
            {"likedCount", 3},
            {"time", QJsonValue(qint64(1700000000000))},
            {"commentId", 42},
            {"liked", true},
            {"parentCommentId", 1},
            {"beReplied", QJsonArray{QJsonObject{
                 {"beRepliedCommentId", 9},
                 {"content", "old"},
                 {"user", QJsonObject{{"userId", 8}, {"nickname", "v"}}},
             }}},
            {"showFloorComment", QJsonObject{{"replyCount", 14}}},
        };
    }

    static QJsonObject dataRoot(const QJsonObject &data)
    {
        return QJsonObject{{"code", 200}, {"data", data}};
    }

private slots:
    void parsesCommentFields()
    {
        const auto result = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{commentObject()}},
        }));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->comments.size(), 1);
        const Comment &comment = result.value->comments.first();
        QCOMPARE(comment.user, QStringLiteral("u"));
        QCOMPARE(comment.userId, qint64(7));
        QCOMPARE(comment.avatar, QStringLiteral("http://cover/u.jpg"));
        QCOMPARE(comment.pendant, QStringLiteral("http://cover/p.png"));
        QCOMPARE(comment.identityIcon, QStringLiteral("http://cover/id.png"));
        QCOMPARE(comment.content, QStringLiteral("hello"));
        QCOMPARE(comment.likes, qint64(3));
        QCOMPARE(comment.time, qint64(1700000000000));
        QCOMPARE(comment.id, qint64(42));
        QCOMPARE(comment.liked, true);
        QCOMPARE(comment.parentId, qint64(1));
        QCOMPARE(comment.replyCount, qint64(14));
        QCOMPARE(comment.beReplied.size(), 1);
        const RepliedComment &replied = comment.beReplied.first();
        QCOMPARE(replied.id, qint64(9));
        QCOMPARE(replied.content, QStringLiteral("old"));
        QCOMPARE(replied.userId, qint64(8));
        QCOMPARE(replied.user, QStringLiteral("v"));
        QCOMPARE(result.value->total, qint64(0));
        QCOMPARE(result.value->cursor, QString());
        QCOMPARE(result.value->hasMore, false);
    }

    // 缺失或 null 的可选字段有确定默认值；user/beReplied/showFloorComment 都是可选的
    void commentMissingOptionalFieldsUseDefaults()
    {
        const auto missing = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{QJsonObject{{"commentId", 5}}}},
        }));
        QVERIFY2(missing.ok(), qPrintable(missing.error.message));
        const Comment &comment = missing.value->comments.first();
        QCOMPARE(comment.id, qint64(5));
        QCOMPARE(comment.user, QString());
        QCOMPARE(comment.userId, qint64(0));
        QCOMPARE(comment.avatar, QString());
        QCOMPARE(comment.content, QString());
        QCOMPARE(comment.likes, qint64(0));
        QCOMPARE(comment.time, qint64(0));
        QCOMPARE(comment.liked, false);
        QCOMPARE(comment.parentId, qint64(0));
        QVERIFY(comment.beReplied.isEmpty());
        QCOMPARE(comment.replyCount, qint64(0));

        const auto nulls = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{QJsonObject{
                 {"commentId", 6},
                 {"user", QJsonValue::Null},
                 {"beReplied", QJsonValue::Null},
                 {"showFloorComment", QJsonValue::Null},
             }}},
        }));
        QVERIFY2(nulls.ok(), qPrintable(nulls.error.message));
        const Comment &nullComment = nulls.value->comments.first();
        QCOMPARE(nullComment.id, qint64(6));
        QCOMPARE(nullComment.user, QString());
        QVERIFY(nullComment.beReplied.isEmpty());
        QCOMPARE(nullComment.replyCount, qint64(0));
    }

    // 热评保持接口顺序，最多 10 条；第 10 条之后的畸形项不会被解析
    void hotCommentsCapAtTenAndKeepOrder()
    {
        QJsonArray comments;
        for (int i = 1; i <= 12; ++i)
            comments.append(QJsonObject{{"commentId", i}, {"time", i}});
        comments.append(QStringLiteral("ignored past the hot cap"));

        const auto result = parseHotComments(dataRoot(QJsonObject{
            {"comments", comments},
            {"totalCount", 99},
            {"cursor", "normalHot#2"},
            {"hasMore", false},
        }));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->comments.size(), 10);
        QCOMPARE(result.value->comments.first().id, qint64(1));
        QCOMPARE(result.value->comments.last().id, qint64(10));
        QCOMPARE(result.value->total, qint64(99));
        QCOMPARE(result.value->cursor, QStringLiteral("normalHot#2"));
        QCOMPARE(result.value->hasMore, false);
    }

    // 10 条上限只属于热评；时间分页保留整页
    void pageKeepsAllComments()
    {
        QJsonArray comments;
        for (int i = 1; i <= 12; ++i)
            comments.append(QJsonObject{{"commentId", i}});
        const auto result = parseCommentPage(dataRoot(QJsonObject{{"comments", comments}}));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->comments.size(), 12);
    }

    // comments 是可选数组：缺失或 null 都表示空线程，不是解析错误
    void missingCommentsArrayIsEmpty()
    {
        const auto hot = parseHotComments(dataRoot(QJsonObject{{"totalCount", 5}}));
        QVERIFY2(hot.ok(), qPrintable(hot.error.message));
        QVERIFY(hot.value->comments.isEmpty());
        QCOMPARE(hot.value->total, qint64(5));

        const auto nullArray = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonValue::Null},
            {"hasMore", true},
            {"cursor", "c"},
        }));
        QVERIFY2(nullArray.ok(), qPrintable(nullArray.error.message));
        QVERIFY(nullArray.value->comments.isEmpty());
        QCOMPARE(nullArray.value->hasMore, true);
        QCOMPARE(nullArray.value->cursor, QStringLiteral("c"));
    }

    // 分页形状：totalCount / cursor / hasMore 与每条评论的 time（下一页游标）都要保留
    void pageCarriesCursorAndMore()
    {
        const auto result = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{commentObject()}},
            {"totalCount", 123456},
            {"cursor", "19"},
            {"hasMore", true},
        }));
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->comments.size(), 1);
        QCOMPARE(result.value->total, qint64(123456));
        QCOMPARE(result.value->cursor, QStringLiteral("19"));
        QCOMPARE(result.value->hasMore, true);
        QCOMPARE(result.value->comments.first().time, qint64(1700000000000));
    }

    void v2MissingDataIsParseError()
    {
        const auto hot = parseHotComments(QJsonObject{{"code", 200}});
        QVERIFY(!hot.ok());
        QCOMPARE(hot.error.kind, ErrorKind::Parse);
        QCOMPARE(hot.error.endpoint, QStringLiteral("api/v2/resource/comments"));
        QCOMPARE(hot.error.path, QStringLiteral("data"));
        QVERIFY(hot.error.message.contains(QStringLiteral("missing object")));

        const auto page =
            parseCommentPage(QJsonObject{{"code", 200}, {"data", QJsonArray{}}});
        QVERIFY(!page.ok());
        QCOMPARE(page.error.kind, ErrorKind::Parse);
        QCOMPARE(page.error.path, QStringLiteral("data"));
        QVERIFY(page.error.message.contains(QStringLiteral("expected object")));
    }

    void commentsArrayWrongTypeIsParseError()
    {
        const auto result = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QStringLiteral("nope")},
            {"totalCount", 1},
        }));
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("api/v2/resource/comments"));
        QCOMPARE(result.error.path, QStringLiteral("data.comments"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected array")));
    }

    void pageScalarWrongTypeIsParseError()
    {
        const auto total = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{}},
            {"totalCount", "many"},
        }));
        QVERIFY(!total.ok());
        QCOMPARE(total.error.path, QStringLiteral("data"));
        QVERIFY(total.error.message.contains(QStringLiteral("data.totalCount")));

        const auto cursor = parseHotComments(dataRoot(QJsonObject{
            {"comments", QJsonArray{}},
            {"cursor", 19},
        }));
        QVERIFY(!cursor.ok());
        QCOMPARE(cursor.error.path, QStringLiteral("data"));
        QVERIFY(cursor.error.message.contains(QStringLiteral("data.cursor")));

        const auto more = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{}},
            {"hasMore", 1},
        }));
        QVERIFY(!more.ok());
        QCOMPARE(more.error.path, QStringLiteral("data"));
        QVERIFY(more.error.message.contains(QStringLiteral("data.hasMore")));
    }

    void malformedCommentItemReportsPath()
    {
        const auto item = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{QStringLiteral("nope")}},
        }));
        QVERIFY(!item.ok());
        QCOMPARE(item.error.path, QStringLiteral("data.comments[0]"));
        QVERIFY(item.error.message.contains(QStringLiteral("expected object")));

        const auto field = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{QJsonObject{
                 {"commentId", 1},
                 {"likedCount", "3"},
             }}},
        }));
        QVERIFY(!field.ok());
        QCOMPARE(field.error.path, QStringLiteral("data.comments[0]"));
        QVERIFY(field.error.message.contains(QStringLiteral("data.comments[0].likedCount")));

        const auto user = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{QJsonObject{
                 {"commentId", 1},
                 {"user", QStringLiteral("u")},
             }}},
        }));
        QVERIFY(!user.ok());
        QCOMPARE(user.error.path, QStringLiteral("data.comments[0].user"));
        QVERIFY(user.error.message.contains(QStringLiteral("expected object")));

        const auto replied = parseCommentPage(dataRoot(QJsonObject{
            {"comments", QJsonArray{QJsonObject{
                 {"commentId", 1},
                 {"beReplied", QJsonObject{}},
             }}},
        }));
        QVERIFY(!replied.ok());
        QCOMPARE(replied.error.path, QStringLiteral("data.comments[0]"));
        QVERIFY(replied.error.message.contains(QStringLiteral("beReplied: expected array")));
    }

    void repliesUseDataCommentsAndRootMore()
    {
        const auto result = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"comments", QJsonArray{
                 commentObject(),
                 QJsonObject{{"commentId", 43}, {"time", 18}},
             }}}},
            {"comments", QJsonArray{QJsonObject{{"commentId", 99}}}},
            {"more", true},
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->comments.size(), 2);
        QCOMPARE(result.value->comments.first().id, qint64(42));
        QCOMPARE(result.value->comments.last().id, qint64(43));
        QCOMPARE(result.value->comments.last().time, qint64(18));
        QCOMPARE(result.value->more, true);
    }

    void repliesFallBackToRootComments()
    {
        const auto emptyData = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"comments", QJsonArray{}}}},
            {"comments", QJsonArray{QJsonObject{{"commentId", 7}, {"time", 70}}}},
            {"more", false},
        });
        QVERIFY2(emptyData.ok(), qPrintable(emptyData.error.message));
        QCOMPARE(emptyData.value->comments.size(), 1);
        QCOMPARE(emptyData.value->comments.first().id, qint64(7));
        QCOMPARE(emptyData.value->comments.first().time, qint64(70));
        QCOMPARE(emptyData.value->more, false);

        const auto noData = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"comments", QJsonArray{QJsonObject{{"commentId", 8}}}},
        });
        QVERIFY2(noData.ok(), qPrintable(noData.error.message));
        QCOMPARE(noData.value->comments.size(), 1);
        QCOMPARE(noData.value->comments.first().id, qint64(8));
        QCOMPARE(noData.value->more, false);
    }

    void repliesEmptyPayloadIsValid()
    {
        const auto result = parseCommentReplies(QJsonObject{{"code", 200}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->comments.isEmpty());
        QCOMPARE(result.value->more, false);
    }

    void repliesMalformedInputIsParseError()
    {
        const auto data = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"data", QJsonArray{}},
        });
        QVERIFY(!data.ok());
        QCOMPARE(data.error.kind, ErrorKind::Parse);
        QCOMPARE(data.error.endpoint, QStringLiteral("resource/comment/floor/get"));
        QCOMPARE(data.error.path, QStringLiteral("data"));
        QVERIFY(data.error.message.contains(QStringLiteral("expected object")));

        const auto nested = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"comments", QStringLiteral("nope")}}},
        });
        QVERIFY(!nested.ok());
        QCOMPARE(nested.error.path, QStringLiteral("data.comments"));
        QVERIFY(nested.error.message.contains(QStringLiteral("expected array")));

        const auto fallback = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"comments", QJsonArray{}}}},
            {"comments", 42},
        });
        QVERIFY(!fallback.ok());
        QCOMPARE(fallback.error.path, QStringLiteral("comments"));
        QVERIFY(fallback.error.message.contains(QStringLiteral("expected array")));

        const auto item = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"comments", QJsonArray{QStringLiteral("nope")}},
        });
        QVERIFY(!item.ok());
        QCOMPARE(item.error.path, QStringLiteral("comments[0]"));

        const auto more = parseCommentReplies(QJsonObject{
            {"code", 200},
            {"comments", QJsonArray{}},
            {"more", 1},
        });
        QVERIFY(!more.ok());
        QCOMPARE(more.error.path, QStringLiteral("more"));
        QVERIFY(more.error.message.contains(QStringLiteral("expected boolean")));
    }

    // 资源类型前缀：歌曲 R_SO_4_，歌单 A_PL_0_（comment_like 模块的 resourceTypeMap）
    void commentThreadIdUsesResourcePrefix()
    {
        QCOMPARE(commentThreadId(CommentThreadKind::Song, 77), QStringLiteral("R_SO_4_77"));
        QCOMPARE(commentThreadId(CommentThreadKind::Playlist, 42), QStringLiteral("A_PL_0_42"));
    }
};

QTEST_GUILESS_MAIN(SdkCommentTest)
#include "comment_test.moc"
