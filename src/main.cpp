// 应用入口：定义应用身份，然后把命令行与界面启动交给 Cli::run（src/cli.cpp）。
// QML 编进可执行文件的 Qt 资源（qrc:/qt/qml/org/nephelesound/yunting/），资源清单见 CMakeLists.txt。
#include "cli.h"
#include "colortone.h"

#include <KAboutData>
#include <QApplication>
#include <QIcon>
#include <QQuickWindow>
#include <QSGRendererInterface>

int main(int argc, char *argv[])
{
    // 桌面歌词与迷你窗是透明的顶层窗口；没有 alpha buffer 时合成器会把透明区域画成黑色。
    // 必须在创建任何 QQuickWindow 之前设置。
    QQuickWindow::setDefaultAlphaBuffer(true);
    // MV 用 MpvQt 的 MpvAbstractItem 渲染，它只支持 OpenGL；必须在创建第一个窗口前定下
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication app(argc, argv);
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
