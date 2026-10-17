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
QVariantList ncmTracks(const QJsonArray &items);
// 热门评论：data.comments 的取数形状（snapshot.py 的 hot_comments）
QVariantMap ncmHotComments(const QJsonObject &resp, double songId);
QVariantList ncmParseLrc(const QString &lrc);
QVariantList ncmParseLrc(const QString &lrc, const QString &tlyric);
QString ncmSized(const QJsonValue &url, int px);
QString ncmFirstLine(const QString &text);
