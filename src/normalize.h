#pragma once
// scripts/snapshot.py 归一化函数的 C++ 移植。形状的唯一定义是 snapshot.py
//（track / sized / playlist_card / search_* / cloud_track / LRC 解析），这里逐个对应，
// 由 scripts/check_contract.py 保证不漂移。字段缺省规则也与 Python 的 `x or ""` 一致：
// null、缺失与空串都归到同一空值。
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// 可播性由 privilege 决定（账号相关：pl>0 可播，st<0 下架，cs 云盘，fee/payed 说明被什么挡住）；
// 只有歌曲没带 privilege 时才退回 fee。结果写在 playable / blockedBy（""、"vip"、"paid"、"unavailable"）。
QVariantMap ncmTrack(const QJsonObject &s, const QString &reason = QString());
QVariantMap ncmTrack(const QJsonObject &s, const QJsonObject &privilege,
                     const QString &reason = QString());
// 平行 privileges 数组按 id 建索引；接口常把 privileges 与 songs 分开发
QHash<double, QJsonObject> ncmPrivilegeIndex(const QJsonArray &privileges);
QVariantMap ncmCloudTrack(const QJsonObject &item);
QVariantMap ncmPlaylistCard(const QJsonObject &p, const char *coverKey);
QVariantMap ncmSearchArtist(const QJsonObject &a);
QVariantMap ncmSearchPlaylist(const QJsonObject &p);
QVariantMap ncmSearchAlbum(const QJsonObject &a);
QVariantMap ncmSearchMv(const QJsonObject &m);
QVariantList ncmTracks(const QJsonArray &items, const QJsonArray &privileges = QJsonArray());
// 热门评论：v2 接口 data.comments 的取数形状（snapshot.py 的 hot_comments）
QVariantMap ncmHotComments(const QJsonObject &resp, double songId);
// 单条评论：热评、分页、楼中楼共用同一套字段，避免三个地方各写一份、字段漂移。
// replyCount 来自 showFloorComment.replyCount，缺失为 0。
QVariantMap ncmComment(const QJsonObject &c);
// 账户与用户主页共用的 profile 映射：avatar/background 尺寸由调用方决定
QVariantMap ncmProfile(const QJsonObject &p, int avatarPx, int backgroundPx);
QVariantList ncmParseLrc(const QString &lrc, const QString &tlyric);
// 逐字歌词（YRC）：形状与 ncmParseLrc 相同，多一个 words 表；行时间与译文同样取两位数
QVariantList ncmParseYrc(const QString &yrc, const QString &tlyric);
// ncmParseLrc 的反向：把 {t, text, tr} 写回 LRC；有译文时同一时间戳再写一行（通行的双语 LRC 写法，
// MPRIS 的 xesam:asText 交给歌词组件时用）
QString ncmLyricsToLrc(const QVariantList &lines);
QString ncmSized(const QJsonValue &url, int px);
QString ncmFirstLine(const QString &text);
