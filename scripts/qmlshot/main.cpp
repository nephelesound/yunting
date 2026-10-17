// 离屏截图：加载一个 QML 窗口，等几帧后用同步的 grabWindow() 存成 PNG。
// 用法: qmlshot <main.qml> <out.png> [宽 高] [-- 根对象属性 key=value ...]
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

namespace {
// prototype/ 下的 QML 消息一律当错误：截图宁可不拍，也不留一张错的。
// 唯一放过的是 Kirigami 页栈的固有提示：pageStack 推入由 Component 创建的页时，
// 在页被放进页栈之前就会报这一句，与我们的代码无关
bool isHarmlessWarning(const QString &description)
{
    return description.contains(QLatin1String("Created graphical object was not placed in the graphics scene."));
}
} // namespace

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
    // 只统计 qmlFile 所在目录（原型 prototype/）的错误；Kirigami 内部 warning 不算截图失败
    const QString prototypeDir = QFileInfo(qmlFile).absolutePath();
    QList<QQmlError> ourErrors;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [&ourErrors, prototypeDir](const QList<QQmlError> &warnings) {
        for (const QQmlError &error : warnings) {
            if (!error.url().toLocalFile().startsWith(prototypeDir + u'/')
                || isHarmlessWarning(error.description()))
                continue;
            ourErrors.append(error);
        }
    });
    engine.load(QUrl::fromLocalFile(qmlFile));
    // 错误文案已经由上面的 message handler 打印；这里只决定退出码
    if (engine.rootObjects().isEmpty() || !ourErrors.isEmpty())
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
        if (eq <= 0) {
            qCritical("invalid property assignment (expected key=value): %s", qPrintable(kv));
            return 2;
        }
        const QString key = kv.left(eq);
        const QByteArray name = key.toUtf8();
        const int index = window->metaObject()->indexOfProperty(name.constData());
        if (index < 0) {
            qCritical("unknown property: %s", qPrintable(key));
            return 2;
        }
        if (!window->metaObject()->property(index).isWritable()
            || !window->setProperty(name.constData(), kv.mid(eq + 1))) {
            qCritical("cannot set property: %s", qPrintable(key));
            return 2;
        }
    }
    window->show();

    // 给异步图片和布局一点时间
    const int delay = qEnvironmentVariableIntValue("QMLSHOT_DELAY_MS");
    QTimer::singleShot(delay > 0 ? delay : 2500, &app, [&] {
        // 设属性、加载数据之后才冒出来的错误（绑定求值、类型不符）同样算失败
        if (!ourErrors.isEmpty()) {
            app.exit(1);
            return;
        }
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
