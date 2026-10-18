#pragma once
// User social SDK — typed read models for follows/followeds, account level and
// subscription counts.
//
// Raw endpoints (all GET; MUSIC_U is attached by the Store for personal reads):
//   api/user/getfollows/<uid>     root.follow[] + root.more
//   api/user/getfolloweds/<uid>   root.followeds[] + root.more
//   api/user/level                root.data{level,nowPlayCount,nextPlayCount,
//                                            nowLoginCount,nextLoginCount,progress,info}
//   api/subcount                  flat counters
//
// Absent or null lists normalize to an empty list and absent `more` to false.
// Every present user row must be an object; missing user fields keep their
// defaults, explicit wrong types are Parse errors. An absent, null, empty or
// non-object `data` on /api/user/level is a Parse error (the endpoint has no
// usable level without it).
//
// URLs stay raw; the Store boundary applies the legacy avatar sizing when it
// converts to the QML-facing map.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace ncm::sdk {

// One row of root.follow[] / root.followeds[].
struct SocialUser {
    qint64 id = 0;          // userId
    QString name;           // nickname
    QString avatar;         // avatarUrl (raw URL)
    QString signature;      // signature
    int gender = 0;         // gender
    bool followed = false;  // followed
    bool mutual = false;    // mutual
    int vipType = 0;        // vipType
};

struct FollowPage {
    QList<SocialUser> items; // root.follow[] or root.followeds[], in order
    bool more = false;       // root.more
};

// data of /api/user/level.
struct UserLevel {
    int level = 0;           // data.level
    int nowPlayCount = 0;    // data.nowPlayCount
    int nextPlayCount = 0;   // data.nextPlayCount
    int nowLoginCount = 0;   // data.nowLoginCount
    int nextLoginCount = 0;  // data.nextLoginCount
    double progress = 0.0;   // data.progress
    QString info;            // data.info
};

// Flat response of /api/subcount.
struct UserSubcount {
    int artistCount = 0;          // artistCount
    int createdPlaylistCount = 0; // createdPlaylistCount
    int subPlaylistCount = 0;     // subPlaylistCount
    int mvCount = 0;              // mvCount
    int djRadioCount = 0;         // djRadioCount
};

// Pure parsers, exposed for tests and callers that already hold a response.
Result<FollowPage> parseUserFollows(const QJsonObject &root);
Result<FollowPage> parseUserFolloweds(const QJsonObject &root);
Result<UserLevel> parseUserLevel(const QJsonObject &root);
Result<UserSubcount> parseUserSubcount(const QJsonObject &root);

} // namespace ncm::sdk
