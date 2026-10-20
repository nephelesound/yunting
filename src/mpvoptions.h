#pragma once
// 三个 mpv 实例（放歌 AudioEngine、MV 与动态封面 MvVideo、评论语音 VoicePlayer）共用的选项；
// 只出声还是要画面、缓存多大，各自另设。MpvQt 在 mpv_initialize 之后才交出句柄，所以都按运行时属性设：
// 初始化时 libmpv 已经加载了内置 Lua 脚本，load-* 会在运行时把它们停下（每个脚本一个 Lua 虚拟机和
// 一个线程，我们一个不用）；load-scripts 只管用户脚本目录，初始化之后再设已无作用，所以不写。
#include "clientidentity.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QVariant>

inline QList<QPair<QString, QVariant>> sharedMpvOptions()
{
    QList<QPair<QString, QVariant>> options{
        {QStringLiteral("terminal"), false},
        {QStringLiteral("audio-display"), QStringLiteral("no")},
        {QStringLiteral("ytdl"), QStringLiteral("no")},
        {QStringLiteral("load-stats-overlay"), false},
        {QStringLiteral("load-console"), false},
        {QStringLiteral("load-auto-profiles"), QStringLiteral("no")},
        {QStringLiteral("load-select"), false},
        {QStringLiteral("load-positioning"), false},
        {QStringLiteral("load-commands"), false},
        {QStringLiteral("load-context-menu"), false},
        {QStringLiteral("cache"), QStringLiteral("yes")},          // 网络流
        {QStringLiteral("audio-device"), QStringLiteral("auto")},  // 跟随 PipeWire/PulseAudio 的默认设备
        {QStringLiteral("user-agent"), ncm::client::userAgent()},
    };
    // 测试与打包构建（clean chroot）没有声卡：只认显式设置的变量，正常启动永远不会走到这里
    if (qEnvironmentVariableIsSet("YUNTING_TEST_AO_NULL"))
        options.append({QStringLiteral("ao"), QStringLiteral("null")});
    return options;
}
