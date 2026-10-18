#pragma once
// scripts/snapshot.py 归一化函数的 C++ 移植。形状的唯一定义是 snapshot.py
//（sized / search_* / cloud_track / LRC 解析），这里逐个对应，由
// scripts/check_contract.py 保证不漂移。曲目与歌单卡片的形状已迁到 sdk/track.h。
// 字段缺省规则也与 Python 的 `x or ""` 一致：null、缺失与空串都归到同一空值。
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace ncm::sdk { struct Comment; }

// 歌曲/歌单卡片的 typed 边界在 sdk/track.h；这里只留云盘条目的兼容 map。
QVariantMap ncmCloudTrack(const QJsonObject &item);
QVariantMap ncmSearchArtist(const QJsonObject &a);
QVariantMap ncmSearchPlaylist(const QJsonObject &p);
QVariantMap ncmSearchAlbum(const QJsonObject &a);
QVariantMap ncmSearchMv(const QJsonObject &m);
// 评论的 QML 兼容形状：SDK 的 typed Comment → Store 现有 map（snapshot.py 的 hot_comments）。
// 这是热评/分页/楼中楼共用的唯一边界转换；avatar 在这里按 96 方图缩放。
QVariantMap commentToVariantMap(const ncm::sdk::Comment &c);
// 账户与用户主页共用的 profile 映射：avatar/background 尺寸由调用方决定
QVariantMap ncmProfile(const QJsonObject &p, int avatarPx, int backgroundPx);
QVariantList ncmParseLrc(const QString &lrc, const QString &tlyric);
// 逐字歌词（YRC）：形状与 ncmParseLrc 相同，多一个 words 表；行时间与译文同样取两位数
QVariantList ncmParseYrc(const QString &yrc, const QString &tlyric);
// ncmParseLrc 的反向：把 {t, text, tr} 写回 LRC；有译文时同一时间戳再写一行（通行的双语 LRC 写法，
// MPRIS 的 xesam:asText 交给歌词组件时用）
QString ncmLyricsToLrc(const QVariantList &lines);
// 播放地址响应（song/enhance/player/url/v1 的 data[0]）里服务器实际给了哪一档与码率：
// 可能比请求的低（版权、会员）；服务器没回 level 时用请求档位
struct NcmPlayedLevel {
    QString level;
    int bitrate = 320000;
};
NcmPlayedLevel ncmPlayedLevel(const QJsonObject &urlItem, const QString &requestedLevel);
QString ncmSized(const QJsonValue &url, int px);
QString ncmFirstLine(const QString &text);
