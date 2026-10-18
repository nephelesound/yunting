#pragma once
// NetEase 客户端的中心身份。所有 /weapi 写请求共用这一份，别在各调用点手写 os/appver。
//
// 必须是完整的官方客户端版本号（四段）。只写 3.1.17 时评论点赞会被服务端按
// 「设备存在安全风险」拒绝（code 250）；参考：
//   docs/reference/splayer-next/electron/main/apis/netease/core/config.ts
//   docs/reference/ncm-api-enhanced/util/request.js
// 这里选 Linux 桌面客户端的官方组合，与本应用实际平台一致；不自动轮换设备身份：
// 设备风控是服务端状态，轮换只会制造不稳定的新设备，不是客户端该做的规避。
// 应用层身份统一后，TLS/HTTP2 指纹仍由 Qt/OpenSSL 决定，不能用 Cookie 伪装成另一个客户端栈。
#include <QString>

namespace ncm::client {

struct Profile {
    QString os;
    QString appver;
    QString osver;
    QString channel;
};

inline const Profile &linuxDesktop()
{
    static const Profile profile{
        QStringLiteral("linux"),
        QStringLiteral("1.2.1.0428"),
        QStringLiteral("Deepin 20.9"),
        QStringLiteral("netease"),
    };
    return profile;
}

// 写接口 Cookie 尾部的客户端上下文；不要在这里放账号 cookie
inline QString cookieContext()
{
    const Profile &p = linuxDesktop();
    return QStringLiteral("os=%1; appver=%2; osver=%3; channel=%4")
        .arg(p.os, p.appver, p.osver, p.channel);
}

// The download entitlement endpoint rejects the Linux desktop context with
// code -120 even for an account that can play/download the same song. The PC
// context returns the signed URL. Use this only for that endpoint; playback,
// comments and writes keep the Linux context.
inline QString downloadCookieContext()
{
    return QStringLiteral("os=pc; appver=3.1.17.204416; "
                          "osver=Microsoft-Windows-10-Professional-build-19045-64bit; "
                          "channel=netease");
}

inline QString userAgent()
{
    return QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) Chrome/140.0");
}

} // namespace ncm::client
