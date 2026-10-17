// 应用入口：装配 NcmClient / Store / AudioEngine / Session / Mpris，注入 prototype/main.qml。
// QML 编进可执行文件的 Qt 资源（qrc:/qt/qml/org/nephelesound/yunting/），资源清单见 CMakeLists.txt。
#include "apicli.h"
#include "audioengine.h"
#include "imagecache.h"
#include "layershellhelper.h"
#include "mpris.h"
#ifdef YUNTING_QRCODE
#include "qrcodeimage.h"
#endif
#include "ncmclient.h"
#include "powerinhibitor.h"
#include "session.h"
#include "store.h"
#include "tray.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>

namespace {
// CLI 只读调试：等某个 data 槽位落地，打印 `{ <key>: value }`。缓存命中会同步触发
// dataChanged，所以先 request 再决定是否进事件循环。
int waitForStoreData(Store &store, const QString &requestKey, const QString &dataKey,
                     const std::function<bool(const QVariantMap &)> &match)
{
    QEventLoop loop;
    int rc = 2;
    bool printed = false;
    QObject::connect(&store, &Store::dataChanged, &loop, [&] {
        if (printed)
            return;
        const QVariant value = store.data().value(dataKey);
        if (!value.isValid() || value.isNull() || !match(value.toMap()))
            return;
        printed = true;
        const QJsonObject out{{dataKey, QJsonValue::fromVariant(value)}};
        std::printf("%s", QJsonDocument(out).toJson(QJsonDocument::Indented).constData());
        rc = 0;
        loop.quit();
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    store.request(requestKey);
    if (rc == 2)
        loop.exec();
    return rc;
}
} // namespace

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

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NetEase Cloud Music client"));
    parser.addHelpOption();
    QCommandLineOption dumpOption("dump-store", "Fetch initial data, write {data, personal} JSON and exit",
                                  "file");
    QCommandLineOption smokeOption("smoke-audio", "Resolve a song's play URL and play it for a few seconds",
                                   "songId");
    QCommandLineOption audioQualityOption("audio-quality",
                                          "Set playback quality: auto|standard|higher|exhigh|lossless|hires",
                                          "level");
    QCommandLineOption importOption("import-gtk4-cookies",
                                    "Import MUSIC_U/__csrf/NMTID from netease-cloud-music-gtk4 once");
    QCommandLineOption writeOption("allow-write", "Allow explicit write commands (like / playlist selftests)");
    QCommandLineOption likeOption("like", "Like/unlike a song: --like <songId> [on|off] --allow-write", "songId");
    QCommandLineOption playlistSelfTestOption("playlist-selftest",
                                              "Create, fill, rename and delete a private test playlist, then exit");
    QCommandLineOption subscribeSelfTestOption("playlist-subscribe-selftest",
                                               "Subscribe and unsubscribe one public playlist, then exit");
    QCommandLineOption detailOption("detail", "Fetch one artist/album and print JSON: --detail artist:6452",
                                    "key");
    QCommandLineOption commentsOption("comments", "Fetch one comments page: --comments <songId>[:offset]",
                                      "song");
    QCommandLineOption artistSongsOption("artist-songs",
                                         "Fetch one artist songs page: --artist-songs <artistId>[:offset]",
                                         "artist");
    QCommandLineOption mvOption("mv", "Fetch one MV (detail + play URL): --mv <mvId>", "mv");
    QCommandLineOption searchUsersOption("search-users", "Search users: --search-users <query>", "query");
    QCommandLineOption searchRadiosOption("search-radios", "Search radio stations: --search-radios <query>", "query");
    QCommandLineOption apiOption("api",
                                 "Inspect/debug backend API: --api list | --api get <path> [key=value...]",
                                 "command");
    QCommandLineOption apiPersonalOption("api-personal", "Send stored cookies with --api get");
    QCommandLineOption anonymousOption("anonymous", "Skip keychain/cookies (read-only debugging)");
    QCommandLineOption verboseOption("verbose", "Print Qt/QML warnings to stderr (this machine's logging config may hide them)");
    QCommandLineOption shotOption("screenshot", "Load the UI, wait for live data, save a PNG of the window and exit", "file");
    QCommandLineOption eqPresetOption("eq-preset",
                                      "Experimental EQ preset: flat/pop/rock/bass/vocal/live/spatial", "preset");
    QCommandLineOption soakOption("soak-audio",
                                  "Headless playback soak: cycle tracks/seek/EQ/device for N seconds",
                                  "seconds");
    QCommandLineOption scrobbleModeOption("scrobble-mode",
                                          "Listening log: legacy (web) or ncbl (PC client PLV/PLD)",
                                          "mode");
    parser.addOption(dumpOption);
    parser.addOption(smokeOption);
    parser.addOption(audioQualityOption);
    parser.addOption(importOption);
    parser.addOption(writeOption);
    parser.addOption(likeOption);
    parser.addOption(playlistSelfTestOption);
    parser.addOption(subscribeSelfTestOption);
    parser.addOption(detailOption);
    parser.addOption(commentsOption);
    parser.addOption(artistSongsOption);
    parser.addOption(mvOption);
    parser.addOption(searchUsersOption);
    parser.addOption(searchRadiosOption);
    parser.addOption(apiOption);
    parser.addOption(apiPersonalOption);
    parser.addOption(anonymousOption);
    parser.addOption(verboseOption);
    parser.addOption(shotOption);
    parser.addOption(eqPresetOption);
    parser.addOption(soakOption);
    parser.addOption(scrobbleModeOption);
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

    const bool anonymous = parser.isSet(anonymousOption);
    NcmClient client(nullptr, !anonymous);
    // 退出登录后的清凭据是异步写（钥匙串）：进程退出前最多等 2 秒把它写完，
    // 否则下一条匿名 Set-Cookie 之前进程就没了，旧 MUSIC_U 会留在钥匙串里。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &client, [&client] {
        client.flushCredentials(2000);
    });

    // QtKeychain 的读写是异步的：等 jar 从存储读出（钥匙串或后备文件）再继续。
    // 兜底 10 秒防钥匙串守护进程挂死时永远起不来。
    {
        if (!client.isReady()) {
            QEventLoop readyLoop;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&client, &NcmClient::ready, &readyLoop, &QEventLoop::quit);
            QObject::connect(&timeout, &QTimer::timeout, &readyLoop, &QEventLoop::quit);
            timeout.start(10 * 1000);
            readyLoop.exec();
        }
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
    if (parser.isSet(eqPresetOption))
        audio.applyEqPreset(parser.value(eqPresetOption));
    if (parser.isSet(scrobbleModeOption))
        audio.setScrobbleMode(parser.value(scrobbleModeOption));
    Session session(&client, &store);
    Mpris mpris(&audio);
    // 打卡成功后本地「最近播放」立即更新；服务端数据仍是权威，下次 refresh 会覆盖
    QObject::connect(&audio, &AudioEngine::scrobbled, &store, &Store::notePlayed);

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

    if (parser.isSet(apiOption)) {
        return runApiCommand(client, parser.value(apiOption), parser.positionalArguments(),
                             parser.isSet(apiPersonalOption));
    }

    if (parser.isSet(likeOption) || parser.isSet(playlistSelfTestOption)
        || parser.isSet(subscribeSelfTestOption)) {
        if (!parser.isSet(writeOption)) {
            std::fprintf(stderr, "refusing write commands without --allow-write\n");
            return 1;
        }
    }

    if (parser.isSet(likeOption)) {
        // 只用于验证 weapi 写链路：--like 108485 on / --like 108485 off
        const double songId = parser.value(likeOption).toDouble();
        const QString mode = parser.positionalArguments().value(0, QStringLiteral("on"));
        const bool liked = mode.compare(QLatin1String("off"), Qt::CaseInsensitive) != 0;
        if (songId <= 0) {
            std::fprintf(stderr, "invalid song id\n");
            return 1;
        }
        if (!client.hasMusicU()) {
            std::fprintf(stderr, "not logged in\n");
            return 1;
        }
        // 先加载首屏，trackById 才能找到 287398；也让本地歌单卡片的计数/曲目检查有意义
        store.waitForInitial();
        QEventLoop loop;
        int rc = 2;
        QObject::connect(&store, &Store::likeSucceeded, &loop, [&](double id, bool state) {
            if (id == songId) {
                rc = 0;
                std::printf("%s %lld\n", state ? "liked" : "unliked", qlonglong(songId));
                loop.quit();
            }
        });
        QObject::connect(&store, &Store::likeFailed, &loop,
                         [&](double id, bool, const QString &message) {
            if (id == songId) {
                rc = 1;
                std::fprintf(stderr, "%s\n", qPrintable(message));
                loop.quit();
            }
        });
        QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
        store.setLiked(songId, liked);
        if (rc == 2)
            loop.exec();
        return rc;
    }

    if (parser.isSet(searchUsersOption) || parser.isSet(searchRadiosOption)) {
        const bool users = parser.isSet(searchUsersOption);
        const QString q = parser.value(users ? searchUsersOption : searchRadiosOption);
        if (q.isEmpty()) {
            std::fprintf(stderr, "empty query\n");
            return 1;
        }
        const QString kind = users ? QStringLiteral("searchUsers") : QStringLiteral("searchRadios");
        const QString key = (users ? QStringLiteral("searchUsers:") : QStringLiteral("searchRadios:")) + q;
        return waitForStoreData(store, key, kind, [&](const QVariantMap &result) {
            return result.value("query").toString() == q;
        });
    }

    if (parser.isSet(mvOption)) {
        const double mvId = parser.value(mvOption).toDouble();
        if (mvId <= 0) {
            std::fprintf(stderr, "expected <mvId>\n");
            return 1;
        }
        return waitForStoreData(store, QStringLiteral("mv:%1").arg(qlonglong(mvId)),
                                QStringLiteral("mv"), [&](const QVariantMap &mv) {
            return mv.value("id").toDouble() == mvId;
        });
    }

    if (parser.isSet(artistSongsOption)) {
        const QStringList parts = parser.value(artistSongsOption).split(u':');
        const double artistId = parts.value(0).toDouble();
        const int offset = parts.value(1).toInt();
        if (artistId <= 0 || offset < 0) {
            std::fprintf(stderr, "expected <artistId>[:offset]\n");
            return 1;
        }
        return waitForStoreData(store,
                                QStringLiteral("artistSongs:%1:%2").arg(qlonglong(artistId)).arg(offset),
                                QStringLiteral("artistSongs"), [&](const QVariantMap &page) {
            return page.value("artistId").toDouble() == artistId
                && page.value("offset").toInt() == offset;
        });
    }

    if (parser.isSet(commentsOption)) {
        const QStringList parts = parser.value(commentsOption).split(u':');
        const double songId = parts.value(0).toDouble();
        const int offset = parts.value(1).toInt();
        if (songId <= 0 || offset < 0) {
            std::fprintf(stderr, "expected <songId>[:offset]\n");
            return 1;
        }
        return waitForStoreData(store,
                                QStringLiteral("comments:%1:%2").arg(qlonglong(songId)).arg(offset),
                                QStringLiteral("commentPage"), [&](const QVariantMap &page) {
            return page.value("songId").toDouble() == songId
                && page.value("offset").toInt() == offset;
        });
    }

    if (parser.isSet(detailOption)) {
        const QString key = parser.value(detailOption);
        const QString kind = key.section(u':', 0, 0);
        const double id = key.section(u':', 1).toDouble();
        if ((kind != QLatin1String("artist") && kind != QLatin1String("album")) || id <= 0) {
            std::fprintf(stderr, "expected artist:<id> or album:<id>\n");
            return 1;
        }
        return waitForStoreData(store, key, kind, [&](const QVariantMap &value) {
            return value.value("id").toDouble() == id;
        });
    }

    if (parser.isSet(subscribeSelfTestOption)) {
        if (!client.hasMusicU()) {
            std::fprintf(stderr, "not logged in\n");
            return 1;
        }
        store.waitForInitial();
        QSet<double> subscribed;
        for (const QVariant &v : store.personal().value("subscribed").toList())
            subscribed.insert(v.toMap().value("id").toDouble());
        double pid = 0;
        for (const QVariant &v : store.personal().value("playlists").toList()) {
            const QVariantMap card = v.toMap();
            const double id = card.value("id").toDouble();
            if (id > 0 && !subscribed.contains(id)) {
                pid = id;
                std::printf("candidate %lld %s\n", qlonglong(pid), qPrintable(card.value("name").toString()));
                break;
            }
        }
        if (pid <= 0) {
            std::fprintf(stderr, "no unsubscribed public playlist found\n");
            return 2;
        }
        QEventLoop loop;
        int rc = 2;
        int step = 0;
        const auto has = [&] {
            for (const QVariant &v : store.personal().value("subscribed").toList())
                if (v.toMap().value("id").toDouble() == pid)
                    return true;
            return false;
        };
        QObject::connect(&store, &Store::playlistChanged, &loop, [&](double id) {
            if (id != pid)
                return;
            if (step == 0) {
                if (!has()) {
                    std::fprintf(stderr, "subscribe landed but local subscribed is missing\n");
                    loop.quit();
                    return;
                }
                step = 1;
                std::printf("subscribed\n");
                store.setPlaylistSubscribed(pid, false);
            } else if (step == 1) {
                if (has()) {
                    std::fprintf(stderr, "unsubscribe landed but local subscribed still has it\n");
                    loop.quit();
                    return;
                }
                std::printf("unsubscribed, local=ok\n");
                rc = 0;
                loop.quit();
            }
        });
        QObject::connect(&store, &Store::playlistActionFailed, &loop,
                         [&](const QString &action, const QString &message) {
            std::fprintf(stderr, "%s: %s\n", qPrintable(action), qPrintable(message));
            loop.quit();
        });
        QTimer::singleShot(30 * 1000, &loop, &QEventLoop::quit);
        store.setPlaylistSubscribed(pid, true);
        loop.exec();
        if (rc != 0 && has()) {
            // 失败也尽量取消订阅，账号回到原状
            QEventLoop cleanup;
            QObject::connect(&store, &Store::playlistChanged, &cleanup, [&](double id) {
                if (id == pid)
                    cleanup.quit();
            });
            QTimer::singleShot(5 * 1000, &cleanup, &QEventLoop::quit);
            store.setPlaylistSubscribed(pid, false);
            cleanup.exec();
        }
        return rc;
    }

    if (parser.isSet(playlistSelfTestOption)) {
        // 端到端验证歌单写接口；只创建一个私有测试歌单，最后删除，账号回到原状。
        if (!client.hasMusicU()) {
            std::fprintf(stderr, "not logged in\n");
            return 1;
        }
        // 先加载首屏，trackById 才能找到 287398；也让本地歌单卡片的计数/曲目检查有意义
        store.waitForInitial();
        QEventLoop loop;
        int rc = 2;
        int step = 0;
        double pid = 0;
        bool localOk = true;
        const auto cardIn = [&](const char *key) -> QVariantMap {
            for (const QVariant &v : store.personal().value(QLatin1String(key)).toList()) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() == pid)
                    return card;
            }
            return {};
        };
        const QString name = QStringLiteral("云听自检 %1").arg(QDateTime::currentSecsSinceEpoch());
        QObject::connect(&store, &Store::playlistCreated, &loop, [&](double id, const QString &) {
            if (step != 0) return;
            pid = id;
            step = 1;
            localOk = localOk && !cardIn("mine").isEmpty();
            std::printf("created %lld\n", qlonglong(pid));
            store.addSongToPlaylist(pid, 287398);
        });
        QObject::connect(&store, &Store::playlistChanged, &loop, [&](double id) {
            if (id != pid) return;
            if (step == 1) {
                step = 2;
                const QVariantMap card = cardIn("mine");
                localOk = localOk && card.value("count").toDouble() == 1
                    && !card.value("tracks").toList().isEmpty();
                std::printf("added\n");
                store.removeSongFromPlaylist(pid, 287398);
            } else if (step == 2) {
                step = 3;
                const QVariantMap card = cardIn("mine");
                localOk = localOk && card.value("count").toDouble() == 0
                    && card.value("tracks").toList().isEmpty();
                std::printf("removed\n");
                store.renamePlaylist(pid, name + QStringLiteral(" renamed"));
            } else if (step == 3) {
                step = 4;
                localOk = localOk && cardIn("mine").value("name").toString().endsWith(QStringLiteral(" renamed"));
                std::printf("renamed\n");
                store.deletePlaylist(pid);
            } else if (step == 4) {
                localOk = localOk && cardIn("mine").isEmpty();
                std::printf("deleted, local=%s\n", localOk ? "ok" : "bad");
                rc = localOk ? 0 : 1;
                loop.quit();
            }
        });
        QObject::connect(&store, &Store::playlistActionFailed, &loop,
                         [&](const QString &action, const QString &message) {
            std::fprintf(stderr, "%s: %s\n", qPrintable(action), qPrintable(message));
            rc = 1;
            loop.quit();
        });
        QTimer::singleShot(45 * 1000, &loop, &QEventLoop::quit);
        store.createPlaylist(name, true);
        loop.exec();
        if (rc != 0 && pid > 0) {
            // 失败也尽量删除测试歌单，避免留下垃圾；等请求真正落地再退出
            QEventLoop cleanup;
            QObject::connect(&store, &Store::playlistChanged, &cleanup, [&](double id) {
                if (id == pid)
                    cleanup.quit();
            });
            QTimer::singleShot(5 * 1000, &cleanup, &QEventLoop::quit);
            store.deletePlaylist(pid);
            cleanup.exec();
        }
        return rc;
    }

    if (parser.isSet(soakOption)) {
        if (parser.isSet(audioQualityOption))
            audio.setQuality(parser.value(audioQualityOption));
        const int seconds = std::max(10, parser.value(soakOption).toInt());
        const QList<double> ids{1973665667, 287398, 108485, 347230, 1330348068};
        const QStringList presets{QStringLiteral("flat"), QStringLiteral("pop"), QStringLiteral("rock"),
                                  QStringLiteral("bass"), QStringLiteral("vocal"), QStringLiteral("live"),
                                  QStringLiteral("spatial")};
        auto *step = new int(0);
        auto *failures = new int(0);
        QObject::connect(&audio, &AudioEngine::failed, &audio, [failures](const QString &reason) {
            ++*failures;
            std::fprintf(stderr, "[soak] failure: %s\n", qPrintable(reason));
        });
        auto rssKb = [] {
            QFile status(QStringLiteral("/proc/self/status"));
            if (!status.open(QIODevice::ReadOnly))
                return qint64(0);
            for (const QByteArray &line : status.readAll().split('\n'))
                if (line.startsWith("VmRSS:"))
                    return line.mid(6).trimmed().split(' ').value(0).toLongLong();
            return qint64(0);
        };
        audio.setVolume(0.55);
        auto *cycle = new QTimer(&audio);
        QObject::connect(cycle, &QTimer::timeout, &audio, [&audio, ids, presets, step, failures, rssKb] {
            const int i = (*step)++;
            audio.applyEqPreset(presets.at(i % presets.size()));
            audio.setVolume(0.35 + 0.05 * (i % 10));
            audio.setMuted(i % 12 == 6);
            audio.setTrack(QVariantMap{{"id", ids.at(i % ids.size())}, {"dur", 0}});
            audio.setPlaying(true);
            if (i % 2 == 0)
                audio.seek(12);
            if (i % 7 == 0) {
                const QVariantList outputs = audio.audioOutputs();
                if (outputs.size() > 1) {
                    const QString id = outputs.at((i / 7) % outputs.size()).toMap().value("id").toString();
                    audio.setOutputDevice(id);
                }
            }
            std::printf("[soak] step=%d track=%lld rss=%lldkB failures=%d\n", i,
                        qlonglong(ids.at(i % ids.size())), rssKb(), *failures);
            std::fflush(stdout);
        });
        cycle->start(20 * 1000);
        QTimer::singleShot(seconds * 1000, &app, [&audio, cycle, failures] {
            audio.setPlaying(false);
            audio.setMuted(false);
            cycle->stop();
            std::printf("[soak] done failures=%d\n", *failures);
            QCoreApplication::quit();
        });
        return app.exec();
    }

    if (parser.isSet(smokeOption)) {
        // 无窗口：事件循环跑在 QCoreApplication 节奏上，AudioEngine 自己打印状态
        if (parser.isSet(audioQualityOption))
            audio.setQuality(parser.value(audioQualityOption));
        const int rc = audio.smokeTest(parser.value(smokeOption).toDouble(), 6);
        if (rc != 0)
            return rc;
        return app.exec();
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // 托盘只在有窗口的运行方式下创建（--dump-store / --smoke-audio 上面已返回，无窗口）
    Tray tray(&audio);
    // 放歌时系统不睡眠（屏幕照常熄灭、锁屏）；看 MV 时屏幕常亮由 QML 设 backend.power.keepScreenOn
    PowerInhibitor power;
    QObject::connect(&audio, &AudioEngine::playingChanged, &power, [&] { power.setPreventSleep(audio.playing()); });

    store.refresh();   // 首屏数据异步到达，QML 通过 data / personal 的 NOTIFY 自动更新
    // 钥匙串超时后才读出 jar（例如在等用户解锁钱包）：先按匿名起来，读出后再按登录态刷一次
    if (!client.isReady())
        QObject::connect(&client, &NcmClient::ready, &store, &Store::refresh, Qt::SingleShotConnection);
    // QML 的 Settings 要求 organizationName，否则 QSettings 初始化失败（队列/窗口大小无法恢复）。
    // "." 让它与不设组织名时落在同一层：~/.config/yunting.conf，而不会多套一层 yunting/。
    // 放在这里而不是开头：之前构造的 Store / CookieVault / 图片缓存已拿到不带组织名的 XDG 路径。
    const QString imageCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images";
    app.setOrganizationName(QStringLiteral("."));
    QQmlApplicationEngine engine;
    ImageCacheFactory imageCache(imageCacheDir);
    engine.setNetworkAccessManagerFactory(&imageCache);
    // main.qml 用 root.backend.{store,audio,session,mpris,tray,layerShell,power} 取后端；QVariantMap 的 key 就是属性名
    LayerShellHelper layerShell;
    bool qrAvailable = false;
#ifdef YUNTING_QRCODE
    engine.addImageProvider(QStringLiteral("qrcode"), new QrCodeImageProvider);
    qrAvailable = true;
#endif
    QVariantMap backend{{"store", QVariant::fromValue<QObject *>(&store)},
                        {"audio", QVariant::fromValue<QObject *>(&audio)},
                        {"session", QVariant::fromValue<QObject *>(&session)},
                        {"mpris", QVariant::fromValue<QObject *>(&mpris)},
                        {"tray", QVariant::fromValue<QObject *>(&tray)},
                        {"layerShell", QVariant::fromValue<QObject *>(&layerShell)},
                        {"power", QVariant::fromValue<QObject *>(&power)},
                        {"qr", qrAvailable}};
    engine.setInitialProperties({{"backend", backend}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    // 界面编进资源：单一资源目录（qmldir 的 singleton 声明按同目录解析），
    // Snapshot.qml 与快照数据不编入，backend 注入后它的创建路径不会走到
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/org/nephelesound/yunting/main.qml")));
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
