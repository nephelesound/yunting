#include "sdk/comment.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto CommentsPath = "api/v2/resource/comments";
constexpr auto RepliesPath = "resource/comment/floor/get";
constexpr auto LikePath = "v1/comment/like";
constexpr auto UnlikePath = "v1/comment/unlike";
constexpr qsizetype HotLimit = 10;

QString indexedPath(const QString &arrayPath, qsizetype index)
{
    return arrayPath + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

// The comments array is optional: absent/null means an empty thread. A present
// value of another type is a contract error, just like any other field.
Result<QJsonArray> commentsArray(const QJsonObject &object, const QString &objectPath,
                                 const char *endpoint)
{
    const QString path = objectPath.isEmpty()
        ? QStringLiteral("comments")
        : objectPath + QStringLiteral(".comments");
    const QJsonValue value = object.value(QStringLiteral("comments"));
    if (value.isUndefined() || value.isNull())
        return Result<QJsonArray>::success({});
    if (!value.isArray())
        return Result<QJsonArray>::failure(endpointError(
            ErrorKind::Parse, endpoint, path + QStringLiteral(": expected array"), path));
    return Result<QJsonArray>::success(value.toArray());
}

Result<RepliedComment> repliedFrom(const QJsonValue &value, const QString &path,
                                   const char *endpoint)
{
    if (!value.isObject())
        return itemTypeFailure<RepliedComment>(endpoint, path);

    const QJsonObject object = value.toObject();
    JsonReader reader(object, path);
    const QJsonObject user = reader.object("user");
    if (!reader.ok())
        return parseFailure<RepliedComment>(endpoint, reader, path + QStringLiteral(".user"));
    JsonReader userReader(user, path + QStringLiteral(".user"));
    RepliedComment replied;
    replied.id = reader.integer("beRepliedCommentId");
    replied.content = reader.string("content");
    replied.userId = userReader.integer("userId");
    replied.user = userReader.string("nickname");
    if (!reader.ok())
        return parseFailure<RepliedComment>(endpoint, reader, path);
    if (!userReader.ok())
        return parseFailure<RepliedComment>(endpoint, userReader, path + QStringLiteral(".user"));
    return Result<RepliedComment>::success(replied);
}

Result<Comment> commentFrom(const QJsonValue &value, const QString &path,
                            const char *endpoint)
{
    if (!value.isObject())
        return itemTypeFailure<Comment>(endpoint, path);

    const QJsonObject object = value.toObject();
    JsonReader reader(object, path);
    const QJsonObject user = reader.object("user");
    if (!reader.ok())
        return parseFailure<Comment>(endpoint, reader, path + QStringLiteral(".user"));
    JsonReader userReader(user, path + QStringLiteral(".user"));
    Comment comment;
    comment.user = userReader.string("nickname");
    comment.userId = userReader.integer("userId");
    comment.avatar = userReader.string("avatarUrl");
    comment.content = reader.string("content");
    comment.likes = reader.integer("likedCount");
    comment.time = reader.integer("time");
    comment.id = reader.integer("commentId");
    comment.liked = reader.boolean("liked");
    comment.parentId = reader.integer("parentCommentId");
    const QJsonArray replied = reader.array("beReplied");
    const QJsonObject floor = reader.object("showFloorComment");
    JsonReader floorReader(floor, path + QStringLiteral(".showFloorComment"));
    comment.replyCount = floorReader.integer("replyCount");
    if (!reader.ok())
        return parseFailure<Comment>(endpoint, reader, path);
    if (!userReader.ok())
        return parseFailure<Comment>(endpoint, userReader, path + QStringLiteral(".user"));
    if (!floorReader.ok())
        return parseFailure<Comment>(endpoint, floorReader,
                                     path + QStringLiteral(".showFloorComment"));

    comment.beReplied.reserve(replied.size());
    for (qsizetype i = 0; i < replied.size(); ++i) {
        const auto parsed =
            repliedFrom(replied.at(i), indexedPath(path + QStringLiteral(".beReplied"), i),
                        endpoint);
        if (!parsed)
            return Result<Comment>::failure(parsed.error);
        comment.beReplied.append(*parsed.value);
    }
    return Result<Comment>::success(comment);
}

Result<QList<Comment>> commentsFrom(const QJsonArray &values, const QString &arrayPath,
                                    const char *endpoint, qsizetype limit)
{
    const qsizetype count = limit >= 0 ? std::min(limit, values.size()) : values.size();
    QList<Comment> comments;
    comments.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        const auto parsed = commentFrom(values.at(i), indexedPath(arrayPath, i), endpoint);
        if (!parsed)
            return Result<QList<Comment>>::failure(parsed.error);
        comments.append(*parsed.value);
    }
    return Result<QList<Comment>>::success(std::move(comments));
}

Result<CommentPage> parseV2Page(const QJsonObject &root, qsizetype limit)
{
    const QJsonValue dataValue = root.value(QStringLiteral("data"));
    if (!dataValue.isObject()) {
        const QString message = dataValue.isUndefined()
            ? QStringLiteral("data: missing object")
            : QStringLiteral("data: expected object");
        return Result<CommentPage>::failure(endpointError(
            ErrorKind::Parse, CommentsPath, message, QStringLiteral("data")));
    }

    const QJsonObject data = dataValue.toObject();
    const auto values = commentsArray(data, QStringLiteral("data"), CommentsPath);
    if (!values)
        return Result<CommentPage>::failure(values.error);

    JsonReader reader(data, QStringLiteral("data"));
    CommentPage page;
    page.total = reader.integer("totalCount");
    page.cursor = reader.string("cursor");
    page.hasMore = reader.boolean("hasMore");
    if (!reader.ok())
        return parseFailure<CommentPage>(CommentsPath, reader, QStringLiteral("data"));

    const auto comments =
        commentsFrom(*values.value, QStringLiteral("data.comments"), CommentsPath, limit);
    if (!comments)
        return Result<CommentPage>::failure(comments.error);
    page.comments = *comments.value;
    return Result<CommentPage>::success(std::move(page));
}

} // namespace

Result<CommentPage> parseHotComments(const QJsonObject &root)
{
    return parseV2Page(root, HotLimit);
}

Result<CommentPage> parseCommentPage(const QJsonObject &root)
{
    return parseV2Page(root, -1);
}

Result<CommentReplyPage> parseCommentReplies(const QJsonObject &root)
{
    const QJsonValue dataValue = root.value(QStringLiteral("data"));
    if (!dataValue.isUndefined() && !dataValue.isNull() && !dataValue.isObject()) {
        return Result<CommentReplyPage>::failure(endpointError(
            ErrorKind::Parse, RepliesPath,
            QStringLiteral("data: expected object"), QStringLiteral("data")));
    }
    const QJsonObject data = dataValue.toObject();

    const auto nested = commentsArray(data, QStringLiteral("data"), RepliesPath);
    if (!nested)
        return Result<CommentReplyPage>::failure(nested.error);

    QJsonArray source = *nested.value;
    QString sourcePath = QStringLiteral("data.comments");
    if (source.isEmpty()) {
        // Older floor responses carry comments at the root and no data.comments.
        const auto fallback = commentsArray(root, QString(), RepliesPath);
        if (!fallback)
            return Result<CommentReplyPage>::failure(fallback.error);
        source = *fallback.value;
        sourcePath = QStringLiteral("comments");
    }

    JsonReader reader(root);
    const bool more = reader.boolean("more");
    if (!reader.ok())
        return parseFailure<CommentReplyPage>(RepliesPath, reader, QStringLiteral("more"));

    const auto comments = commentsFrom(source, sourcePath, RepliesPath, -1);
    if (!comments)
        return Result<CommentReplyPage>::failure(comments.error);
    CommentReplyPage page;
    page.comments = *comments.value;
    page.more = more;
    return Result<CommentReplyPage>::success(std::move(page));
}

QString commentThreadId(CommentThreadKind kind, qint64 ownerId)
{
    const auto prefix = kind == CommentThreadKind::Playlist ? "A_PL_0_" : "R_SO_4_";
    return QString::fromLatin1(prefix) + QString::number(ownerId);
}

void setCommentLiked(NcmClient &client, CommentThreadKind kind, qint64 ownerId,
                     qint64 commentId, bool liked, QObject *context,
                     CommentLikeCallback callback)
{
    if (!callback)
        return;
    const QString endpoint = QString::fromLatin1(liked ? LikePath : UnlikePath);
    if (ownerId <= 0 || commentId <= 0) {
        callback(Result<CommentLikeState>::failure(endpointError(
            ErrorKind::InvalidRequest, endpoint,
            QStringLiteral("ownerId and commentId must be positive"))));
        return;
    }
    const QJsonObject payload{
        {"threadId", commentThreadId(kind, ownerId)},
        {"commentId", QString::number(commentId)},
    };
    postWeapiJson(client, context, endpoint, payload, true,
                  [callback = std::move(callback), commentId, liked]
                  (Result<QJsonObject> root) {
        if (!root) {
            callback(Result<CommentLikeState>::failure(root.error));
            return;
        }
        callback(Result<CommentLikeState>::success(CommentLikeState{commentId, liked}));
    });
}

} // namespace ncm::sdk
