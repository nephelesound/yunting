#include "store_resource.h"

namespace ncm {

QString resourceKind(int resourceType)
{
    // 网易云评论线程的 resourceType（参考 util/config.json 的 resourceTypeMap 前缀）：
    // 0 歌曲 R_SO_4_、1 MV R_MV_5_、2 歌单 A_PL_0_、3 专辑 R_AL_3_、
    // 4 电台 A_DJ_1_、5 视频 R_VI_62_、6 动态 A_EV_2_、7 节目 A_DR_14_。
    // QML 没有电台/动态这两种详情页，归 other；未知数字同样归 other。
    switch (resourceType) {
    case 0:
        return QStringLiteral("song");
    case 1:
        return QStringLiteral("mv");
    case 2:
        return QStringLiteral("playlist");
    case 3:
        return QStringLiteral("album");
    case 5:
        return QStringLiteral("video");
    case 7:
        return QStringLiteral("program");
    default:
        return QStringLiteral("other");
    }
}

QString resourceKind(const QString &sdkKind)
{
    // 通知 SDK 的 ForwardNotification::resourceKind 是它从 JSON 里认出的对象名，
    // 与上面的数字无关；event（动态本体）和空值都落到 other。
    if (sdkKind == QLatin1String("song"))
        return QStringLiteral("song");
    if (sdkKind == QLatin1String("playlist"))
        return QStringLiteral("playlist");
    if (sdkKind == QLatin1String("album"))
        return QStringLiteral("album");
    if (sdkKind == QLatin1String("program"))
        return QStringLiteral("program");
    if (sdkKind == QLatin1String("video"))
        return QStringLiteral("video");
    return QStringLiteral("other");
}

QVariantMap resourceMap(const QString &kind, qint64 id, const QString &vid,
                        const QString &name, const QString &cover,
                        const QString &artist)
{
    const bool video = kind == QLatin1String("video");
    QVariantMap out{
        {"kind", kind},
        {"id", video ? 0.0 : static_cast<double>(id)},
        {"name", name},
        {"cover", cover},
        {"artist", artist},
    };
    if (video)
        out.insert(QStringLiteral("vid"), vid);
    return out;
}

} // namespace ncm
