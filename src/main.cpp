// 应用入口：装配 NcmClient / Store / AudioEngine / Session / Mpris，注入 prototype/main.qml。
// QML 仍来自源码树（原型阶段，界面与后端并行演进），由 PROTOTYPE_DIR 编译定义给出路径。
#include "audioengine.h"
#include "imagecache.h"
#include "mpris.h"
#include "ncmclient.h"
#include "session.h"
#include "store.h"
#include "tray.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
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
    // 应用的名字只在这里定义，其余地方都从 QCoreApplication / QGuiApplication 读：
    //   applicationName：数据与缓存目录、MPRIS 总线名、钥匙串服务名（不设组织名，目录不多套一层）
    //   desktopFileName：.desktop 文件名、托盘 Id、图标名
    app.setApplicationName(QStringLiteral("yunting"));
    app.setApplicationDisplayName(QStringLiteral("云听"));
    app.setDesktopFileName(QStringLiteral("io.github.sudoskys.Yunting"));

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
        // 警告以上一律打印；--verbose 时再打开本应用的 ncm.* 调试分类（网络请求路径等）
        if (parser.isSet(verboseOption))
            QLoggingCategory::setFilterRules(QStringLiteral("yunting.*.debug=true"));
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
            const bool ours = ctx.category && qstrncmp(ctx.category, "yunting.", 8) == 0;
            if (ours || (type != QtDebugMsg && type != QtInfoMsg))
                std::fprintf(stderr, "%s\n", qPrintable(msg));
        });
    }

    NcmClient client;

    // QtKeychain 的读写是异步的：等 jar 从存储读出（钥匙串或后备文件）再继续。
    // 兜底 10 秒防钥匙串守护进程挂死时永远起不来。
    {
        QEventLoop readyLoop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&client, &NcmClient::ready, &readyLoop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &readyLoop, &QEventLoop::quit);
        timeout.start(10 * 1000);
        readyLoop.exec();
    }

    if (parser.isSet(importOption)) {
        // 一次性导入：写进与启动同一个存储（钥匙串，或后备文件）。异步链在内部跑完。
        const int n = client.importGtk4();
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
        // 按需数据走真实的 request() 路径（经 QueryCache），契约也就覆盖了它：
        // 搜索一个词、取热歌榜第一首的歌词与热评，等三者落地（兜底 15 秒）
        const QString probeQuery = QStringLiteral("林俊杰");
        const QVariantList hot = store.data().value("tracks").toList();
        const double probeSong = hot.isEmpty() ? 0 : hot.first().toMap().value("id").toDouble();
        const auto landed = [&] {
            const QVariantMap d = store.data();
            return d.value("search").toMap().value("query").toString() == probeQuery
                && !d.value("search").toMap().value("songs").toList().isEmpty()
                && (probeSong == 0 || d.value("lyricsTrackId").toDouble() == probeSong)
                && (probeSong == 0 || d.value("comments").toMap().value("songId").toDouble() == probeSong);
        };
        store.request(QStringLiteral("search:") + probeQuery);
        if (probeSong != 0) {
            store.request(QStringLiteral("lyrics:%1").arg(qlonglong(probeSong)));
            store.request(QStringLiteral("comments:%1").arg(qlonglong(probeSong)));
        }
        QEventLoop loop;
        QTimer::singleShot(15 * 1000, &loop, &QEventLoop::quit);
        QObject::connect(&store, &Store::dataChanged, &loop, [&] { if (landed()) loop.quit(); });
        if (!landed())
            loop.exec();
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

    // 托盘只在有窗口的运行方式下创建（--dump-store / --smoke-audio 上面已返回，无窗口）
    Tray tray(&audio);

    store.refresh();   // 首屏数据异步到达，QML 通过 data / personal 的 NOTIFY 自动更新
    // 钥匙串超时后才读出 jar（例如在等用户解锁钱包）：先按匿名起来，读出后再按登录态刷一次
    if (!client.isReady())
        QObject::connect(&client, &NcmClient::ready, &store, &Store::refresh, Qt::SingleShotConnection);
    QQmlApplicationEngine engine;
    ImageCacheFactory imageCache;
    engine.setNetworkAccessManagerFactory(&imageCache);
    // main.qml 用 root.backend.{store,audio,session,mpris,tray} 取后端；QVariantMap 的 key 就是属性名
    QVariantMap backend{{"store", QVariant::fromValue<QObject *>(&store)},
                        {"audio", QVariant::fromValue<QObject *>(&audio)},
                        {"session", QVariant::fromValue<QObject *>(&session)},
                        {"mpris", QVariant::fromValue<QObject *>(&mpris)},
                        {"tray", QVariant::fromValue<QObject *>(&tray)}};
    engine.setInitialProperties({{"backend", backend}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl::fromLocalFile(QStringLiteral(PROTOTYPE_DIR) + QStringLiteral("/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // 窗口创建完成后关联托盘，左键从此显示/隐藏主窗口
    tray.setAssociatedWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()));

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
