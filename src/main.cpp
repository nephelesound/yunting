// 应用入口：定义应用身份，然后把命令行与界面启动交给 Cli::run（src/cli.cpp）。
// QML 编进可执行文件的 Qt 资源（qrc:/qt/qml/org/nephelesound/yunting/），资源清单见 CMakeLists.txt。
#include "cli.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // 应用的名字只在这里定义，其余地方都从 QCoreApplication / QGuiApplication 读：
    //   applicationName：数据与缓存目录、MPRIS 总线名、钥匙串服务名（不设组织名，目录不多套一层）
    //   desktopFileName：.desktop 文件名、托盘 Id、图标名
    app.setApplicationName(QStringLiteral("yunting"));
    app.setApplicationDisplayName(QStringLiteral("云听"));
    app.setDesktopFileName(QStringLiteral("io.github.nephelesound.Yunting"));
    // 任务栏/窗口图标：装好后走主题；从源码运行时用内嵌 SVG，避免显示通用占位图标
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("io.github.nephelesound.Yunting"));
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral(":/io.github.nephelesound.Yunting.svg"));
    app.setWindowIcon(appIcon);

    return Cli::run(app);
}
