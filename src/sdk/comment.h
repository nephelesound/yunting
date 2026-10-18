#pragma once
// Comment SDK — typed models for the song comment surfaces.
//
// Raw endpoints:
//   GET  api/v2/resource/comments          hot (sortType=2) and cursor pages (sortType=3)
//        data.comments[], data.totalCount, data.cursor, data.hasMore
//   POST /weapi/resource/comment/floor/get replies of one comment
//        data.comments[] (fallback root comments[]), root more
//
// `comments` is optional in both payloads: an absent or null array means the
// thread/floor has no comments yet and parses as an empty list. Every present
// comment is validated field by field by JsonReader; a wrong-typed field is a
// Parse error carrying its JSON path. The v2 response must contain a `data`
// object, while the floor endpoint also accepts the older root-level `comments`
// shape.
//
// Raw URLs stay unsized here (avatar is the raw user.avatarUrl); the Store
// boundary chooses its legacy 96px avatar size. Hot parsing keeps server order
// and caps the list at 10 items, matching the existing UI contract. `time` is
// the millisecond timestamp the cursor flow uses for the next page.
// Relative include: normalize.cpp is also compiled by targets without the SDK
// root on their include path.
#include "sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace ncm::sdk {

// One entry of a comment's beReplied[] array ("回复 @某人").
struct RepliedComment {
    qint64 id = 0;       // beReplied[].beRepliedCommentId
    QString content;     // beReplied[].content
    qint64 userId = 0;   // beReplied[].user.userId
    QString user;        // beReplied[].user.nickname
};

// One comment row shared by hot comments, cursor pages and floor replies.
struct Comment {
    QString user;                    // user.nickname
    qint64 userId = 0;               // user.userId
    QString avatar;                  // user.avatarUrl (raw URL)
    QString content;                 // content
    qint64 likes = 0;                // likedCount
    qint64 time = 0;                 // time (milliseconds; cursor value for the next page)
    qint64 id = 0;                   // commentId
    bool liked = false;              // liked (the signed-in user's state)
    qint64 parentId = 0;             // parentCommentId
    QList<RepliedComment> beReplied; // beReplied[]
    qint64 replyCount = 0;           // showFloorComment.replyCount
};

// data of api/v2/resource/comments. Hot and cursor-paged responses share this
// shape; parseHotComments caps comments at 10, parseCommentPage keeps all.
struct CommentPage {
    QList<Comment> comments; // data.comments[]
    qint64 total = 0;        // data.totalCount
    QString cursor;          // data.cursor (opaque next-page token)
    bool hasMore = false;    // data.hasMore
};

// resource/comment/floor/get. The current API nests comments under data; older
// responses put them at the root, so the parser falls back when data.comments
// is absent or empty.
struct CommentReplyPage {
    QList<Comment> comments; // data.comments[], fallback comments[]
    bool more = false;       // root more
};

Result<CommentPage> parseHotComments(const QJsonObject &root);
Result<CommentPage> parseCommentPage(const QJsonObject &root);
Result<CommentReplyPage> parseCommentReplies(const QJsonObject &root);

} // namespace ncm::sdk
