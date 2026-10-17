// 应用入口：装配 NcmClient / Store / AudioEngine / Session / Mpris，注入 prototype/main.qml。
// QML 仍来自源码树（原型阶段，界面与后端并行演进），由 PROTOTYPE_DIR 编译定义给出路径。
#include "audioengine.h"
#include "mpris.h"
#include "ncmclient.h"
#include "session.h"
#include "store.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ncm-player"));
    app.setOrganizationName(QStringLiteral("ncm-player"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NetEase Cloud Music client"));
    parser.addHelpOption();
    QCommandLineOption dumpOption("dump-store", "Fetch initial data, write {data, personal} JSON and exit",
                                  "file");
    QCommandLineOption smokeOption("smoke-audio", "Resolve a song's play URL and play it for a few seconds",
                                   "songId");
    QCommandLineOption importOption("import-gtk4-cookies",
                                    "Import MUSIC_U/__csrf/NMTID from netease-cloud-music-gtk4 once");
    QCommandLineOption verboseOption("verbose", "Print Qt/QML warnings to stderr (this machine's logging config may hide them)");
    QCommandLineOption shotOption("screenshot", "Load the UI, wait for live data, save a PNG of the window and exit", "file");
    parser.addOption(dumpOption);
    parser.addOption(smokeOption);
    parser.addOption(importOption);
    parser.addOption(verboseOption);
    parser.addOption(shotOption);
    parser.process(app);

    if (parser.isSet(verboseOption) || parser.isSet(shotOption)) {
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
            if (type != QtDebugMsg && type != QtInfoMsg)
                std::fprintf(stderr, "%s\n", qPrintable(msg));
        });
    }

    NcmClient client;

    if (parser.isSet(importOption)) {
        const QString path = client.jarPath();
        QDir().mkpath(QFileInfo(path).path());
        const int n = NcmClient::importGtk4Cookies(path);
        if (n < 0) {
            std::fprintf(stderr, "import failed\n");
            return 1;
        }
        std::printf("imported %d cookies\n", n);
        return 0;
    }

    Store store(&client);
    AudioEngine audio(&client);
    Session session(&client, &store);
    Mpris mpris(&audio);

    if (parser.isSet(dumpOption)) {
        store.waitForInitial();
        const QJsonObject root{{"data", QJsonObject::fromVariantMap(store.data())},
                               {"personal", QJsonObject::fromVariantMap(store.personal())}};
        QFile out(parser.value(dumpOption));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(parser.value(dumpOption)));
            return 1;
        }
        out.write(QJsonDocument(root).toJson());
        return 0;
    }

    if (parser.isSet(smokeOption)) {
        // 无窗口：事件循环跑在 QCoreApplication 节奏上，AudioEngine 自己打印状态
        const int rc = audio.smokeTest(parser.value(smokeOption).toDouble(), 6);
        if (rc != 0)
            return rc;
        return app.exec();
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    store.refresh();   // 首屏数据异步到达，QML 通过 data / personal 的 NOTIFY 自动更新
    QQmlApplicationEngine engine;
    // main.qml 用 root.backend.{store,audio,session,mpris} 取后端；QVariantMap 的 key 就是属性名
    QVariantMap backend{{"store", QVariant::fromValue<QObject *>(&store)},
                        {"audio", QVariant::fromValue<QObject *>(&audio)},
                        {"session", QVariant::fromValue<QObject *>(&session)},
                        {"mpris", QVariant::fromValue<QObject *>(&mpris)}};
    engine.setInitialProperties({{"backend", backend}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl::fromLocalFile(QStringLiteral(PROTOTYPE_DIR) + QStringLiteral("/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(shotOption)) {
        // 等首屏数据与封面到齐再截（真实网络，给足时间）；同步 grabWindow，不依赖 grabToImage 回调
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = parser.value(shotOption);
        QTimer::singleShot(8000, &app, [window, file] {
            const bool saved = window && window->grabWindow().save(file);
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
