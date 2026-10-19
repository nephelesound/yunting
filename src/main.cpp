// 应用入口：定义应用身份，然后把命令行与界面启动交给 Cli::run（src/cli.cpp）。
// QML 编进可执行文件的 Qt 资源（qrc:/qt/qml/org/nephelesound/yunting/），资源清单见 CMakeLists.txt。
#include "cli.h"
#include "colortone.h"
#include "link_parser.h"

#include <KAboutData>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QIcon>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <cstdio>
#include <optional>

namespace {

// 桌面、浏览器、门户拉起云听都只带一个链接或什么也不带；带选项的是命令行用法，照常起
bool plainLaunch(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-')
            return false;
    }
    return true;
}

// 浏览器点「在客户端打开」时，链接经 xdg-desktop-portal 拉起云听；门户若比桌面先启动，
// 它的环境里没有 WAYLAND_DISPLAY / DISPLAY，建 QApplication 就会崩。离屏（offscreen / minimal）是有意为之，不算
bool linkWithoutDisplay(int argc, char *argv[])
{
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (platform.startsWith("offscreen") || platform.startsWith("minimal")
        || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") || !qEnvironmentVariableIsEmpty("DISPLAY"))
        return false;
    for (int i = 1; i < argc; ++i) {
        if (findLink(QString::fromLocal8Bit(argv[i])).found())
            return true;
    }
    return false;
}

// 已有云听在运行：把参数交给它（KDBusService 的 CommandLine，与第二次启动走同一条路），连同拉起这个进程的
// 激活令牌，那边才能把窗口提到前面（Wayland 不许没有令牌的窗口抢焦点）。没在运行返回空。
// 要在钥匙串、mpv 这些初始化之前问：第二次启动不该先等这些
std::optional<int> forwardToRunningInstance()
{
    const QString service = QStringLiteral("io.github.nephelesound.Yunting");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->isServiceRegistered(service))
        return std::nullopt;
    QVariantMap platformData;
    if (const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN"); !token.isEmpty())
        platformData.insert(QStringLiteral("activation-token"), token);
    if (const QString startup = qEnvironmentVariable("DESKTOP_STARTUP_ID"); !startup.isEmpty())
        platformData.insert(QStringLiteral("desktop-startup-id"), startup);
    QDBusMessage call = QDBusMessage::createMethodCall(
        service, QStringLiteral("/io/github/nephelesound/Yunting"),
        QStringLiteral("org.kde.KDBusService"), QStringLiteral("CommandLine"));
    call << QCoreApplication::arguments() << QDir::currentPath() << platformData;
    // 那边卡住时不能陪着等（D-Bus 默认 25 秒）：3 秒没回话就当它不在，自己照常起
    const QDBusMessage reply = bus.call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        std::fprintf(stderr, "已在运行的云听没有回应：%s\n", qPrintable(reply.errorMessage()));
        return std::nullopt;
    }
    return reply.arguments().value(0).toInt();
}

} // namespace

int main(int argc, char *argv[])
{
    if (linkWithoutDisplay(argc, argv)) {
        QCoreApplication app(argc, argv);
        if (const auto rc = forwardToRunningInstance())
            return *rc;
        std::fprintf(stderr, "没有图形环境，链接没能交给正在运行的云听，没法打开\n");
        return 1;
    }
    // 在 QApplication 之前判断：它会把 -platform、-style 这些 Qt 自己的选项从 argv 里拿掉
    const bool plain = plainLaunch(argc, argv);

    // 桌面歌词与迷你窗是透明的顶层窗口；没有 alpha buffer 时合成器会把透明区域画成黑色。
    // 必须在创建任何 QQuickWindow 之前设置。
    QQuickWindow::setDefaultAlphaBuffer(true);
    // MV 用 MpvQt 的 MpvAbstractItem 渲染，它只支持 OpenGL；必须在创建第一个窗口前定下
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication app(argc, argv);
    if (plain) {
        if (const auto rc = forwardToRunningInstance())
            return *rc;
    }
    // 应用的身份只在这里定义（KAboutData 同时写进 QCoreApplication / QGuiApplication，「关于」页也读它），
    // 其余地方都从 QCoreApplication / QGuiApplication 读：
    //   componentName（applicationName）：数据与缓存目录、MPRIS 总线名、钥匙串服务名（不设组织名，目录不多套一层）
    //   desktopFileName：.desktop 文件名、托盘 Id、图标名
    KAboutData about(QStringLiteral("yunting"), QStringLiteral("云听"), QStringLiteral(YUNTING_VERSION),
                     QStringLiteral("为 KDE Plasma 打造的网易云音乐客户端"),
                     KAboutLicense::GPL_V3, QStringLiteral("© 2026 sudoskys"));
    about.setLicense(KAboutLicense::GPL_V3, KAboutLicense::OrLaterVersions);
    about.setOtherText(QStringLiteral("「网易云音乐」名称与商标，以及通过其接口获取的内容，归各自权利人所有。"));
    about.setHomepage(QStringLiteral("https://github.com/nephelesound/yunting"));
    about.setBugAddress(QByteArrayLiteral("https://github.com/nephelesound/yunting/issues"));
    about.setOrganizationDomain(QByteArrayLiteral("nephelesound.github.io"));   // 默认是 kde.org
    about.setDesktopFileName(QStringLiteral("io.github.nephelesound.Yunting"));
    about.addAuthor(QStringLiteral("Nephele Sound"), QStringLiteral("维护"), QString(),
                    QStringLiteral("https://github.com/nephelesound"));
    KAboutData::setApplicationData(about);
    // 系统配色是中性灰时给本应用染上强调色的色调（要在任何窗口之前，缓存目录要用到上面的应用名）
    ColorTone::apply();
    // 任务栏/窗口图标：装好后走主题；从源码运行时用内嵌 SVG，避免显示通用占位图标
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("io.github.nephelesound.Yunting"));
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral(":/io.github.nephelesound.Yunting.svg"));
    app.setWindowIcon(appIcon);

    return Cli::run(app);
}
