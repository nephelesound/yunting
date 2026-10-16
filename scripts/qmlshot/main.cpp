// 离屏截图：加载一个 QML 窗口，等几帧后用同步的 grabWindow() 存成 PNG。
// 用法: qmlshot <main.qml> <out.png> [宽 高] [-- 根对象属性 key=value ...]
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

int main(int argc, char *argv[])
{
    // 本机的 Qt 日志配置会吞掉 QML 警告；截图时必须看到它们
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
        fprintf(stderr, "%s\n", qPrintable(msg));
    });
    QGuiApplication app(argc, argv);
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    const QStringList args = app.arguments();
    if (args.size() < 3) {
        qCritical("usage: qmlshot <main.qml> <out.png> [w h] [-- key=value ...]");
        return 2;
    }
    const QString qmlFile = args[1];
    const QString outFile = args[2];
    const int sep = args.indexOf(QStringLiteral("--"));
    const QStringList positional = args.mid(3, sep < 0 ? -1 : sep - 3);
    const QStringList props = sep < 0 ? QStringList() : args.mid(sep + 1);

    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(qmlFile));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        qCritical("root object is not a window");
        return 1;
    }
    if (positional.size() >= 2)
        window->resize(positional[0].toInt(), positional[1].toInt());
    for (const QString &kv : props) {
        const int eq = kv.indexOf(u'=');
        window->setProperty(kv.left(eq).toUtf8().constData(), kv.mid(eq + 1));
    }
    window->show();

    // 给异步图片和布局一点时间
    const int delay = qEnvironmentVariableIntValue("QMLSHOT_DELAY_MS");
    QTimer::singleShot(delay > 0 ? delay : 2500, &app, [&] {
        const QImage img = window->grabWindow();
        if (img.isNull() || !img.save(outFile)) {
            qCritical("grab failed");
            app.exit(1);
            return;
        }
        app.exit(0);
    });
    return app.exec();
}
