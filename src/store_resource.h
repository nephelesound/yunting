#pragma once
// QML 面向的 Resource 形状：动态、系统通知、@我 共用同一份 kind 映射与字段集合。
#include <QString>
#include <QVariantMap>

namespace ncm {

// 网易云评论线程里的 resourceType 数字 → QML kind（未知一律 other）。
QString resourceKind(int resourceType);
// 通知 SDK 已归一化的 resourceKind 字符串 → QML kind（event/空 → other）。
QString resourceKind(const QString &sdkKind);

// { kind, id, name, cover, artist }。kind=video 时 id 为 0，并额外带 vid（字符串）；
// 其余 kind 不带 vid。cover/artist 为空串表示上游没有给。
QVariantMap resourceMap(const QString &kind, qint64 id, const QString &vid,
                        const QString &name, const QString &cover,
                        const QString &artist);

} // namespace ncm
