#pragma once
// scripts/snapshot.py 归一化函数的 C++ 移植。形状的唯一定义是 snapshot.py
//（track / sized / playlist_card / search_* / cloud_track / LRC 解析），这里逐个对应，
// 由 scripts/check_contract.py 保证不漂移。字段缺省规则也与 Python 的 `x or ""` 一致：
// null、缺失与空串都归到同一空值。
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

QVariantMap ncmTrack(const QJsonObject &s, const QString &reason = QString());
QVariantMap ncmCloudTrack(const QJsonObject &item);
QVariantMap ncmPlaylistCard(const QJsonObject &p, const char *coverKey);
QVariantMap ncmSearchArtist(const QJsonObject &a);
QVariantMap ncmSearchPlaylist(const QJsonObject &p);
QVariantMap ncmSearchAlbum(const QJsonObject &a);
QVariantMap ncmSearchMv(const QJsonObject &m);
QVariantList ncmTracks(const QJsonArray &items);
// 热门评论：data.comments 的取数形状（snapshot.py 的 hot_comments）
QVariantMap ncmHotComments(const QJsonObject &resp, double songId);
// 单条评论：热评、分页、楼中楼共用同一套字段，避免三个地方各写一份、字段漂移
QVariantMap ncmComment(const QJsonObject &c);
// 账户与用户主页共用的 profile 映射：avatar/background 尺寸由调用方决定
QVariantMap ncmProfile(const QJsonObject &p, int avatarPx, int backgroundPx);
QVariantList ncmParseLrc(const QString &lrc, const QString &tlyric);
// ncmParseLrc 的反向：把 {t, text, tr} 写回 LRC；有译文时同一时间戳再写一行（通行的双语 LRC 写法，
// MPRIS 的 xesam:asText 交给歌词组件时用）
QString ncmLyricsToLrc(const QVariantList &lines);
QString ncmSized(const QJsonValue &url, int px);
QString ncmFirstLine(const QString &text);
